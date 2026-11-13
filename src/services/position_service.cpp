/**
 * @file position_service.cpp
 * @brief Uydu ve tahmin arasında geçiş (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Araç hızı can_service tablosundan okunuyor**, ayrı bir kanal
 *    kurulmadan. Tablo zaten kimlik başına son frame'i tutuyor; aradığımız
 *    sinyal orada hazır duruyor. can_service'in bu servisten haberi yok.
 *
 *  - **Hız yoksa tahmin başlamıyor.** Aracın kendi hız verisi olmadan mesafe
 *    bilinemez ve jiroskop tek başına konum üretemez. Bu durumda konum
 *    "bilinmiyor" olarak raporlanıyor; uydurma bir nokta göndermek yerine.
 *
 *  - **Sapma ölçümü yalnız araç dururken.** Hareket hâlindeki dönüşleri sapma
 *    sanmamak için hız sıfır şartı var.
 */

#include "position_service.h"
#include "../core/os_config.h"
#include "../drivers/imu/icm42688p.h"
#include "can_service.h"
#include "can_signals.h"
#include "gnss_service.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "pos";

#define POS_TICK_MS 1000

static SemaphoreHandle_t s_mutex = NULL;
static dr_state_t        s_dr;
static position_t        s_cur;
static position_stats_t  s_st;

/* Jiroskop sapma kestirimi. */
static int32_t  s_bias_sum   = 0;
static uint32_t s_bias_count = 0;
static int16_t  s_bias       = 0;

static int64_t s_last_tick = 0;

const char* position_source_name(pos_source_t s)
{
    switch (s) {
        case POS_GNSS: return "uydu";
        case POS_DR:   return "tahmin";
        default:       return "yok";
    }
}

/** can_service tablosunda tekerlek hızını ara. */
static bool read_can_speed(uint16_t* out_kmh_x10)
{
    static can_frame_t buf[CAN_FRAMES_PER_REPORT];
    size_t n = can_service_snapshot(buf, CAN_FRAMES_PER_REPORT);
    for (size_t i = 0; i < n; i++) {
        if (can_signal_wheel_speed(buf[i].id, buf[i].data, buf[i].dlc,
                                   out_kmh_x10))
            return true;
    }
    return false;
}

static void tick(void)
{
    const int64_t now = esp_timer_get_time() / 1000;
    uint32_t dt = (s_last_tick == 0) ? POS_TICK_MS
                                     : (uint32_t)(now - s_last_tick);
    s_last_tick = now;
    if (dt == 0) return;

    uint16_t can_speed = 0;
    const bool have_speed = read_can_speed(&can_speed);

    int16_t yaw = 0;
    const bool have_gyro = imu_read_yaw_rate(&yaw);
    if (have_gyro) yaw = (int16_t)(yaw - s_bias);

    /* Sapma kestirimi: yalnız araç dururken ve hız verisi varken. */
    if (have_gyro && have_speed && can_speed == 0) {
        int16_t raw = 0;
        if (imu_read_yaw_rate(&raw)) {
            s_bias_sum += raw;
            s_bias_count++;
            if (s_bias_count >= 30) {          /* ~30 saniyelik ortalama */
                s_bias = (int16_t)(s_bias_sum / (int32_t)s_bias_count);
                s_bias_sum = 0;
                s_bias_count = 0;
            }
        }
    }

    gnss_fix_t fix;
    const bool have_fix = gnss_service_get_fix(&fix);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (have_fix) {
        /* Uydu var: doğrudan kullan ve tahmini bu noktadan yeniden başlat.
         * Böylece birikmiş hata atılıyor. */
        s_cur.source          = POS_GNSS;
        s_cur.lat_e7          = fix.lat_e7;
        s_cur.lon_e7          = fix.lon_e7;
        s_cur.speed_kmh_x10   = fix.speed_kmh_x10;
        s_cur.heading_deg_x10 = fix.course_deg_x10;
        s_cur.sats            = fix.sats;
        s_cur.uncertainty_m   = 5;

        dr_seed(&s_dr, fix.lat_e7, fix.lon_e7, fix.course_deg_x10);
        s_st.gnss_fixes++;
    } else if (s_dr.valid && have_speed) {
        /* Uydu yok: tahminle ilerlet. */
        if (s_st.source != POS_DR) s_st.dr_sessions++;

        dr_step(&s_dr, can_speed, yaw, dt, have_gyro);

        if (s_dr.valid) {
            s_cur.source          = POS_DR;
            s_cur.lat_e7          = s_dr.lat_e7;
            s_cur.lon_e7          = s_dr.lon_e7;
            s_cur.speed_kmh_x10   = can_speed;
            s_cur.heading_deg_x10 = s_dr.heading_deg_x10;
            s_cur.sats            = 0;
            s_cur.uncertainty_m   = dr_uncertainty_m(&s_dr);
            s_st.dr_steps++;
        } else {
            /* Sınır aşıldı: aşırı sapmış tahmini göndermektense konum yok. */
            s_cur.source = POS_NONE;
            s_st.dr_expired++;
            ESP_LOGW(TAG, "tahmin sınırı aşıldı, konum bilinmiyor "
                          "(%lu m yol, %u m belirsizlik)",
                     (unsigned long)s_dr.distance_m, dr_uncertainty_m(&s_dr));
        }
    } else {
        /* Ne uydu ne hız verisi var. Tahmin yürütülemez. */
        s_cur.source = POS_NONE;
    }

    s_st.source            = s_cur.source;
    s_st.have_can_speed    = have_speed;
    s_st.have_gyro         = have_gyro;
    s_st.gyro_bias_dps_x10 = s_bias;
    s_st.uncertainty_m     = s_cur.uncertainty_m;
    s_st.dr_distance_m     = s_dr.distance_m;

    xSemaphoreGive(s_mutex);
}

static void position_task(void* arg)
{
    (void)arg;
    for (;;) {
        tick();
        vTaskDelay(pdMS_TO_TICKS(POS_TICK_MS));
    }
}

esp_err_t position_service_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    dr_reset(&s_dr);
    memset(&s_cur, 0, sizeof(s_cur));
    memset(&s_st, 0, sizeof(s_st));

    esp_err_t ierr = imu_start();
    if (ierr == ESP_ERR_NOT_SUPPORTED)
        ESP_LOGI(TAG, "jiroskop derleme dışı (ENABLE_IMU=0) — tahmin yalnız "
                      "hız verisiyle, belirsizlik daha hızlı büyür");
    else if (ierr != ESP_OK)
        ESP_LOGW(TAG, "jiroskop başlatılamadı: %s", esp_err_to_name(ierr));

    BaseType_t r = xTaskCreatePinnedToCore(position_task, "pos_svc", 4096, NULL,
                                           TELEMETRY_TASK_PRIORITY, NULL,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init OK (tahmin sınırı %u m / %u sn)",
             (unsigned)DR_MAX_UNCERTAINTY_M, (unsigned)(DR_MAX_ELAPSED_MS / 1000));
    return ESP_OK;
}

bool position_service_get(position_t* out)
{
    if (!out || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool ok = (s_cur.source != POS_NONE);
    if (ok) *out = s_cur;
    xSemaphoreGive(s_mutex);
    return ok;
}

void position_service_stats(position_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_mutex);
}
