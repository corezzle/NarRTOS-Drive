/**
 * @file telemetry_service.cpp
 * @brief Periyodik rapor üretimi (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Kayıt yığında değil statik tamponda kuruluyor.** 64 frame'lik kayıt
 *    WiFi listesiyle en çok 933 bayt; görev yığını 4 KB ve içinde ayrıca can_service anlık görüntüsü
 *    (64 × 24 bayt) var. İkisini de yığına koymak sınırı zorlardı.
 *
 *  - **Kontak her tikte okunuyor, periyot beklenmeden.** Kontak değişimi
 *    sunucu tarafındaki kontak raporunun tek girdisi; 10 dakikalık park
 *    periyodunu beklemek o raporu doğrudan yanlışlar.
 *
 *  - **Duvar saati biliniyorsa kayda YAZILIYOR.** Bilinmiyorsa 0 kalıyor ve
 *    damga gönderim anında monoton saatten hesaplanıyor. Bilinen değeri
 *    yazmak, cihaz yeniden başlarsa kaydın hâlâ damgalanabilir olmasını
 *    sağlıyor (boot_id değişince monoton hesap geçersizleşiyor).
 *
 *  - **Konum position_service'ten okunuyor, uydu alıcısından değil.** Uydu
 *    ve tahmin (ölü hesap) ayrımı orada yönetiliyor; burası yalnız kaynağı
 *    bayrağa çeviriyor. Tahmin edilen konum TELEMETRY_F_DR ile işaretleniyor
 *    ki sunucu ölçülmüş konumdan ayırt edebilsin.
 */

#include "telemetry_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "can_service.h"
#include "gnss_service.h"
#include "mqtt_service.h"
#include "position_service.h"
#include "telemetry_codec.h"
#include "time_service.h"
#include "wifi_scan_service.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "telem";

/* Yakalama tikinin çözünürlüğü. Kontak değişimini bu gecikmeyle görüyoruz. */
#define TELEMETRY_TICK_MS 500

static telemetry_stats_t s_st = {};
static int8_t   s_ign_sim  = -1;
static bool     s_ign      = false;
static int64_t  s_next_at  = 0;

/* Kayıt + anlık görüntü tamponları — bkz. dosya başı. Kuyrukta WiFi
 * bölümü de var (v2): 1 bayt sayaç + en fazla 8 ağ. */
static uint8_t     s_rec[TELEMETRY_HDR_SIZE +
                         CAN_FRAMES_PER_REPORT * TELEMETRY_FRAME_SIZE +
                         1 + TELEMETRY_WIFI_MAX * TELEMETRY_WIFI_SIZE];
static can_frame_t s_snap[CAN_FRAMES_PER_REPORT];

static bool read_ignition(void)
{
    if (s_ign_sim >= 0) return s_ign_sim != 0;
    return gpio_get_level((gpio_num_t)PIN_IGNITION) != 0;
}

esp_err_t telemetry_service_capture_now(void)
{
    size_t n = can_service_snapshot(s_snap, CAN_FRAMES_PER_REPORT);

    telemetry_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic   = TELEMETRY_REC_MAGIC;
    h.ver     = TELEMETRY_REC_VER;
    h.flags   = s_ign ? TELEMETRY_F_IGNITION : 0;
    h.boot_id = time_service_boot_id();
    h.uptime_ms = time_service_uptime_ms();
    /* Saat biliniyorsa kayda yaz: reboot sonrası da damgalanabilir kalsın. */
    h.wall_ms   = time_service_now_ms();

    /* Konum: uydu varsa uydudan, yoksa tahminden (position_service karar
     * veriyor, bkz. position_service.h). Tahmin edilen konum F_DR ile
     * işaretleniyor ve hata yarıçapı yazılıyor; sunucu ikisini ayırt
     * edemezse güzergâh doğruluğu sessizce bozulur. Konum hiç yoksa 0,0
     * gidiyor ve sunucu o noktayı güzergâhtan eliyor. */
    position_t pos;
    if (position_service_get(&pos)) {
        h.flags |= TELEMETRY_F_FIX;
        if (pos.source == POS_DR) h.flags |= TELEMETRY_F_DR;
        h.lat_e7            = pos.lat_e7;
        h.lon_e7            = pos.lon_e7;
        h.speed_kmh_x10     = pos.speed_kmh_x10;
        h.heading_deg_x10   = pos.heading_deg_x10;
        h.sats              = pos.sats;
        h.pos_uncertainty_m = pos.uncertainty_m;
    } else {
        h.lat_e7 = 0;
        h.lon_e7 = 0;
        h.speed_kmh_x10   = 0;
        h.heading_deg_x10 = 0;
        /* Uydu sayısı sabitleme olmasa da gönderiliyor: "kaç uydu görüyor
         * ama yetmiyor" saha teşhisinde işe yarıyor. */
        gnss_stats_t gs;
        gnss_service_stats(&gs);
        h.sats = gs.sats;
    }
    /* Modem yok (M7), yakıt kaynağı yok: sunucunun "bilinmiyor" sentinel'i.
     * fuel ölçekli olduğu için ham -1 DEĞİL (bkz. telemetry_codec.h). */
    h.gsm_csq      = TELEMETRY_CSQ_UNKNOWN;
    h.fuel_pct_x10 = TELEMETRY_FUEL_UNKNOWN;

    h.frame_count = (uint8_t)n;
    memcpy(s_rec, &h, sizeof(h));

    uint8_t* fp = s_rec + TELEMETRY_HDR_SIZE;
    for (size_t i = 0; i < n; i++) {
        telemetry_frame_t f;
        memset(&f, 0, sizeof(f));
        f.id  = s_snap[i].id;
        f.dlc = (s_snap[i].dlc > 8) ? 8 : s_snap[i].dlc;
        memcpy(f.data, s_snap[i].data, f.dlc);
        memcpy(fp + i * TELEMETRY_FRAME_SIZE, &f, sizeof(f));
    }

    /* WiFi kuyruğu (v2). Bayat liste rapora girmez: araç ilerlemişken iki
     * dakika önceki otoparkın ağlarını göndermek sunucuyu yanıltır. Liste
     * uydu varken de eklenebilir (taze kalmışsa); sunucu için ek kanıt. */
    wifi_scan_ap_t aps[TELEMETRY_WIFI_MAX];
    uint32_t age = UINT32_MAX;
    size_t wn = wifi_scan_service_get(aps, TELEMETRY_WIFI_MAX, &age);
    if (wn > 0 && age > WIFI_SCAN_MAX_AGE_MS) wn = 0;

    uint8_t* wp = fp + n * TELEMETRY_FRAME_SIZE;
    wp[0] = (uint8_t)wn;
    for (size_t i = 0; i < wn; i++) {
        telemetry_wifi_t w;
        memcpy(w.mac, aps[i].bssid, 6);
        w.rssi = aps[i].rssi;
        memcpy(wp + 1 + i * TELEMETRY_WIFI_SIZE, &w, sizeof(w));
    }

    esp_err_t err = mqtt_service_enqueue_report(
        s_rec, telemetry_rec_size((unsigned)n, (unsigned)wn));
    if (err == ESP_OK) {
        s_st.captured++;
        s_st.last_frames = (uint32_t)n;
    } else {
        s_st.push_failed++;
        ESP_LOGE(TAG, "rapor tampona yazılamadı: %s", esp_err_to_name(err));
    }
    return err;
}

