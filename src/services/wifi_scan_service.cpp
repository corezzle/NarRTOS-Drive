/**
 * @file wifi_scan_service.cpp
 * @brief WiFi tarama politikası (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Tarama başarısız olursa eski liste KORUNUYOR.** Silinseydi geçici bir
 *    sürücü hatası (bağlanma sürerken meşgul dönmesi gibi) elde ne varsa onu
 *    da götürürdü. Liste yaşlanmaya devam ediyor; bayatlarsa rapora zaten
 *    girmiyor.
 *
 *  - **ENABLE_WIFI=0 iken görev hiç kurulmuyor.** Sürücü her turda
 *    NOT_SUPPORTED döner, sayaç şişer, log dolar; anlamı yok. Önbellek ve
 *    `wifi inject` yine çalışıyor, kayıt yolu donanımsız da sürülebiliyor.
 */

#include "wifi_scan_service.h"
#include "../core/os_config.h"
#include "position_service.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "wscan";

static SemaphoreHandle_t s_mutex = NULL;
static wifi_scan_ap_t    s_list[WIFI_SCAN_MAX_APS];
static uint8_t           s_count = 0;
static int64_t           s_scanned_at = -1;   /* uptime ms; -1 = hiç */
static wifi_scan_stats_t s_st;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

esp_err_t wifi_scan_service_scan_now(void)
{
    wifi_scan_ap_t tmp[WIFI_SCAN_MAX_APS];
    uint8_t n = 0;
    esp_err_t err = wifi_scan_bssids(tmp, WIFI_SCAN_MAX_APS, &n);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        memcpy(s_list, tmp, (size_t)n * sizeof(wifi_scan_ap_t));
        s_count      = n;
        s_scanned_at = now_ms();
        s_st.scans_ok++;
        s_st.last_count = n;
    } else {
        s_st.scans_failed++;   /* eski liste korunuyor, bkz. dosya başı */
    }
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED)
        ESP_LOGW(TAG, "tarama başarısız: %s", esp_err_to_name(err));
    return err;
}

size_t wifi_scan_service_get(wifi_scan_ap_t* out, size_t max, uint32_t* age_ms)
{
    if (age_ms) *age_ms = UINT32_MAX;
    if (!out || max == 0 || !s_mutex) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t n = 0;
    if (s_scanned_at >= 0) {
        n = (s_count < max) ? s_count : max;
        memcpy(out, s_list, n * sizeof(wifi_scan_ap_t));
        if (age_ms) {
            int64_t age = now_ms() - s_scanned_at;
            *age_ms = (age > UINT32_MAX) ? UINT32_MAX : (uint32_t)age;
        }
    }
    xSemaphoreGive(s_mutex);
    return n;
}

void wifi_scan_service_inject(const uint8_t mac[6], int8_t rssi)
{
    if (!mac || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t i = (s_count < WIFI_SCAN_MAX_APS) ? s_count : WIFI_SCAN_MAX_APS - 1;
    memcpy(s_list[i].bssid, mac, 6);
    s_list[i].rssi = rssi;
    if (s_count < WIFI_SCAN_MAX_APS) s_count++;
    s_scanned_at = now_ms();
    s_st.injects++;
    s_st.last_count = s_count;
    xSemaphoreGive(s_mutex);
}

void wifi_scan_service_clear(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_count      = 0;
    s_scanned_at = -1;
    s_st.last_count = 0;
    xSemaphoreGive(s_mutex);
}

void wifi_scan_service_stats(wifi_scan_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_st;
    out->age_ms = UINT32_MAX;
    if (s_scanned_at >= 0) {
        int64_t age = now_ms() - s_scanned_at;
        out->age_ms = (age > UINT32_MAX) ? UINT32_MAX : (uint32_t)age;
    }
    xSemaphoreGive(s_mutex);
}

#if ENABLE_WIFI
static void scan_task(void* arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_POLL_MS));

        /* Uydu varken taranmaz; bkz. header. */
        position_stats_t ps;
        position_service_stats(&ps);
        if (ps.source == POS_GNSS) continue;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        int64_t at = s_scanned_at;
        xSemaphoreGive(s_mutex);
        if (at >= 0 && (now_ms() - at) < WIFI_SCAN_INTERVAL_MS) continue;

        wifi_scan_service_scan_now();
    }
}
#endif

esp_err_t wifi_scan_service_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_count = 0;
    s_scanned_at = -1;
    memset(&s_st, 0, sizeof(s_st));
    s_st.hw = (ENABLE_WIFI != 0);

#if ENABLE_WIFI
    BaseType_t r = xTaskCreatePinnedToCore(scan_task, "wscan_svc", 4096, NULL,
                                           TELEMETRY_TASK_PRIORITY, NULL,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }
    ESP_LOGI(TAG, "init OK (uydu yokken %u sn'de bir, en fazla %u ağ)",
             (unsigned)(WIFI_SCAN_INTERVAL_MS / 1000),
             (unsigned)WIFI_SCAN_MAX_APS);
#else
    ESP_LOGI(TAG, "tarama derleme dışı (ENABLE_WIFI=0) — `wifi inject` çalışır");
#endif
    return ESP_OK;
}
