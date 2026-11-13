#pragma once
/**
 * @brief GNSS servisi — konum ve İKİNCİ SAAT KAYNAĞI.
 *
 * İki iş yapıyor:
 *
 *  1. Telemetri dokümanının konum alanlarını dolduruyor: `latitude`, `longitude`,
 *     `speed`, `direction`, `satelliteCount`. M4'e kadar bunlar 0 gidiyordu.
 *  2. **Şebekeden bağımsız saat.** M4'te tek saat kaynağı SNTP'ydi, yani ağ
 *     yokken damga da yoktu ve raporlar tamponda bekliyordu. GNSS sabitlemesi
 *     olan bir araç şebeke hiç görmeden de doğru damga üretebiliyor. Kapalı
 *     otopark senaryosunun kalıcı çözümü bu.
 *
 * ── Servis DONANIMDAN BAĞIMSIZ ────────────────────────────────────────────
 *
 * can_service'teki kalıbın aynısı. İki kaynaktan besleniyor:
 *   - `drivers/gnss/nmea_uart` (ENABLE_GNSS=1 iken gerçek alıcı)
 *   - `gnss inject` komutu (modül yokken test)
 * İkisi de aynı kapıya gidiyor: gnss_service_ingest(). Enjekte edilen cümle
 * gerçek cümleyle aynı yoldan geçiyor.
 *
 * ── BAYAT SABİTLEME, SABİTLEME YOKLUĞUNDAN KÖTÜDÜR ────────────────────────
 *
 * Alıcı tünelde sinyali kaybettiğinde son bilinen konumu tutmaya devam etmek
 * cazip görünür ama yanlıştır: hareket eden araçta 10 dakikalık eski bir
 * nokta "şu anki konum" diye gönderilir ve sunucu bunu sorgulamaz —
 * `latitude: {$ne: 0}` filtresi onu memnuniyetle kabul eder. Sonuç, haritada
 * duran ama aslında yol alan bir araç.
 *
 * Bu yüzden sabitleme GNSS_FIX_MAX_AGE_MS'ten eskiyse geçersiz sayılıyor ve
 * rapora 0,0 gidiyor — sunucu o noktayı güzergâhtan zaten eliyor.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint16_t speed_kmh_x10;
    uint16_t course_deg_x10;
    uint8_t  sats;
    uint32_t age_ms;        /**< son geçerli sabitlemenin üstünden geçen süre */
} gnss_fix_t;

typedef struct {
    uint32_t rx_sentences;  /**< ayrıştırmaya girmiş cümle */
    uint32_t csum_errors;   /**< sağlama tutmadı — atıldı */
    uint32_t unsupported;   /**< GSV/VTG gibi ilgilenmediklerimiz */
    uint32_t format_errors;
    uint32_t fix_updates;   /**< geçerli konum güncellemesi */
    uint32_t time_sets;     /**< GNSS'ten saat kurulması */
    bool     has_fix;       /**< şu an geçerli VE taze sabitleme var mı */
    uint8_t  sats;
} gnss_stats_t;

/** Tabloyu kur ve `gnss inject` kapısını aç. time_service'ten SONRA. */
esp_err_t gnss_service_init(void);

/** Tek NMEA cümlesi işle. Hem UART görevinden hem komut arayüzünden çağrılır;
 *  içeride mutex ile korunur. */
void gnss_service_ingest(const char* sentence);

/** Geçerli VE taze sabitleme varsa doldurur.
 *  @return false ise çağıran konumu 0,0 göndermeli */
bool gnss_service_get_fix(gnss_fix_t* out);

void gnss_service_stats(gnss_stats_t* out);

#ifdef __cplusplus
}
#endif
