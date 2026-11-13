/**
 * @file test_nmea_parser.cpp
 * @brief NMEA ayrıştırıcısını Mac üzerinde sınar.
 *
 * GERÇEK nmea_parser.cpp derleniyor; modül saf olduğu için shim gerekmiyor.
 * Zaman damgası kontrolünde telemetry_codec'in ISO biçimlendiricisi
 * kullanılıyor — elle epoch hesaplamak yerine bağımsız bir yoldan doğrulama.
 *
 * Neyi kanıtlıyor:
 *   - RMC ve GGA'dan telemetri dokümanının GNSS alanlarının doğru çıkması
 *   - koordinat dönüşümü (ddmm.mmmm -> 1e-7 derece) tam sayı aritmetiğiyle
 *   - knot -> km/sa dönüşümü
 *   - SAĞLAMA TOPLAMI tutmayan cümlenin TAMAMEN atılması
 *   - konuşmacı kimliğinden bağımsızlık ($GP/$GN/$GA/$BD)
 *   - sabitleme yokken alanların BOŞ kalması (0,0'a düşmemesi)
 *   - güney/batı yarımküre işaretleri
 *
 * Çalıştırma:  tools/host-test/run.sh
 */

#include "../../src/services/nmea_parser.h"
#include "../../src/services/telemetry_codec.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/** Gövdeye doğru sağlama toplamını ekler: "$GPRMC,..." -> "$GPRMC,...*6A".
 *  Elle sağlama yazmak testin kendisini hataya açık yapardı. */
static std::string mk(const std::string& body)
{
    uint8_t sum = 0;
    for (size_t i = 1; i < body.size(); i++) sum ^= (uint8_t)body[i];
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X", sum);
    return body + tail;
}

static nmea_fields_t parse(const std::string& s, nmea_result_t* r = nullptr)
{
    nmea_fields_t f;
    memset(&f, 0, sizeof(f));
    nmea_result_t res = nmea_parse(s.c_str(), &f);
    if (r) *r = res;
    return f;
}

/* Klasik NMEA örneği — 2026-08-14 12:35:19 UTC, Avusturya civarı. */
static const char* RMC_BODY =
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,140826,003.1,W";
static const char* GGA_BODY =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";

/* ── 1. RMC ────────────────────────────────────────────────────────────── */
static void test_rmc(void)
{
    printf("\n[1] RMC — konum, hız, rota, tarih/saat\n");
    nmea_result_t r;
    nmea_fields_t f = parse(mk(RMC_BODY), &r);

    check(r == NMEA_OK, "ayrıştırıldı");
    check(f.has_fix_flag && f.fix_valid, "durum 'A' -> sabitleme geçerli");

    /* 4807.038 N = 48° + 7.038' = 48.1173000° */
    check(f.has_position, "konum var");
    check(f.lat_e7 == 481173000, "enlem 48.1173000");
    /* 01131.000 E = 11° + 31.000' = 11.5166667° (yuvarlanmış) */
    check(f.lon_e7 == 115166667, "boylam 11.5166667 (dakika/60, yuvarlanmış)");

    /* 22.4 knot × 1,852 = 41,4848 km/sa -> 41.5 */
    check(f.has_speed && f.speed_kmh_x10 == 415, "hız 22.4 knot -> 41.5 km/sa");
    check(f.has_course && f.course_deg_x10 == 844, "rota 84.4");

    char iso[32];
    telemetry_format_iso8601(f.utc_ms, iso, sizeof(iso));
    check(f.has_time, "zaman var");
    check(strcmp(iso, "2026-08-14T12:35:19.000Z") == 0,
          "tarih+saat doğru çözüldü");
    if (strcmp(iso, "2026-08-14T12:35:19.000Z") != 0)
        printf("      gelen: %s\n", iso);
}

/* ── 2. GGA ────────────────────────────────────────────────────────────── */
static void test_gga(void)
{
    printf("\n[2] GGA — uydu sayısı ve sabitleme kalitesi\n");
    nmea_result_t r;
    nmea_fields_t f = parse(mk(GGA_BODY), &r);

    check(r == NMEA_OK, "ayrıştırıldı");
    check(f.has_sats && f.sats == 8, "uydu sayısı 8");
    check(f.has_fix_flag && f.fix_valid, "kalite 1 -> sabitleme var");
    check(f.has_position && f.lat_e7 == 481173000, "konum RMC ile aynı");
    check(!f.has_time, "GGA tarih taşımaz (yalnız saat) -> zaman yok");

    /* Kalite 0 = sabitleme yok. */
    std::string body = GGA_BODY;
    size_t p = body.find(",E,1,08,");
    body.replace(p, 8, ",E,0,00,");
    f = parse(mk(body), &r);
    check(f.has_fix_flag && !f.fix_valid, "kalite 0 -> sabitleme YOK");
    check(f.has_sats && f.sats == 0, "uydu 0");
}

