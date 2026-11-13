// ═══════════════════════════════════════════════════════════════════════════
// NarRTOS-Drive — giriş noktası
//
// M0: derlenen, açılan ve durumunu loglayan taban. Servisler henüz yok;
// aşağıdaki boot sırası Wear'daki disiplini izler — her adım bir öncekinin
// kurduğu altyapıya dayanır, sıra bozulamaz.
//
//   1. nvs_flash_init()   kalıcı ayar deposu (IMEI, MQTT, APN buradan okunur)
//   2. config_store       NVS'ten ayarları oku
//   3. command_service    USB konfigürasyon yolu — servislerden ÖNCE gelir ki
//                         yanlış ayarla açılan cihaza müdahale edilebilsin
//   4. can_service        tabloyu kur, `can inject` sink'ini kaydet
//   5. twai_driver        gerçek hattı dinle (ENABLE_CAN)
//   6. store_forward      flash tamponunu tara — AĞDAN ÖNCE, çünkü bağlantı
//                         gelir gelmez boşaltılacak tampon hazır olmalı
//   7. net_service        bağlantıyı ayakta tut (wifi / modem)
//   8. mqtt_service       tamponu broker'a boşalt + uzak komut taşıyıcısı
//   9. time_service       duvar saati (SNTP) — damganın kaynağı
//  10. telemetry_service  periyodik rapor üretimi
//  11. gnss_service       UART NMEA (M5)
//  12. position_service   uydu/tahmin birleştirme (M8)
//  13. wifi_scan_service  kapalı alanda konumlama için AP listesi (M9)
//  14. power_service      kontak kapanınca derin uyku (M10)
//
// event_bus hâlâ YOK. Ağ durumunun iki abonesi var (mqtt, time) ve bu M4'te
// tek yuvadan küçük bir diziye çevrildi; konu/filtre gerektirmeyen, derleme
// anında belli bir abonelik için pub/sub hâlâ kazanç değil.
// ═══════════════════════════════════════════════════════════════════════════

