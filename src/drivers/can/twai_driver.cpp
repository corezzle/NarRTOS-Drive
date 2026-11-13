/**
 * @file twai_driver.cpp
 * @brief ESP32-S3 dahili TWAI denetleyicisi (bkz. header).
 *
 * Tasarım notları:
 *  - `tx_queue_len = 0`: cihaz aracın hattına YAZMAZ. Takip cihazının veri
 *    yoluna frame basması gereksiz ve riskli; TX kuyruğunu hiç kurmuyoruz ki
 *    yanlışlıkla bile mümkün olmasın.
 *  - `rx_queue_len = 64`: 250 kbit/s'te yoğun bir hat ~2000 frame/sn üretir.
 *    Alıcı görev çekirdek 1'de ve öncelik 6'da; kuyruk yalnız kısa tıkanmalar
 *    için tampon. Taşarsa TWAI_ALERT_RX_QUEUE_FULL ile sayılır.
 *  - Bus-off: hat arızasında denetleyici kendini kapatır. Otomatik kurtarma
 *    başlatıyoruz — aracın kontağı kapanıp açıldığında ya da kablo geçici
 *    temassızlığında cihazın elle müdahale beklememesi için.
 */

#include "twai_driver.h"

#if ENABLE_CAN

#include "../../core/os_config.h"
#include "../../services/can_service.h"

#include <driver/twai.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "twai";

static bool         s_running = false;
static TaskHandle_t s_task    = NULL;

static twai_timing_config_t timing_for(uint32_t kbps)
{
    switch (kbps) {
        case 125:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();  return t; }
        case 500:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  return t; }
        case 1000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();    return t; }
        default:   { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();  return t; }
    }
}

static void twai_rx_task(void* arg)
{
    (void)arg;
    for (;;) {
        twai_message_t msg;
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(1000));

        if (err == ESP_OK) {
            /* Uzaktan istek (RTR) frame'inde veri yok — tabloya koymanın
             * anlamı yok, sunucu da çözemez. */
            if (!msg.rtr) {
                can_service_ingest(msg.identifier, msg.extd, msg.data,
                                   msg.data_length_code);
            }
            continue;
        }

        if (err == ESP_ERR_TIMEOUT) {
            /* Hat sessiz. Normal olabilir (kontak kapalı) — bus durumunu
             * kontrol edip bus-off ise kurtarmayı tetikliyoruz. */
            twai_status_info_t st;
            if (twai_get_status_info(&st) == ESP_OK &&
                st.state == TWAI_STATE_BUS_OFF) {
                ESP_LOGW(TAG, "bus-off — kurtarma başlatılıyor");
                twai_initiate_recovery();
            }
            continue;
        }

        ESP_LOGW(TAG, "twai_receive: %s", esp_err_to_name(err));
    }
}

esp_err_t can_twai_start(uint32_t kbps)
{
    if (s_running) return ESP_OK;

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    g.tx_queue_len = 0;    /* cihaz hatta YAZMAZ */
    g.rx_queue_len = 64;

    twai_timing_config_t t = timing_for(kbps);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    /* Filtre yok: hangi PGN'lerin ilgilendiği sunucuda (DBC) belli oluyor,
     * cihazda süzmek bilgiyi erkenden atmak olur. Tablo zaten kimlik başına
     * tek girdi tuttuğu için hacim sorun değil. */

    esp_err_t err = twai_driver_install(&g, &t, &f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver_install: %s", esp_err_to_name(err));
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_start: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return err;
    }

    BaseType_t r = xTaskCreatePinnedToCore(
        twai_rx_task, "twai_rx", 3072, NULL, CAN_TASK_PRIORITY, &s_task,
        CAN_TASK_CORE);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "alıcı görev oluşturulamadı");
        twai_stop();
        twai_driver_uninstall();
        return ESP_FAIL;
    }

    s_running = true;
    ESP_LOGI(TAG, "init OK (%lu kbit/s, TX kapalı, tx=%d rx=%d)",
             (unsigned long)kbps, PIN_CAN_TX, PIN_CAN_RX);
    return ESP_OK;
}

esp_err_t can_twai_stop(void)
{
    if (!s_running) return ESP_OK;
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    twai_stop();
    twai_driver_uninstall();
    s_running = false;
    return ESP_OK;
}

bool can_twai_running(void) { return s_running; }

int can_twai_bus_state(void)
{
    if (!s_running) return -1;
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) return -1;
    switch (st.state) {
        case TWAI_STATE_STOPPED:    return 0;
        case TWAI_STATE_RUNNING:    return 1;
        case TWAI_STATE_BUS_OFF:    return 2;
        case TWAI_STATE_RECOVERING: return 3;
        default:                    return -1;
    }
}

#else   /* ENABLE_CAN == 0 — donanım yok, servis `can inject` ile beslenir */

esp_err_t can_twai_start(uint32_t kbps) { (void)kbps; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t can_twai_stop(void)           { return ESP_OK; }
bool      can_twai_running(void)        { return false; }
int       can_twai_bus_state(void)      { return -1; }

#endif  /* ENABLE_CAN */
