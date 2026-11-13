/**
 * @file nmea_uart.cpp
 * @brief NMEA alıcısından satır okuyup gnss_service'e veren sürücü.
 *
 * Tasarım notları:
 *
 *  - **Satır tamponu taşarsa satır ATILIR, kırpılmaz.** Kırpılmış bir NMEA
 *    cümlesi sağlama toplamını kaybeder ve zaten reddedilirdi; ama yarısı
 *    bir sonraki satırın başına yapışırsa iki cümle birden bozulur. Taşmada
 *    senkronu bir sonraki '$'a kadar atarak kuruyoruz.
 *
 *  - **Görev CAN'den düşük öncelikte.** GNSS 1 Hz yayın yapar ve UART
 *    tamponu birkaç cümleyi tutar; CAN ise 250 kbit/s'te frame kaçırabilir.
 *
 *  - Alıcıya hiçbir şey YAZMIYORUZ (bkz. header).
 */

#include "nmea_uart.h"

#if ENABLE_GNSS

#include "../../services/gnss_service.h"
#include "../../services/nmea_parser.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "gnssuart";

#define GNSS_UART_RX_BUF 1024

static bool         s_running = false;
static TaskHandle_t s_task    = NULL;

static void gnss_rx_task(void* arg)
{
    (void)arg;
    static char line[NMEA_SENTENCE_MAX];
    size_t pos      = 0;
    bool   overflow = false;

    for (;;) {
        uint8_t ch;
        int r = uart_read_bytes((uart_port_t)GNSS_UART_NUM, &ch, 1,
                                portMAX_DELAY);
        if (r <= 0) continue;

        if (ch == '\n' || ch == '\r') {
            if (!overflow && pos > 0) {
                line[pos] = '\0';
                gnss_service_ingest(line);
            }
            pos = 0;
            overflow = false;
            continue;
        }

        /* Yeni cümle başlangıcı taşmadan kurtarır. */
        if (ch == '$') { pos = 0; overflow = false; }

        if (pos < sizeof(line) - 1) {
            line[pos++] = (char)ch;
        } else {
            /* Kırpılmış cümle göndermektense atıyoruz — bkz. dosya başı. */
            overflow = true;
            pos = 0;
        }
    }
}

esp_err_t gnss_uart_start(uint32_t baud)
{
    if (s_running) return ESP_OK;

    uart_config_t cfg = {};
    cfg.baud_rate = (int)baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity    = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install((uart_port_t)GNSS_UART_NUM,
                                        GNSS_UART_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config((uart_port_t)GNSS_UART_NUM, &cfg);
    if (err != ESP_OK) return err;

    /* TX pini de veriliyor ama sürücü yazmıyor; alıcıya bağlanmasa da olur. */
    err = uart_set_pin((uart_port_t)GNSS_UART_NUM, PIN_GNSS_TX, PIN_GNSS_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err)); return err; }

    BaseType_t r = xTaskCreatePinnedToCore(gnss_rx_task, "gnss_rx", 3072, NULL,
                                           TELEMETRY_TASK_PRIORITY, &s_task,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "alıcı görev oluşturulamadı"); return ESP_FAIL; }

    s_running = true;
    ESP_LOGI(TAG, "init OK (%lu baud, rx=%d tx=%d)", (unsigned long)baud,
             PIN_GNSS_RX, PIN_GNSS_TX);
    return ESP_OK;
}

esp_err_t gnss_uart_stop(void)
{
    if (!s_running) return ESP_OK;
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    uart_driver_delete((uart_port_t)GNSS_UART_NUM);
    s_running = false;
    return ESP_OK;
}

bool gnss_uart_running(void) { return s_running; }

#else   /* ENABLE_GNSS == 0 — modül yok, servis `gnss inject` ile beslenir */

esp_err_t gnss_uart_start(uint32_t baud) { (void)baud; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t gnss_uart_stop(void)           { return ESP_OK; }
bool      gnss_uart_running(void)        { return false; }

#endif  /* ENABLE_GNSS */