#include "core/config_store.h"
#include "core/os_config.h"
#include "drivers/can/twai_driver.h"
#include "drivers/gnss/nmea_uart.h"
#include "services/can_service.h"
#include "services/gnss_service.h"
#include "services/command_service.h"
#include "services/mqtt_service.h"
#include "services/net_service.h"
#include "services/ota_service.h"
#include "services/position_service.h"
#include "services/power_service.h"
#include "services/wifi_scan_service.h"
#include "services/status_led.h"
#include "services/store_forward.h"
#include "services/telemetry_service.h"
#include "services/time_service.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "drive";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "─────────────────────────────────────────────");
    ESP_LOGI(TAG, "NarRTOS-%s  v%s  (code %d)",
             NARRTOS_PRODUCT, OS_VERSION_STR, OS_VERSION_CODE);
    ESP_LOGI(TAG, "─────────────────────────────────────────────");

    // 1. NVS — kalıcı ayar deposu.
    // Bölüm dolu ya da sürüm uyumsuzsa silip yeniden kur; Wear'da da aynı
    // kalıp. Bunu yapmazsak yeni firmware eski NVS düzeniyle açılamaz.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS yeniden kuruluyor (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "nvs: OK");

    // 2. Ayar deposu — IMEI, MQTT, APN. Komut servisinden önce gelmeli,
    //    çünkü ilk iş bu ayarları okuyup yazmak olacak.
    ESP_ERROR_CHECK(config_store_init());

    // 3. Komut servisi. Ekran ve buton olmadığı için cihazla konuşmanın TEK
    //    yolu bu — bilerek diğer servislerden önce başlıyor. Yanlış MQTT
    //    adresiyle açılan bir cihaza müdahale edebilmenin başka yolu yok.
    ESP_ERROR_CHECK(command_service_init());

    // 4. CAN toplayıcı. DONANIMDAN BAĞIMSIZ — tabloyu kurar ve komut
    //    arayüzünün `can inject` sink'ini kaydeder. Transceiver olmasa da
    //    çalışır; boru hattını donanımsız sınamanın yolu bu.
    ESP_ERROR_CHECK(can_service_init());

    // 5. TWAI sürücüsü. ENABLE_CAN=0 iken boş çevrilir ve
    //    ESP_ERR_NOT_SUPPORTED döner — hata değil, donanım yok demek.
    {
        uint16_t kbps = CAN_BITRATE_DEFAULT_KBPS;
        config_store_get_u16(NVS_KEY_CAN_KBPS, &kbps, CAN_BITRATE_DEFAULT_KBPS);
        esp_err_t cerr = can_twai_start(kbps);
        if (cerr == ESP_ERR_NOT_SUPPORTED)
            ESP_LOGI(TAG, "twai: derleme dışı (ENABLE_CAN=0) — `can inject` ile besleyin");
        else if (cerr != ESP_OK)
            ESP_LOGE(TAG, "twai başlatılamadı: %s", esp_err_to_name(cerr));
    }

    // 6. Store & forward tamponu. AĞDAN ÖNCE: bağlantı gelir gelmez
    //    boşaltılacak tampon hazır olmalı, ayrıca önceki açılıştan kalan
    //    kayıtlar burada sayılıyor.
    {
        esp_err_t serr = store_forward_init();
        if (serr != ESP_OK)
            ESP_LOGE(TAG, "store_forward başlatılamadı: %s — "
                          "kapsama dışı raporlar KAYBEDİLECEK",
                     esp_err_to_name(serr));
    }

    // 7. Ağ. Arka uç NVS'ten seçiliyor (`set net backend`); ayar eksikse
    //    yığın hiç ayağa kalkmaz, durum `net status`ta "ayarsız" görünür.
    ESP_ERROR_CHECK(net_service_init());

    // 8. MQTT. net_service'in durum bildirimine abone olur; broker adresi
    //    ayarlıysa ilk çevrimiçi olduğunda istemciyi başlatır.
    ESP_ERROR_CHECK(mqtt_service_init());

    // 9. Duvar saati. net_service'ten SONRA (ağ gelince SNTP başlatacak),
    //    telemetry'den ÖNCE (boot_id ve damga oradan okunuyor).
    ESP_ERROR_CHECK(time_service_init());

    // 9b. GNSS. time_service'ten SONRA: saatin ikinci kaynağı bu, şebeke
    //     olmadan da damga üretebilmenin yolu. Servis DONANIMDAN BAĞIMSIZ —
    //     modül yokken `gnss inject` ile beslenir (can_service kalıbı).
    ESP_ERROR_CHECK(gnss_service_init());
    {
        uint32_t baud = GNSS_BAUD_DEFAULT;
        config_store_get_u32(NVS_KEY_GNSS_BAUD, &baud, GNSS_BAUD_DEFAULT);
        esp_err_t gerr = gnss_uart_start(baud);
        if (gerr == ESP_ERR_NOT_SUPPORTED)
            ESP_LOGI(TAG, "gnss: derleme dışı (ENABLE_GNSS=0) — "
                          "`gnss inject` ile besleyin");
        else if (gerr != ESP_OK)
            ESP_LOGE(TAG, "gnss uart başlatılamadı: %s", esp_err_to_name(gerr));
    }

    // 9c. Konum birleştirme. gnss'ten SONRA (uydu kaynağı o), telemetry'den
    //     ÖNCE (rapor konumu buradan okuyor). Uydu kesildiğinde konumu araç
    //     hızı + jiroskopla tahmin ederek sürdürüyor (bkz. position_service.h).
    ESP_ERROR_CHECK(position_service_init());

    // 9d. WiFi tarama. position'dan SONRA (politika konum kaynağına
    //     bakıyor: uydu varken taranmaz), telemetry'den ÖNCE (liste rapora
    //     giriyor). Konumu cihaz çözmez; MAC listesi sunucuda çözülür.
    ESP_ERROR_CHECK(wifi_scan_service_init());

    // 10. Telemetri üretimi. Rapor ikili kayıt olarak tampona yazılıyor;
    //     JSON'a çevrilmesi gönderim anında oluyor (bkz. telemetry_codec.h).
    ESP_ERROR_CHECK(telemetry_service_init());

    // 10b. OTA. mqtt ve telemetry'den SONRA: geri alma doğrulaması yayın
    //      onayını mqtt'den, kurulum penceresini kontak durumundan okuyor.
    //      İlk işi bu açılışın bir OTA adayı olup olmadığına bakmak.
    ESP_ERROR_CHECK(ota_service_init());

    // 11. Durum LED'i. EN SONA bırakıldı: diğer servislerin durumunu
    //     yokluyor, hepsi ayakta olmalı. Ekransız cihazda kablo ve ağ
    //     yokken tek geri bildirim yolu bu.
    ESP_ERROR_CHECK(status_led_init());

    // Kalan servis: modem arka ucu (M7). Bayrağı açan derlemede
    // modem_backend.cpp'deki #error ile durur.

    // 12. Güç yönetimi. EN SON: uyku kararını diğer servislerin durumuyla
    //     veriyor (kontak, tampon, OTA) ve zamanlayıcı uyanışında hemen bir
    //     rapor ürettiriyor; hepsi ayakta olmalı. Varsayılan kapalı,
    //     `set power sleep 1` ile açılır.
    ESP_ERROR_CHECK(power_service_init());

    ESP_LOGI(TAG, "boot tamam — boş heap %lu bayt",
             (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "seri porttan 'help' yazarak komut listesini görebilirsiniz");

    // app_main dönebilir; komut servisi kendi görevinde çalışıyor. Boşa
    // dönen bir bekleme döngüsü tutmuyoruz — FreeRTOS zamanlayıcısı zaten
    // ayakta, idle görev güç tasarrufunu yönetiyor.
}