/* ── 3. Sağlama toplamı ────────────────────────────────────────────────── */
static void test_checksum(void)
{
    printf("\n[3] sağlama toplamı — bozuk cümle TAMAMEN atılır\n");

    std::string good = mk(RMC_BODY);

    /* Tek karakter bozulması: 4807.038 -> 4907.038, yani ~111 km sıçrama.
     * Sağlama olmasaydı sunucu bunu geçerli bir konum sanardı. */
    std::string bad = good;
    bad[bad.find("4807.038")] = '4';
    bad[bad.find("4807.038") + 1] = '9';

    nmea_result_t r;
    nmea_fields_t f = parse(bad, &r);
    check(r == NMEA_ERR_CHECKSUM, "bozuk sağlama reddedildi");
    check(!f.has_position && !f.has_speed && !f.has_time,
          "hiçbir alan doldurulmadı (kısmi veri sızmıyor)");

    check(nmea_checksum_ok(good.c_str()), "sağlam cümle geçiyor");
    check(!nmea_checksum_ok("$GPRMC,123519,A"), "'*' yoksa reddedilir");
    check(!nmea_checksum_ok("$GPRMC,123519,A*ZZ"), "hex olmayan sağlama reddedilir");
}

/* ── 4. Konuşmacı kimliği ──────────────────────────────────────────────── */
static void test_talker_ids(void)
{
    printf("\n[4] konuşmacı kimliği — $GP/$GN/$GA/$BD hepsi kabul\n");

    const char* prefixes[] = { "GP", "GN", "GA", "GL", "BD" };
    bool hepsi = true;
    for (const char* p : prefixes) {
        std::string body = RMC_BODY;
        body[1] = p[0];
        body[2] = p[1];
        nmea_result_t r;
        nmea_fields_t f = parse(mk(body), &r);
        if (r != NMEA_OK || !f.has_position) { hepsi = false; break; }
    }
    check(hepsi, "beş önek de çözüldü (çok takımyıldızlı alıcılar $GN kullanır)");
}

/* ── 5. Sabitleme yokken ───────────────────────────────────────────────── */
static void test_no_fix(void)
{
    printf("\n[5] sabitleme yokken alanlar BOŞ kalır\n");

    /* Alıcı sabitleme öncesi böyle gönderir. */
    nmea_result_t r;
    nmea_fields_t f = parse(mk("$GPRMC,,V,,,,,,,,,,N"), &r);

    check(r == NMEA_OK, "boş alanlı cümle geçerli sayıldı");
    check(f.has_fix_flag && !f.fix_valid, "durum 'V' -> sabitleme yok");
    /* Kritik: boşu 0 okumak aracı Gine Körfezi'ne (0,0) koyardı ve sunucu
     * bunu geçerli bir konum sanardı. */
    check(!f.has_position, "konum YOK — 0,0'a düşülmedi");
    check(!f.has_speed && !f.has_course && !f.has_time,
          "hız/rota/zaman da yok");
}

/* ── 6. Yarımküre işaretleri ───────────────────────────────────────────── */
static void test_hemispheres(void)
{
    printf("\n[6] güney/batı -> negatif\n");

    nmea_result_t r;
    /* Aynı sayılar, S ve W. */
    std::string body = "$GPRMC,123519,A,4807.038,S,01131.000,W,000.0,000.0,140826,,";
    nmea_fields_t f = parse(mk(body), &r);
    check(r == NMEA_OK, "ayrıştırıldı");
    check(f.lat_e7 == -481173000, "güney enlem negatif");
    check(f.lon_e7 == -115166667, "batı boylam negatif");

    /* Geçersiz yarımküre harfi kabul edilmemeli. */
    body = "$GPRMC,123519,A,4807.038,X,01131.000,E,000.0,000.0,140826,,";
    f = parse(mk(body), &r);
    check(!f.has_position, "geçersiz yarımküre harfi -> konum yok");
}

