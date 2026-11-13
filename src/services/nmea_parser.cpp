/**
 * @file nmea_parser.cpp
 * @brief NMEA 0183 ayrıştırıcısı (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Boş alanlar normaldir.** Sabitleme yokken alıcı ",,,," gönderir.
 *    Boş alan hata değil, "bu bilgi yok" demek; ilgili `has_*` bayrağı
 *    kapalı kalır. Boşu 0 olarak okumak, sabitleme yokken aracı Gine
 *    Körfezi'ne (0,0) koymak olurdu.
 *
 *  - **Sağlama toplamı önce.** Cümlenin tek bir alanına bile bakmadan
 *    doğrulanıyor; tutmuyorsa `out`a hiç dokunulmuyor.
 *
 *  - **Kayan nokta yok.** Koordinat ddmm.mmmm biçiminde ve 1e-7 dereceye
 *    çevriliyor; bütün dönüşümler tam sayı, yuvarlama açıkça yazılmış.
 *    Host testi ile hedef bit bit aynı sonucu üretiyor.
 */

#include "nmea_parser.h"

#include <string.h>

/* ── Sağlama toplamı ────────────────────────────────────────────────────── */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool nmea_checksum_ok(const char* s)
{
    if (!s || s[0] != '$') return false;

    uint8_t sum = 0;
    size_t  i   = 1;
    for (; s[i] && s[i] != '*' && i < NMEA_SENTENCE_MAX; i++) {
        if (s[i] == '\r' || s[i] == '\n') return false;   /* '*' hiç gelmedi */
        sum ^= (uint8_t)s[i];
    }
    if (s[i] != '*') return false;

    int hi = hexval(s[i + 1]);
    int lo = hexval(s[i + 2]);
    if (hi < 0 || lo < 0) return false;

    return sum == (uint8_t)((hi << 4) | lo);
}

/* ── Sayı ayrıştırma: "123.45" -> mantis 12345, ondalık hane 2 ─────────── */

static bool parse_scaled(const char* s, int64_t* mant, int* scale)
{
    if (!s || !*s) return false;

    int64_t m   = 0;
    int     sc  = 0;
    bool    dot = false;
    bool    any = false;
    bool    neg = false;

    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }

    for (; *s; s++) {
        if (*s == '.') {
            if (dot) return false;
            dot = true;
            continue;
        }
        if (*s < '0' || *s > '9') return false;
        /* Taşma koruması: NMEA alanları kısa, 15 hane fazlasıyla yeter. */
        if (m > 999999999999999LL) return false;
        m = m * 10 + (*s - '0');
        if (dot) sc++;
        any = true;
    }
    if (!any) return false;

    *mant  = neg ? -m : m;
    *scale = sc;
    return true;
}

static int64_t pow10i(int n)
{
    int64_t p = 1;
    while (n-- > 0) p *= 10;
    return p;
}

