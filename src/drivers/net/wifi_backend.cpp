/**
 * @file wifi_backend.cpp
 * @brief WiFi station arka ucu — atölye bağlantısı.
 *
 * Sahada kullanılmayacak: araç takibinde WiFi anlamsız, ayrıca araç takip sunucusu
 * cihazları IMEI ile anahtarlıyor ve IMEI modemden gelir. Bu arka uç modem
 * donanımı gelene kadar MQTT ve store&forward yolunu gerçek bir ağ üzerinde
 * sürebilmek için var — yoksa M3'ün tamamı yalnızca derlenen kod olurdu.
 *
 * Wear'ın wifi_service.cpp'sinden alınanlar: event group + STA olay
 * işleyicisi kalıbı.
 *
 * Wear'dan AYRILAN nokta — yeniden deneme burada YOK. Wear kendi içinde 5 kez
 * deniyordu; burada tek deneme yapılıp sonuç dönüyor, tekrar politikası
 * net_service'te. Sebebi: aynı politikanın modem arka ucunda ikinci kez
 * yazılmasını istemiyoruz, ve araçta doğru davranış üstel bekleme (tünelde
 * saniyede bir denemek boşa akım).
 *
 * Kullanıcı tercihi ve SSID listesi gibi Wear'a özgü işler taşınmadı: ekran
 * yok. BSSID taraması ise M9'da GELDİ (wifi_scan.h): kapalı alanda konum
 * için çevredeki ağların MAC listesi rapora ekleniyor. Radyonun tek sahibi
 * bu dosya olduğu için tarama da burada.
 */

#include "net_backend.h"
#include "wifi_scan.h"

#if ENABLE_WIFI

#include "../../core/config_store.h"
#include "../../core/os_config.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <string.h>

static const char* TAG = "wifi";

#define BIT_GOT_IP BIT0
#define BIT_FAILED BIT1

static EventGroupHandle_t s_events    = NULL;
static bool               s_inited    = false;
static volatile bool      s_connected = false;

static void on_wifi_event(void* arg, esp_event_base_t base, int32_t id,
                          void* data)
{
    (void)arg; (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_events) {
            xEventGroupClearBits(s_events, BIT_GOT_IP);
            xEventGroupSetBits(s_events, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        if (s_events) {
            xEventGroupClearBits(s_events, BIT_FAILED);
            xEventGroupSetBits(s_events, BIT_GOT_IP);
        }
        ESP_LOGI(TAG, "IP alındı");
    }
}

static esp_err_t wifi_init(void)
{
    if (s_inited) return ESP_OK;

    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;

    /* Bu üçü başka bir servis tarafından çağrılmış olabilir (OTA, SNTP...).
     * INVALID_STATE "zaten kurulu" demek, hata değil. */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err)); return err; }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    /* Ayarları NVS'e yazdırmıyoruz: SSID/şifre config_store'da duruyor, iki
     * yerde kopyası olması "hangisi geçerli" sorusunu doğurur. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err)); return err; }

    s_inited = true;
    ESP_LOGI(TAG, "init OK (station)");
    return ESP_OK;
}

static bool wifi_is_configured(void)
{
    char ssid[33] = {0};
    config_store_get_str(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    return ssid[0] != '\0';
}

static esp_err_t wifi_connect(uint32_t timeout_ms)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    char ssid[33] = {0};
    char pass[65] = {0};
    config_store_get_str(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    config_store_get_str(NVS_KEY_WIFI_PASS, pass, sizeof(pass), "");
    if (!ssid[0]) return ESP_ERR_INVALID_STATE;

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strlcpy((char*)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char*)wc.sta.password, pass, sizeof(wc.sta.password));
    /* Şifre boşsa açık ağ; auth eşiğini OPEN'a çekmezsek ESP açık ağı
     * atlıyor. */
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) return err;

    xEventGroupClearBits(s_events, BIT_GOT_IP | BIT_FAILED);
    err = esp_wifi_connect();
    if (err != ESP_OK) return err;

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_FAILED,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_GOT_IP) return ESP_OK;

    /* Başarısız — bağlantı denemesini bırakıyoruz ki sürücü kendi başına
     * yeniden denemesin; tekrar kararı net_service'in. */
    esp_wifi_disconnect();
    return (bits & BIT_FAILED) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

static void wifi_disconnect_(void)
{
    if (!s_inited) return;
    esp_wifi_disconnect();
    s_connected = false;
}

static bool wifi_is_connected(void) { return s_connected; }

static const net_backend_t s_wifi = {
    "wifi", wifi_init, wifi_connect, wifi_disconnect_,
    wifi_is_connected, wifi_is_configured,
};

const net_backend_t* net_backend_wifi(void) { return &s_wifi; }

esp_err_t wifi_scan_bssids(wifi_scan_ap_t* out, uint8_t max, uint8_t* out_count)
{
    if (out_count) *out_count = 0;
    if (!out || max == 0) return ESP_ERR_INVALID_ARG;

    /* Radyoyu ayağa kaldır (bağlanmadan). Arka uç olarak modem seçiliyse
     * wifi_init'i başka kimse çağırmıyor; ilk tarama burada başlatır. */
    esp_err_t err = wifi_init();
    if (err != ESP_OK) return err;

    /* scan_time 0 bırakılıyor: sürücü varsayılanı güvenli (Wear'daki coex
     * dersi; burada BT yok ama varsayılanı ezmenin kazancı da yok). */
    wifi_scan_config_t cfg = {};
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    err = esp_wifi_scan_start(&cfg, true /* bloklayıcı */);
    if (err != ESP_OK) return err;   /* bağlanma sürüyorsa ESP_ERR_WIFI_STATE */

    wifi_ap_record_t recs[WIFI_SCAN_MAX_APS];
    uint16_t n = (max > WIFI_SCAN_MAX_APS) ? WIFI_SCAN_MAX_APS : max;
    err = esp_wifi_scan_get_ap_records(&n, recs);   /* kalan kayıtları boşaltır */
    if (err != ESP_OK) return err;

    /* Sürücü RSSI'ye göre sıralı verir; ilk N zaten en güçlüler. */
    uint8_t count = 0;
    for (uint16_t i = 0; i < n && count < max; i++) {
        memcpy(out[count].bssid, recs[i].bssid, 6);
        out[count].rssi = recs[i].rssi;
        count++;
    }
    if (out_count) *out_count = count;
    return ESP_OK;
}

#else   /* ENABLE_WIFI == 0 */

const net_backend_t* net_backend_wifi(void) { return NULL; }

esp_err_t wifi_scan_bssids(wifi_scan_ap_t* out, uint8_t max, uint8_t* out_count)
{
    (void)out; (void)max;
    if (out_count) *out_count = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
