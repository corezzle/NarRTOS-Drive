#pragma once
/**
 * @brief NMEA 0183 cümle ayrıştırıcısı — saf, donanımsız, host'ta test edilir.
 *
 * telemetry_codec ile aynı disiplin: donanım, UART, FreeRTOS bilmiyor.
 * Sebebi aynı — buradan çıkan sayı doğrudan sunucuya gidiyor ve yanlış bir
 * koordinat sahada "araç ışınlandı" olarak görünür.
 *
 * ── Hangi cümleler ────────────────────────────────────────────────────────
 *
 *   RMC  konum, hız, rota, TARİH+SAAT, geçerlilik  → asıl cümle
 *   GGA  konum, sabitleme kalitesi, UYDU SAYISI
 *
 * İkisi birlikte telemetri dokümanının GNSS'ten gelen bütün alanlarını karşılıyor:
 * `latitude`, `longitude`, `speed`, `direction`, `satelliteCount`. RMC tek
 * başına yetmiyor (uydu sayısı yok), GGA tek başına yetmiyor (tarih yok —
 * damga üretemeyiz).
 *
 * ── Konuşmacı kimliği (talker id) esnek ───────────────────────────────────
 *
 * `$GPRMC` eski tek takımyıldızlı alıcılar, `$GNRMC` çok takımyıldızlı
 * (GPS+GLONASS+Galileo) alıcılar. `$GA`, `$GL`, `$BD` de var. Cümle tipi
 * SON ÜÇ harfle eşleştiriliyor; iki harfli önek neyse kabul ediliyor.
 * Yoksa alıcı değişince sessizce hiçbir konum çözülmezdi.
 *
 * ── Sağlama toplamı ZORUNLU ───────────────────────────────────────────────
 *
 * `*HH` XOR sağlaması tutmayan cümlenin HİÇBİR alanı doldurulmuyor. UART
 * hattında tek bir bozuk bayt "4103.7401" yerine "4103.9401" okutabilir —
 * 3 kilometrelik sıçrama. Sunucu tarafında bu, hız alarmı üreten ve güzergâhı
 * bozan bir hata olur; üstelik sunucunun bunu ayıklama şansı yok.
 *
 * ── Sayılar TAM SAYI aritmetiğiyle ────────────────────────────────────────
 *
 * Kayan nokta kullanılmıyor. NMEA koordinatı ddmm.mmmm biçiminde (derece +
 * ondalık DAKİKA); 1e-7 dereceye çevirirken float yuvarlaması platforma göre
 * değişebilir ve host testi ile hedef ayrışırdı. telemetry_codec'te alınan
 * kararın aynısı.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bir cümleden çıkarılabilenler. `has_*` bayrakları hangi alanın gerçekten
 *  geldiğini söyler — 0 ile "alan yok"u karıştırmamak için. */
typedef struct {
    bool     has_position;
    int32_t  lat_e7;
    int32_t  lon_e7;

    bool     has_speed;
    uint16_t speed_kmh_x10;   /**< RMC knot verir, km/sa'e çevrilir */

    bool     has_course;
    uint16_t course_deg_x10;

    bool     has_sats;
    uint8_t  sats;

    bool     has_time;
    int64_t  utc_ms;          /**< yalnız RMC (tarih+saat birlikte) */

    bool     has_fix_flag;
    bool     fix_valid;       /**< RMC 'A' ya da GGA kalite > 0 */
} nmea_fields_t;

typedef enum {
    NMEA_OK = 0,
    NMEA_ERR_FORMAT,      /**< '$' yok, çok uzun, alan sayısı yetersiz */
    NMEA_ERR_CHECKSUM,    /**< sağlama tutmadı — cümle TAMAMEN atıldı */
    NMEA_ERR_UNSUPPORTED, /**< geçerli ama ilgilenmediğimiz cümle (GSV, VTG…) */
} nmea_result_t;

/** Azami cümle uzunluğu (NMEA 0183 sınırı 82; pay bırakıldı). */
#define NMEA_SENTENCE_MAX 100

/**
 * @brief Tek bir NMEA cümlesini ayrıştırır.
 *
 * @param sentence '$' ile başlayan, satır sonu OLMADAN ya da olan cümle
 * @param out      yalnız NMEA_OK dönerse dokunulur; çağıran sıfırlamalı
 */
nmea_result_t nmea_parse(const char* sentence, nmea_fields_t* out);

/** Sağlama toplamını tek başına doğrula — teşhis için. */
bool nmea_checksum_ok(const char* sentence);

#ifdef __cplusplus
}
#endif