/* mqtt_service ile telemetry_codec arasındaki köprü. Burada duruyor çünkü
 * ikisini de bilen tek yer burası: mqtt_service kayıt biçimini bilmiyor,
 * codec de MQTT'nin bekletme/düşürme sözleşmesini bilmiyor. */
int telemetry_render_for_mqtt(const void* rec, size_t len, char* out,
                              size_t cap, void* ctx)
{
    (void)ctx;
    char imei[24];
    config_store_get_str(NVS_KEY_IMEI, imei, sizeof(imei), "");

    int r = telemetry_encode_json(rec, len, imei,
                                  time_service_now_ms(),
                                  time_service_uptime_ms(),
                                  time_service_boot_id(), out, cap);
    if (r >= 0) return r;

    /* Saat henüz yok ama kayıt bu açılıştan: saat gelince çözülecek. */
    if (r == TELEMETRY_ERR_NO_TIME) return MQTT_RENDER_RETRY;
    /* Bozuk kayıt, tanınmayan sürüm ya da önceki açılıştan damgasız kayıt. */
    return MQTT_RENDER_DROP;
}

static uint32_t period_ms(void)
{
    return s_ign ? TELEMETRY_PERIOD_MS : TELEMETRY_PERIOD_PARKED_MS;
}

static void telemetry_task(void* arg)
{
    (void)arg;
    s_ign     = read_ignition();
    s_next_at = time_service_uptime_ms() + period_ms();

    for (;;) {
        int64_t now = time_service_uptime_ms();

        bool ign = read_ignition();
        if (ign != s_ign) {
            /* Kontak değişimi periyodu beklemez — filo kontak raporunun
             * tek girdisi bu. */
            s_ign = ign;
            ESP_LOGI(TAG, "kontak %s", ign ? "AÇILDI" : "kapandı");
            telemetry_service_capture_now();
            s_next_at = now + period_ms();
        } else if (now >= s_next_at) {
            telemetry_service_capture_now();
            s_next_at = now + period_ms();
        }

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_TICK_MS));
    }
}

void telemetry_service_set_ignition_sim(int8_t v)
{
    s_ign_sim = (v < 0) ? -1 : (v ? 1 : 0);
    ESP_LOGW(TAG, "kontak kaynağı: %s",
             (s_ign_sim < 0) ? "gerçek pin" : (s_ign_sim ? "elle AÇIK" : "elle kapalı"));
}

void telemetry_service_stats(telemetry_stats_t* out)
{
    if (!out) return;
    *out = s_st;
    out->ignition     = s_ign;
    out->ignition_sim = s_ign_sim;
    int64_t d = s_next_at - time_service_uptime_ms();
    out->next_in_ms = (d > 0) ? (uint32_t)d : 0;
}

esp_err_t telemetry_service_init(void)
{
    /* Aşağı çekme: transceiver/araç bağlı değilken pin havada kalır ve kontak
     * rastgele açılıp kapanmış görünürdü. Gerçek devrede harici bölücü
     * olacak; aşağı çekme onu bozmaz. */
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_IGNITION;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "kontak pini kurulamadı: %s", esp_err_to_name(err));
        return err;
    }

    /* Yük üreticisini MQTT'ye tak — tampondaki ikili kayıt buradan JSON'a
     * çevriliyor. */
    mqtt_service_set_render(telemetry_render_for_mqtt, NULL);

    BaseType_t r = xTaskCreatePinnedToCore(telemetry_task, "telem_svc", 4096,
                                           NULL, TELEMETRY_TASK_PRIORITY, NULL,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init OK (periyot %d sn / park %d sn, kontak pin %d)",
             TELEMETRY_PERIOD_MS / 1000, TELEMETRY_PERIOD_PARKED_MS / 1000,
             PIN_IGNITION);
    return ESP_OK;
}