/* ── 7. Hız ve rota dönüşümleri ────────────────────────────────────────── */
static void test_conversions(void)
{
    printf("\n[7] knot -> km/sa ve rota kenar durumları\n");

    struct { const char* kn; uint16_t kmh10; } c[] = {
        { "000.0", 0    },
        { "001.0", 19   },   /* 1,852 -> 1.9 */
        { "010.0", 185  },   /* 18,52  -> 18.5 */
        { "022.4", 415  },   /* 41,4848 -> 41.5 */
        { "054.0", 1000 },   /* 100,008 -> 100.0 */
    };
    bool hepsi = true;
    for (auto& t : c) {
        std::string body = std::string("$GPRMC,123519,A,4807.038,N,01131.000,E,")
                         + t.kn + ",084.4,140826,,";
        nmea_fields_t f = parse(mk(body));
        if (!f.has_speed || f.speed_kmh_x10 != t.kmh10) {
            printf("      %s knot -> %u (beklenen %u)\n", t.kn,
                   f.has_speed ? f.speed_kmh_x10 : 9999, t.kmh10);
            hepsi = false;
        }
    }
    check(hepsi, "knot -> km/sa dönüşümü doğru yuvarlanıyor");

    /* 360.0 derece = 0 derece; sunucuya 360 göndermek anlamsız. */
    nmea_fields_t f = parse(mk("$GPRMC,123519,A,4807.038,N,01131.000,E,000.0,360.0,140826,,"));
    check(f.has_course && f.course_deg_x10 == 0, "rota 360.0 -> 0.0");
}

/* ── 8. Hassasiyet ─────────────────────────────────────────────────────── */
static void test_precision(void)
{
    printf("\n[8] 1e-7 hassasiyet, kayan nokta kullanılmadan\n");

    /* Tam bölünen bir değer: 30.000000' = 0.5 derece. */
    nmea_fields_t f = parse(mk("$GPRMC,123519,A,4130.000000,N,02900.000000,E,000.0,000.0,140826,,"));
    check(f.lat_e7 == 415000000, "41°30.000000' -> 41.5000000 (tam)");
    check(f.lon_e7 == 290000000, "29°00.000000' -> 29.0000000");

    /* Ondalık hane sayısı değişebilir; sonuç aynı olmalı. */
    nmea_fields_t a = parse(mk("$GPRMC,123519,A,4130.5,N,02900.5,E,000.0,000.0,140826,,"));
    nmea_fields_t b = parse(mk("$GPRMC,123519,A,4130.500000,N,02900.500000,E,000.0,000.0,140826,,"));
    check(a.lat_e7 == b.lat_e7 && a.lon_e7 == b.lon_e7,
          "aynı değer farklı hane sayısıyla aynı sonucu veriyor");

    /* Kesirli saniye. */
    nmea_fields_t t = parse(mk("$GPRMC,123519.75,A,4807.038,N,01131.000,E,000.0,000.0,140826,,"));
    char iso[32];
    telemetry_format_iso8601(t.utc_ms, iso, sizeof(iso));
    check(strcmp(iso, "2026-08-14T12:35:19.750Z") == 0, "kesirli saniye 0.75 -> 750 ms");
}

/* ── 9. Reddedilenler ──────────────────────────────────────────────────── */
static void test_rejects(void)
{
    printf("\n[9] biçim hataları ve ilgilenmediğimiz cümleler\n");
    nmea_result_t r;

    parse("GPRMC,123519", &r);
    check(r == NMEA_ERR_FORMAT, "'$' ile başlamayan reddedilir");

    parse(mk("$GPGSV,3,1,11,03,03,111,00"), &r);
    check(r == NMEA_ERR_UNSUPPORTED, "GSV geçerli ama desteklenmiyor");

    parse(mk("$GPRMC,123519,A"), &r);
    check(r == NMEA_ERR_FORMAT, "alan sayısı yetersiz RMC reddedilir");

    std::string uzun = "$GPRMC," + std::string(NMEA_SENTENCE_MAX, 'x');
    parse(uzun, &r);
    check(r == NMEA_ERR_FORMAT, "azami uzunluğu aşan cümle reddedilir");

    nmea_fields_t f;
    memset(&f, 0, sizeof(f));
    check(nmea_parse(nullptr, &f) == NMEA_ERR_FORMAT, "NULL cümle reddedilir");
}

int main(void)
{
    printf("nmea_parser testi\n");

    test_rmc();
    test_gga();
    test_checksum();
    test_talker_ids();
    test_no_fix();
    test_hemispheres();
    test_conversions();
    test_precision();
    test_rejects();

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