/** Yuvarlayarak böl (pozitif bölen). */
static int64_t div_round(int64_t num, int64_t den)
{
    return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

/* ── Koordinat: ddmm.mmmm -> 1e-7 derece ───────────────────────────────── */

static bool parse_coord(const char* val, const char* hemi, int deg_digits,
                        int32_t* out_e7)
{
    int64_t mant;
    int     scale;
    if (!parse_scaled(val, &mant, &scale)) return false;
    if (mant < 0) return false;
    if (!hemi || !*hemi) return false;

    const int64_t P   = pow10i(scale);
    const int64_t deg = mant / (100 * P);
    const int64_t rem = mant % (100 * P);      /* dakika, P ile ölçekli */

    /* dakika/60 -> derece, sonra 1e-7'ye. Yuvarlama açıkça. */
    int64_t e7 = deg * 10000000LL + div_round(rem * 10000000LL, 60 * P);

    char h = hemi[0];
    if (h == 'S' || h == 'W') e7 = -e7;
    else if (h != 'N' && h != 'E') return false;

    const int64_t limit = (deg_digits == 2) ? 900000000LL : 1800000000LL;
    if (e7 > limit || e7 < -limit) return false;

    *out_e7 = (int32_t)e7;
    return true;
}

/* ── Tarih/saat ────────────────────────────────────────────────────────── */

/* Howard Hinnant'ın civil_from_days tersi — takvim aritmetiği için standart
 * tam sayı algoritması. Artık yıl/yüzyıl kurallarını doğru çözüyor. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + (int64_t)doe - 719468LL;
}

static bool two_digits(const char* s, int* out)
{
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return false;
    *out = (s[0] - '0') * 10 + (s[1] - '0');
    return true;
}

/** hhmmss[.sss] + ddmmyy -> epoch ms */
static bool parse_datetime(const char* tod, const char* date, int64_t* out_ms)
{
    if (!tod || strlen(tod) < 6) return false;
    if (!date || strlen(date) < 6) return false;

    int hh, mi, ss, dd, mo, yy;
    if (!two_digits(tod, &hh) || !two_digits(tod + 2, &mi) ||
        !two_digits(tod + 4, &ss)) return false;
    if (!two_digits(date, &dd) || !two_digits(date + 2, &mo) ||
        !two_digits(date + 4, &yy)) return false;

    if (hh > 23 || mi > 59 || ss > 60) return false;      /* 60: artık saniye */
    if (mo < 1 || mo > 12 || dd < 1 || dd > 31) return false;

    /* NMEA yılı İKİ HANELİ ve belirsiz. 2000-2099 varsayılıyor.
     *
     * "yy >= 80 ise 19xx" gibi bir pivot kurmadık: sahaya bugün çıkan bir
     * cihaza canlı alıcıdan 19xx gelmez, ama GPS hafta sayacı taşması olan
     * bozuk bir alıcı 19,6 yıl geçmişte bir tarih üretebilir. Pivot bunu
     * "makul" bir tarihe çevirip gizlerdi. Ayrıştırıcı dürüst kalıyor,
     * MAKULLÜK DENETİMİ saati kuran yerde (gnss_service) yapılıyor —
     * TIME_VALID_MIN_EPOCH_S'ten eski her damga reddediliyor. */
    const int year = 2000 + yy;

    int ms = 0;
    if (tod[6] == '.') {
        /* En çok 3 hane al, eksikse sağdan sıfır doldur. */
        int mult = 100;
        for (int i = 7; i < 10 && tod[i] >= '0' && tod[i] <= '9'; i++) {
            ms += (tod[i] - '0') * mult;
            mult /= 10;
        }
    }

    const int64_t days = days_from_civil(year, (unsigned)mo, (unsigned)dd);
    *out_ms = ((days * 86400LL) + hh * 3600LL + mi * 60LL + ss) * 1000LL + ms;
    return true;
}

/* ── Cümle ayrıştırma ──────────────────────────────────────────────────── */

#define NMEA_MAX_FIELDS 24

nmea_result_t nmea_parse(const char* sentence, nmea_fields_t* out)
{
    if (!sentence || !out) return NMEA_ERR_FORMAT;
    if (sentence[0] != '$') return NMEA_ERR_FORMAT;

    size_t len = strlen(sentence);
    if (len >= NMEA_SENTENCE_MAX) return NMEA_ERR_FORMAT;

    if (!nmea_checksum_ok(sentence)) return NMEA_ERR_CHECKSUM;

    /* Yerel kopya: alanları yerinde NUL'layacağız. */
    char buf[NMEA_SENTENCE_MAX];
    memcpy(buf, sentence, len + 1);

    /* '*' ve sonrasını at, satır sonlarını kırp. */
    char* star = strchr(buf, '*');
    if (star) *star = '\0';
    for (char* p = buf; *p; p++)
        if (*p == '\r' || *p == '\n') { *p = '\0'; break; }

    char*  f[NMEA_MAX_FIELDS] = {0};
    size_t nf = 0;
    f[nf++] = buf + 1;                    /* '$' atlandı */
    for (char* p = buf + 1; *p; p++) {
        if (*p == ',') {
            *p = '\0';
            if (nf < NMEA_MAX_FIELDS) f[nf++] = p + 1;
        }
    }
    if (nf < 2) return NMEA_ERR_FORMAT;

    /* Cümle tipi SON ÜÇ harf — konuşmacı kimliği ne olursa olsun. */
    const char* id = f[0];
    size_t      idl = strlen(id);
    if (idl < 3) return NMEA_ERR_FORMAT;
    const char* type = id + (idl - 3);

    if (strncmp(type, "RMC", 3) == 0) {
        if (nf < 10) return NMEA_ERR_FORMAT;

        /* Geçerlilik: 'A' = geçerli, 'V' = uyarı (sabitleme yok). */
        if (f[2][0] == 'A' || f[2][0] == 'V') {
            out->has_fix_flag = true;
            out->fix_valid    = (f[2][0] == 'A');
        }

        int32_t lat, lon;
        if (parse_coord(f[3], f[4], 2, &lat) &&
            parse_coord(f[5], f[6], 3, &lon)) {
            out->has_position = true;
            out->lat_e7 = lat;
            out->lon_e7 = lon;
        }

        int64_t m; int sc;
        if (parse_scaled(f[7], &m, &sc) && m >= 0) {
            /* RMC hızı KNOT verir. km/sa = knot × 1,852.
             * kmh_x10 = mant × 1852 / (100 × 10^scale), yuvarlanarak. */
            const int64_t den = 100 * pow10i(sc);
            int64_t kmh10 = div_round(m * 1852LL, den);
            if (kmh10 <= 65535) {
                out->has_speed      = true;
                out->speed_kmh_x10  = (uint16_t)kmh10;
            }
        }

        if (parse_scaled(f[8], &m, &sc) && m >= 0) {
            int64_t c10 = div_round(m * 10LL, pow10i(sc));
            if (c10 <= 3600) {
                out->has_course      = true;
                out->course_deg_x10  = (uint16_t)(c10 == 3600 ? 0 : c10);
            }
        }

        int64_t ms;
        if (parse_datetime(f[1], f[9], &ms)) {
            out->has_time = true;
            out->utc_ms   = ms;
        }
        return NMEA_OK;
    }

    if (strncmp(type, "GGA", 3) == 0) {
        if (nf < 8) return NMEA_ERR_FORMAT;

        int64_t m; int sc;
        if (parse_scaled(f[6], &m, &sc) && sc == 0) {
            out->has_fix_flag = true;
            out->fix_valid    = (m > 0);          /* 0 = sabitleme yok */
        }

        int32_t lat, lon;
        if (parse_coord(f[2], f[3], 2, &lat) &&
            parse_coord(f[4], f[5], 3, &lon)) {
            out->has_position = true;
            out->lat_e7 = lat;
            out->lon_e7 = lon;
        }

        if (parse_scaled(f[7], &m, &sc) && sc == 0 && m >= 0 && m <= 255) {
            out->has_sats = true;
            out->sats     = (uint8_t)m;
        }
        return NMEA_OK;
    }

    /* GSV, VTG, GSA, GLL… geçerli ama bize bir şey katmıyor. */
    return NMEA_ERR_UNSUPPORTED;
}
