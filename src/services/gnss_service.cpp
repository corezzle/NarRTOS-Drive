/**
 * @file gnss_service.cpp
 * @brief GNSS durum tutucusu ve saat besleyicisi (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Konum ve zaman AYRI yaşıyor.** RMC ikisini birlikte taşır ama GGA
 *    yalnız konum+uydu verir; ayrıca sabitleme kaybolduktan sonra da alıcı
 *    saati bir süre doğru tutar. İkisini tek "geçerlilik" bayrağına bağlamak,
 *    sabitleme yokken damga üretememek olurdu — oysa saat hâlâ iyi.
 *
 *  - **Saat yalnız GEREKİNCE kuruluyor.** Her RMC'de settimeofday çağırmak
 *    damgaları milisaniye düzeyinde zıplatır ve monoton saatle kurulan
 *    ilişkiyi bozar. Kural: saat geçersizse kur, geçerliyse ancak sapma
 *    GNSS_TIME_RESYNC_MS'i aşarsa kur.
 *
 *  - **Makullük denetimi burada.** nmea_parser iki haneli yılı olduğu gibi
 *    çeviriyor; bozuk bir alıcının ürettiği saçma tarih
 *    time_service_set_wall_ms() tarafından reddediliyor (2020 öncesi).
 *    Reddedilen her deneme sayılıyor ki sahada sessiz kalmasın.
 */

#include "gnss_service.h"
#include "../core/os_config.h"
#include "nmea_parser.h"
#include "time_service.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static const char* TAG = "gnss";

static SemaphoreHandle_t s_mutex = NULL;

/* Son bilinen konum — tazeliği s_fix_at ile ölçülüyor. */
static bool     s_have_fix = false;
static int32_t  s_lat_e7 = 0, s_lon_e7 = 0;
static uint16_t s_speed_x10 = 0, s_course_x10 = 0;
static uint8_t  s_sats = 0;
static int64_t  s_fix_at = 0;      /* monoton ms */

static gnss_stats_t s_st = {};

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

esp_err_t gnss_service_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_have_fix = false;
    memset(&s_st, 0, sizeof(s_st));

    ESP_LOGI(TAG, "init OK (sabitleme tazelik sınırı %d sn)",
             GNSS_FIX_MAX_AGE_MS / 1000);
    return ESP_OK;
}

/** Mutex alınmış olmalı. */
static void maybe_set_clock(int64_t gnss_utc_ms)
{
    const int64_t cur = time_service_now_ms();

    if (cur > 0) {
        int64_t diff = cur - gnss_utc_ms;
        if (diff < 0) diff = -diff;
        if (diff < GNSS_TIME_RESYNC_MS) return;   /* yeterince yakın */
        ESP_LOGW(TAG, "saat sapması %lld ms — GNSS'ten yeniden kuruluyor",
                 (long long)diff);
    }

    /* Makul olmayan tarih burada reddediliyor (bozuk alıcı / hafta taşması). */
    if (time_service_set_wall_ms(gnss_utc_ms, "gnss") == ESP_OK) {
        s_st.time_sets++;
    } else {
        ESP_LOGW(TAG, "GNSS saati makul değil, reddedildi");
    }
}

void gnss_service_ingest(const char* sentence)
{
    if (!sentence || !s_mutex) return;

    nmea_fields_t f;
    memset(&f, 0, sizeof(f));
    nmea_result_t r = nmea_parse(sentence, &f);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_st.rx_sentences++;

    switch (r) {
        case NMEA_ERR_CHECKSUM:    s_st.csum_errors++;   break;
        case NMEA_ERR_UNSUPPORTED: s_st.unsupported++;   break;
        case NMEA_ERR_FORMAT:      s_st.format_errors++; break;
        default: break;
    }
    if (r != NMEA_OK) { xSemaphoreGive(s_mutex); return; }

    /* ── Zaman: sabitlemeden BAĞIMSIZ ──
     * Alıcı sabitlemeyi kaybetse de saati bir süre doğru tutar; konumu
     * geçersiz sayarken damgayı da atmanın anlamı yok. */
    if (f.has_time) maybe_set_clock(f.utc_ms);

    /* ── Konum: yalnız sabitleme geçerliyken ── */
    bool fix_ok = f.has_fix_flag ? f.fix_valid : s_have_fix;

    if (fix_ok && f.has_position) {
        s_lat_e7 = f.lat_e7;
        s_lon_e7 = f.lon_e7;
        s_fix_at = now_ms();
        s_have_fix = true;
        s_st.fix_updates++;
    } else if (f.has_fix_flag && !f.fix_valid) {
        /* Alıcı açıkça "sabitleme yok" diyor. Eski konumu tutmuyoruz;
         * tazelik zaman aşımına bırakmak yerine hemen düşürmek doğru. */
        s_have_fix = false;
    }

    /* Hız ve rota yalnız sabitleme varken anlamlı; sabitleme yokken alıcı
     * genelde bunları boş bırakıyor zaten. */
    if (fix_ok) {
        if (f.has_speed)  s_speed_x10  = f.speed_kmh_x10;
        if (f.has_course) s_course_x10 = f.course_deg_x10;
    }
    /* Uydu sayısı sabitleme olmasa da bilgi: "3 uydu görüyor ama yetmiyor". */
    if (f.has_sats) s_sats = f.sats;

    xSemaphoreGive(s_mutex);
}

bool gnss_service_get_fix(gnss_fix_t* out)
{
    if (!out || !s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = false;
    if (s_have_fix) {
        int64_t age = now_ms() - s_fix_at;
        if (age <= GNSS_FIX_MAX_AGE_MS) {
            out->lat_e7         = s_lat_e7;
            out->lon_e7         = s_lon_e7;
            out->speed_kmh_x10  = s_speed_x10;
            out->course_deg_x10 = s_course_x10;
            out->sats           = s_sats;
            out->age_ms         = (uint32_t)age;
            ok = true;
        }
        /* Bayat: geçersiz sayılıyor ama s_have_fix'i düşürmüyoruz — yeni bir
         * cümle gelirse yaş sıfırlanıp yeniden geçerli olabilir. */
    }
    xSemaphoreGive(s_mutex);
    return ok;
}

void gnss_service_stats(gnss_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_mutex) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_st;
    out->sats = s_sats;
    out->has_fix = s_have_fix && (now_ms() - s_fix_at) <= GNSS_FIX_MAX_AGE_MS;
    xSemaphoreGive(s_mutex);
}
