/**
 * @file power_service.cpp
 * @brief Güç yönetimi (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Uyku sayacı RTC belleğinde.** Normal statik değişken her uyanışta
 *    sıfırlanırdı; RTC belleği derin uykuda enerjili kalıyor. Sayaç saha
 *    teşhisi için: "cihaz geceleri kaç kez uyanmış" sorusunun cevabı.
 *
 *  - **Uyumadan önce `offline` yayınlanıyor** (elden geldiğince). Vasiyet
 *    (LWT) yalnız BEKLENMEDİK kopmada devreye girer; düzgün uyuyan cihaz
 *    için broker'a doğru bilgiyi kendimiz bırakıyoruz. Yayın başarısızsa
 *    yine uyunur; akü beklemekten kıymetli.
 *
 *  - **Zamanlayıcı uyanışında bekleme kısa** (POWER_TIMER_AWAKE_MS). Uzun
 *    bekleme her park periyodunda dakikalarca tam güç demek olurdu ve
 *    uykunun kazancını geri yerdi.
 *
 *  - **Kontakla uyanma kontak pininin YÜKSELMESİYLE** (ext1 ANY_HIGH).
 *    Pin zaten aşağı çekmeli (bkz. telemetry_service); araç çalışınca
 *    bölücüden gelen gerilim pini kaldırıyor.
 */

#include "power_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "mqtt_contract.h"
#include "mqtt_service.h"
#include "net_service.h"
#include "ota_service.h"
#include "power_policy.h"
#include "store_forward.h"
#include "telemetry_service.h"

#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "power";

/* RTC belleği: derin uykuda korunur, soğuk açılışta sıfırlanır. */
static RTC_DATA_ATTR uint32_t s_rtc_sleep_cycles = 0;

static const char* s_wake_cause = "soğuk açılış";
static bool        s_timer_wake = false;
static const char* s_why        = "henüz değerlendirilmedi";
static int64_t     s_ign_off_at = -1;   /* uptime ms; -1 = kontak açık */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool sleep_enabled(void)
{
    uint16_t v = 0;
    config_store_get_u16(NVS_KEY_POWER_SLEEP, &v, 0);
    return v != 0;
}

void power_service_sleep_now(void)
{
    ESP_LOGW(TAG, "derin uyku: %lu sn sonra ya da kontakla uyanır "
                  "(toplam %lu uyku)",
             (unsigned long)(POWER_WAKE_INTERVAL_MS / 1000),
             (unsigned long)(s_rtc_sleep_cycles + 1));

    /* Broker'a düzgün veda; vasiyet yalnız beklenmedik kopmayı kapatıyor.
     * Başarısızsa da uyunur. */
    mqtt_service_publish_raw(MQTT_LEAF_STATUS, MQTT_LWT_PAYLOAD,
                             MQTT_QOS_STATUS, MQTT_RETAIN_STATUS);
    vTaskDelay(pdMS_TO_TICKS(700));   /* yayının çıkması için kısa pay */

    s_rtc_sleep_cycles++;

    esp_sleep_enable_timer_wakeup((uint64_t)POWER_WAKE_INTERVAL_MS * 1000ULL);
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_IGNITION, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
    /* buraya dönülmez */
}

static void gather_and_maybe_sleep(void)
{
    telemetry_stats_t ts;
    telemetry_service_stats(&ts);

    if (ts.ignition) {
        s_ign_off_at = -1;
    } else if (s_ign_off_at < 0) {
        s_ign_off_at = now_ms();
    }

    sf_stats_t sf;
    store_forward_stats(&sf);

    ota_status_t ota;
    ota_service_status(&ota);

    power_inputs_t in = {};
    in.enabled         = sleep_enabled();
    in.ignition        = ts.ignition;
    in.net_online      = net_service_is_online();
    in.ota_busy        = (ota.state == OTA_CHECKING ||
                          ota.state == OTA_DOWNLOADING ||
                          ota.state == OTA_INSTALLED);
    in.ota_waiting_install = (ota.state == OTA_AVAILABLE);
    in.ign_off_ms      = (s_ign_off_at < 0) ? 0
                         : (uint32_t)(now_ms() - s_ign_off_at);
    in.pending_records = sf.stored;
    /* Zamanlayıcı uyanışı kısa pencere: her park periyodunda dakikalarca
     * tam güç, uykunun kazancını geri yerdi. */
    in.sleep_delay_ms  = s_timer_wake ? POWER_TIMER_AWAKE_MS
                                      : POWER_SLEEP_DELAY_MS;
    in.max_awake_ms    = POWER_MAX_AWAKE_MS;

    const char* why = NULL;
    power_action_t act = power_policy_eval(&in, &why);
    s_why = why;

    if (act == POWER_GO_SLEEP) power_service_sleep_now();
}

static void power_task(void* arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POWER_POLL_MS));
        gather_and_maybe_sleep();
    }
}

esp_err_t power_service_init(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:
            s_wake_cause = "zamanlayıcı (park raporu)";
            s_timer_wake = true;
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            s_wake_cause = "kontak açıldı";
            break;
        default:
            s_wake_cause = "soğuk açılış";
            break;
    }

    if (s_timer_wake) {
        /* Bunun için uyandık: periyodu bekletmeden bir park raporu üret.
         * Saat RTC'den korunduğu için damga hemen çözülür; uydu soğuk
         * başladığı için konum büyük ihtimalle "yok" gider, dürüst olan bu. */
        ESP_LOGI(TAG, "zamanlayıcı uyanışı: park raporu üretiliyor");
        telemetry_service_capture_now();
    }

    BaseType_t r = xTaskCreatePinnedToCore(power_task, "power_svc", 4096, NULL,
                                           TELEMETRY_TASK_PRIORITY, NULL,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init OK (uyanma: %s, uyku %s, toplam %lu uyku)",
             s_wake_cause, sleep_enabled() ? "AÇIK" : "kapalı",
             (unsigned long)s_rtc_sleep_cycles);
    return ESP_OK;
}

void power_service_stats(power_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->enabled      = sleep_enabled();
    out->why          = s_why;
    out->wake_cause   = s_wake_cause;
    out->sleep_cycles = s_rtc_sleep_cycles;
    out->ign_off_ms   = (s_ign_off_at < 0) ? -1 : (now_ms() - s_ign_off_at);
    out->timer_wake   = s_timer_wake;
}
