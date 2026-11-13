/**
 * @file time_service.cpp
 * @brief Duvar saati ve boot kimliği (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Geçerlilik yıla bakarak belirleniyor**, ayrı bir "senkron oldu" bayrağı
 *    yerine. Sistem saati senkron öncesi 1970'ten başlar; 2020'den küçük her
 *    değer senkronsuz demek. Bayrak tutsaydık, saati SNTP dışında bir yol
 *    (ileride GNSS) kurduğunda bayrağı güncellemeyi unutmak sessiz bir hata
 *    olurdu — kaynak ne olursa olsun tek ölçüt sistem saatinin kendisi.
 *
 *  - **SNTP yalnız ağ ONLINE olunca başlatılıyor.** Ağsız cihazda SNTP
 *    görevini boşa çalıştırmanın anlamı yok; ayrıca esp_netif_sntp_init
 *    netif hazır değilken kurulursa ilk sorgular boşa gider.
 *
 *  - **Senkron GERİYE de sıçratabilir.** `smooth_sync` kapalı: araç saatinin
 *    yumuşakça kaymasını beklemek yerine bir anda doğru değere atlaması
 *    isteniyor, çünkü damgalar bu saatten üretiliyor ve yavaş yakınsama
 *    boyunca üretilen her rapor yanlış damgalanırdı.
 */

#include "time_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "net_service.h"

#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <string.h>
#include <sys/time.h>

static const char* TAG = "time";

static uint32_t    s_boot_id = 0;
static const char* s_source  = "yok";
static bool        s_sntp_up = false;
static char        s_ntp_host[64];

int64_t time_service_uptime_ms(void) { return esp_timer_get_time() / 1000; }

uint32_t time_service_boot_id(void) { return s_boot_id; }

const char* time_service_source(void) { return s_source; }

int64_t time_service_now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    if (tv.tv_sec < TIME_VALID_MIN_EPOCH_S) return 0;
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool time_service_valid(void) { return time_service_now_ms() > 0; }

esp_err_t time_service_set_wall_ms(int64_t utc_ms, const char* source)
{
    if (utc_ms / 1000 < TIME_VALID_MIN_EPOCH_S) return ESP_ERR_INVALID_ARG;

    struct timeval tv;
    tv.tv_sec  = (time_t)(utc_ms / 1000);
    tv.tv_usec = (suseconds_t)((utc_ms % 1000) * 1000);
    if (settimeofday(&tv, NULL) != 0) return ESP_FAIL;

    s_source = source ? source : "elle";
    ESP_LOGI(TAG, "saat kuruldu (kaynak: %s)", s_source);
    return ESP_OK;
}

/* SNTP senkron geri çağrısı — lwip'in SNTP görevinden gelir. */
static void on_sntp_sync(struct timeval* tv)
{
    (void)tv;
    s_source = "sntp";
    ESP_LOGI(TAG, "SNTP senkron oldu (%s)", s_ntp_host);
}

static void sntp_start_once(void)
{
    if (s_sntp_up) return;

    config_store_get_str(NVS_KEY_NTP, s_ntp_host, sizeof(s_ntp_host),
                         NTP_SERVER_DEFAULT);
    if (!s_ntp_host[0]) strlcpy(s_ntp_host, NTP_SERVER_DEFAULT, sizeof(s_ntp_host));

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp_host);
    cfg.start        = true;
    cfg.wait_for_sync = false;   /* bloklamıyoruz: ağ görevini tutmasın */
    cfg.sync_cb      = on_sntp_sync;
    cfg.smooth_sync  = false;    /* bkz. dosya başı */

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP kurulamadı: %s", esp_err_to_name(err));
        return;
    }
    s_sntp_up = true;
    ESP_LOGI(TAG, "SNTP başlatıldı (%s)", s_ntp_host);
}

static void on_net_state(net_state_t s, void* ctx)
{
    (void)ctx;
    /* Geri çağrı net görevinde; esp_netif_sntp_init kısa ve bloklamıyor. */
    if (s == NET_STATE_ONLINE) sntp_start_once();
}

esp_err_t time_service_init(void)
{
    /* boot_id asla 0 olmamalı: 0'ı "kimlik yok" olarak kullanan bir kayıt
     * yanlışlıkla bu oturuma ait sayılabilirdi. */
    do { s_boot_id = esp_random(); } while (s_boot_id == 0);

    esp_err_t err = net_service_add_state_cb(on_net_state, NULL);
    if (err != ESP_OK) return err;

    /* net_service bizden önce ONLINE'a geçmiş olabilir. */
    if (net_service_is_online()) sntp_start_once();

    /* Derin uykudan uyanışta RTC saati korur: settimeofday ile kurulan saat
     * uyku boyunca saymaya devam ediyor. Kaynak etiketi de bunu söylesin;
     * yoksa `time status` geçerli saate "yok" derdi. */
    if (time_service_valid()) s_source = "rtc (uykudan korundu)";

    ESP_LOGI(TAG, "init OK (boot_id %08lX, saat %s)",
             (unsigned long)s_boot_id,
             time_service_valid() ? "geçerli" : "HENÜZ YOK");
    return ESP_OK;
}
