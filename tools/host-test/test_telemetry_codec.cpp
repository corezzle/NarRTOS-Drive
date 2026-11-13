/**
 * @file test_telemetry_codec.cpp
 * @brief Telemetri dokümanı üreticisini Mac üzerinde sınar.
 *
 * GERÇEK telemetry_codec.cpp derleniyor. Modül saf olduğu için shim bile
 * gerekmiyor — donanım, ağ ve FreeRTOS bilmiyor.
 *
 * Bu testin neden en kritik test olduğu: gönderilen dokümanın biçimi bu
 * projede en zor geri alınabilen şey. Yanlış alan adı ya da yanlış sayı
 * biçimi sahadaki HER araçtan yanlış veri toplar ve sunucuda sessizce
 * birikir; fark edilmesi haftalar sürer, biriken veri de kurtarılamaz.
 *
 * Neyi kanıtlıyor:
 *   - alan adları ve tipleri sunucunun beklediğiyle birebir (altın çıktı)
 *   - CAN kimliği TAM 8 hex hane — sunucudaki CAN çözücü buna bağımlı
 *   - enlem/boylam 1e-7 hassasiyetinde, kayan nokta kullanılmadan
 *   - "bilinmiyor" sentinel'leri: fuel -1, gsmSignalQuality -1, konum 0,0
 *   - konum kaynağı ayrımı: uydu "gnss", ölü hesap "dr", konum yoksa "none"
 *   - v2 WiFi kuyruğu: mac+rssi listesi JSON'a doğru çıkıyor, boş liste
 *     alan üretmiyor, v1 kayıtlar (OTA öncesi tampondan) hâlâ okunuyor
 *   - zaman damgası çözümünün dört hâli (yaz / geriye hesapla / beklet / düşür)
 *   - bozuk ve eski sürüm kayıtların reddedilmesi
 *   - tampon taşmasında YARIM JSON üretilmemesi
 *
 * Çalıştırma:  tools/host-test/run.sh
 */

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

static void check_str(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) == 0) { g_pass++; printf("  ✓ %s\n", what); return; }
    g_fail++;
    printf("  ✗ %s\n", what);
    printf("      beklenen: %s\n", want);
    printf("      gelen   : %s\n", got);
}

/* 2026-01-01T00:00:00Z. Elle hesaplandı: 1577836800 (2020-01-01) + 2192 gün. */
static const int64_t EPOCH_2026 = 1767225600000LL;

/* ── Kayıt kurucu ───────────────────────────────────────────────────────── */
struct RecBuf {
    uint8_t bytes[TELEMETRY_HDR_SIZE + 64 * TELEMETRY_FRAME_SIZE +
                  1 + 10 * TELEMETRY_WIFI_SIZE];
    size_t  len = 0;
};

/** v2 kayıt: WiFi listesi de yazılabilir (wifi NULL / nw 0 = boş kuyruk). */
static void buildw(RecBuf& r, const telemetry_hdr_t& h,
                   const telemetry_frame_t* frames, unsigned n,
                   const telemetry_wifi_t* wifi, unsigned nw)
{
    telemetry_hdr_t hh = h;
    hh.magic       = TELEMETRY_REC_MAGIC;
    hh.ver         = TELEMETRY_REC_VER;
    hh.frame_count = (uint8_t)n;
    memcpy(r.bytes, &hh, sizeof(hh));
    for (unsigned i = 0; i < n; i++)
        memcpy(r.bytes + TELEMETRY_HDR_SIZE + i * TELEMETRY_FRAME_SIZE,
               &frames[i], TELEMETRY_FRAME_SIZE);
    uint8_t* wp = r.bytes + telemetry_rec_size_v1(n);
    wp[0] = (uint8_t)nw;
    for (unsigned i = 0; i < nw; i++)
        memcpy(wp + 1 + i * TELEMETRY_WIFI_SIZE, &wifi[i], TELEMETRY_WIFI_SIZE);
    r.len = telemetry_rec_size(n, nw);
}

static void build(RecBuf& r, const telemetry_hdr_t& h,
                  const telemetry_frame_t* frames, unsigned n)
{
    buildw(r, h, frames, n, nullptr, 0);
}

/** v1 kayıt: WiFi kuyruğu yok. OTA öncesi firmware'in tampona yazdığı biçim. */
static void build_v1(RecBuf& r, const telemetry_hdr_t& h,
                     const telemetry_frame_t* frames, unsigned n)
{
    buildw(r, h, frames, n, nullptr, 0);
    r.bytes[2] = 1;                       /* ver */
    r.len = telemetry_rec_size_v1(n);     /* kuyruk bayti yok */
}

/** Tipik bir "her şey biliniyor" kaydı. */
static telemetry_hdr_t full_hdr(void)
{
    telemetry_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.flags           = TELEMETRY_F_IGNITION | TELEMETRY_F_FIX;
    h.boot_id         = 0xAABBCCDD;
    h.uptime_ms       = 60000;
    h.wall_ms         = EPOCH_2026 + 123;
    h.lat_e7          = 410123456;      /* 41.0123456 */
    h.lon_e7          = 290123456;      /* 29.0123456 */
    h.speed_kmh_x10   = 543;            /* 54.3 */
    h.heading_deg_x10 = 1234;           /* 123.4 */
    h.gsm_csq         = TELEMETRY_CSQ_UNKNOWN;
    h.fuel_pct_x10    = TELEMETRY_FUEL_UNKNOWN;
    h.sats            = 0;
    h.pos_uncertainty_m = 5;            /* uydu konumu için tipik değer */
    return h;
}

static telemetry_frame_t frame(uint32_t id, uint8_t dlc,
                               std::initializer_list<uint8_t> bytes)
{
    telemetry_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id  = id;
    f.dlc = dlc;
    uint8_t i = 0;
    for (uint8_t b : bytes) { if (i < 8) f.data[i++] = b; }
    return f;
}

/* ── 1. Altın çıktı ────────────────────────────────────────────────────── */
static void test_golden(void)
{
    printf("\n[1] telemetri dokümanı — alan adları ve biçim (altın çıktı)\n");

    RecBuf r;
    telemetry_frame_t f[1] = {
        frame(0x18FEE500, 8, {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08})
    };
    build(r, full_hdr(), f, 1);

    char out[4096];
    int n = telemetry_encode_json(r.bytes, r.len, "351234567890123",
                                  0, 0, 0xAABBCCDD, out, sizeof(out));
    check(n > 0, "kodlandı");

    /* Alan adları sunucu sözleşmesinden; vehicleStatus,
     * idleDuration ve ignitionDuration SUNUCUDA hesaplanıyor, burada yok. */
    check_str(out,
        "{\"imei\":\"351234567890123\","
        "\"dateTime\":\"2026-01-01T00:00:00.123Z\","
        "\"latitude\":41.0123456,"
        "\"longitude\":29.0123456,"
        "\"speed\":54.3,"
        "\"ignitionState\":true,"
        "\"direction\":123.4,"
        "\"gsmSignalQuality\":-1,"
        "\"satelliteCount\":0,"
        "\"fuel\":-1.0,"
        "\"positionSource\":\"gnss\","
        "\"positionAccuracy\":5,"
        "\"can\":[\"0x18FEE500,01,02,03,04,05,06,07,08\"]}",
        "doküman birebir eşleşti");

    check((size_t)n == strlen(out), "dönen uzunluk dizeyle tutarlı");
}

/* ── 2. CAN kimliği tam 8 hane ─────────────────────────────────────────── */
static void test_can_id_width(void)
{
    printf("\n[2] CAN kimliği TAM 8 hex hane (sunucudaki CAN çözücü buna bağımlı)\n");

    /* Sunucudaki CAN çözücü kimliği sabit konumdan (3. karakterden, son 2
     * karakter hariç) yeniden kuruyor. Hane sayısı 8 değilse
     * yanlış PGN'e çözülür ve bu SESSİZ bir hata olur. */
    RecBuf r;
    telemetry_frame_t f[3] = {
        frame(0x18FEE500, 8, {0,0,0,0,0,0,0,0}),
        frame(0x123,      2, {0xAB, 0xCD}),       /* kısa standart kimlik */
        frame(0x00000000, 1, {0xFF}),
    };
    build(r, full_hdr(), f, 3);

    char out[4096];
    int n = telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out));
    check(n > 0, "kodlandı");

    std::string s(out);
    check(s.find("\"0x18FEE500,00,00,00,00,00,00,00,00\"") != std::string::npos,
          "29-bit kimlik 8 hane");
    check(s.find("\"0x00000123,AB,CD\"") != std::string::npos,
          "11-bit kimlik SIFIR DOLGULU 8 hane (0x123 değil)");
    check(s.find("\"0x00000000,FF\"") != std::string::npos,
          "sıfır kimlik 8 hane");
    check(s.find("\"0x123,") == std::string::npos,
          "kısa biçim hiç üretilmiyor");
}

/* ── 3. Sabit nokta biçimlendirme ──────────────────────────────────────── */
static void test_fixed_point(void)
{
    printf("\n[3] sabit nokta: kayan nokta kullanılmadan 1e-7 hassasiyet\n");

    struct { int32_t lat; int32_t lon; const char* wlat; const char* wlon; } c[] = {
        { 410123456,  290123456,  "41.0123456",  "29.0123456"  },
        { -410123456, -290123456, "-41.0123456", "-29.0123456" },  /* güney/batı */
        { 1,          -1,         "0.0000001",   "-0.0000001"  },  /* en küçük adım */
        { 900000000,  1800000000, "90.0000000",  "180.0000000" },  /* uçlar */
        { 410000000,  0,          "41.0000000",  "0.0000000"   },
    };

    bool hepsi = true;
    for (auto& t : c) {
        telemetry_hdr_t h = full_hdr();
        h.lat_e7 = t.lat;
        h.lon_e7 = t.lon;
        RecBuf r;
        build(r, h, nullptr, 0);

        char out[1024];
        if (telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out)) <= 0) { hepsi = false; break; }

        std::string s(out);
        std::string wl = std::string("\"latitude\":") + t.wlat + ",";
        std::string wo = std::string("\"longitude\":") + t.wlon + ",";
        if (s.find(wl) == std::string::npos || s.find(wo) == std::string::npos) {
            printf("      %s / %s bulunamadı içinde: %s\n", t.wlat, t.wlon, out);
            hepsi = false;
            break;
        }
    }
    check(hepsi, "enlem/boylam: negatif, en küçük adım ve uç değerler doğru");

    /* Hız ve yön tek ondalık. */
    telemetry_hdr_t h = full_hdr();
    h.speed_kmh_x10   = 0;
    h.heading_deg_x10 = 3599;
    RecBuf r;
    build(r, h, nullptr, 0);
    char out[1024];
    telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD, out, sizeof(out));
    std::string s(out);
    check(s.find("\"speed\":0.0,") != std::string::npos, "hız 0 -> \"0.0\"");
    check(s.find("\"direction\":359.9,") != std::string::npos, "yön 359.9");
}

/* ── 4. Bilinmeyen değer sentinel'leri ─────────────────────────────────── */
static void test_sentinels(void)
{
    printf("\n[4] \"bilinmiyor\" sentinel'leri (sunucu koduyla eşleşmeli)\n");

    /* fuel ölçekli tutulduğu için ham -1 yazmak -0.1 üretirdi — sunucu bunu
     * geçerli bir yakıt seviyesi sanardı. */
    check(TELEMETRY_FUEL_UNKNOWN == -10, "fuel sentinel -10 (ölçekli)");

    telemetry_hdr_t h = full_hdr();
    h.flags &= (uint8_t)~TELEMETRY_F_FIX;    /* GNSS yok — M5'e kadarki hâl */
    h.lat_e7 = 410123456;                    /* dolu olsa bile gitmemeli */
    h.lon_e7 = 290123456;
    RecBuf r;
    build(r, h, nullptr, 0);

    char out[1024];
    telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD, out, sizeof(out));
    std::string s(out);

    /* Sunucu güzergâh sorgusunda latitude: {$ne: 0} filtreliyor — 0,0
     * "fix yok" demek ve o noktalar güzergâhtan doğal olarak eleniyor. */
    check(s.find("\"latitude\":0.0000000,") != std::string::npos,
          "fix yokken enlem 0 (eski konum sızmıyor)");
    check(s.find("\"longitude\":0.0000000,") != std::string::npos,
          "fix yokken boylam 0");
    check(s.find("\"fuel\":-1.0") != std::string::npos,
          "fuel -1.0 (JSON'da -1 ile aynı sayı)");
    check(s.find("\"gsmSignalQuality\":-1,") != std::string::npos,
          "gsmSignalQuality -1 (ölçeksiz)");
    check(s.find("\"positionSource\":\"none\"") != std::string::npos,
          "fix yokken positionSource \"none\"");
    check(s.find("\"positionAccuracy\":-1") != std::string::npos,
          "fix yokken positionAccuracy -1 (bilinmiyor)");
}

/* ── 4b. Konum kaynağı: uydu / tahmin ayrımı ───────────────────────────── */
static void test_position_source(void)
{
    printf("\n[4b] konum kaynağı: tahmin (ölü hesap) uydudan ayırt ediliyor\n");

    /* Tahmini konum uydu konumu gibi kaydedilirse güzergâh doğruluğu
     * sessizce bozulur; ayrımı taşıyan bayrak F_DR. */
    telemetry_hdr_t h = full_hdr();
    h.flags |= TELEMETRY_F_DR;
    h.pos_uncertainty_m = 137;               /* tahmin yürüdükçe büyüyen değer */
    RecBuf r;
    build(r, h, nullptr, 0);

    char out[1024];
    int n = telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out));
    check(n > 0, "kodlandı");
    std::string s(out);
    check(s.find("\"positionSource\":\"dr\"") != std::string::npos,
          "tahminde positionSource \"dr\"");
    check(s.find("\"positionAccuracy\":137") != std::string::npos,
          "belirsizlik metre olarak gidiyor");
    check(s.find("\"latitude\":41.0123456,") != std::string::npos,
          "tahmini konum yine de gönderiliyor (0,0 değil)");
}

/* ── 4c. WiFi kuyruğu (v2) ─────────────────────────────────────────────── */
static void test_wifi(void)
{
    printf("\n[4c] v2 WiFi kuyruğu: liste JSON'a çıkıyor, v1 hâlâ okunuyor\n");

    char out[2048];

    /* İki ağlı kayıt. Biçim wearos-backend geolocation.service.js'in
     * beklediğiyle birebir: [{"mac","rssi"}, ...]. */
    telemetry_wifi_t w[2] = {
        {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, -62},
        {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, -80},
    };
    RecBuf r;
    buildw(r, full_hdr(), nullptr, 0, w, 2);

    int n = telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out));
    check(n > 0, "kodlandı");
    std::string s(out);
    check(s.find("\"wifi\":[{\"mac\":\"AA:BB:CC:DD:EE:FF\",\"rssi\":-62},"
                 "{\"mac\":\"00:11:22:33:44:55\",\"rssi\":-80}]")
              != std::string::npos,
          "iki ağ, MAC iki nokta ayraçlı büyük hex, rssi negatif tam sayı");
    check(s.rfind("]}") == s.size() - 2, "wifi son alan, belge kapanıyor");

    /* Boş liste: alan hiç yazılmıyor (altın çıktı testi de buna dayanıyor). */
    RecBuf r0;
    build(r0, full_hdr(), nullptr, 0);
    check(telemetry_encode_json(r0.bytes, r0.len, "1", 0, 0, 0xAABBCCDD,
                                out, sizeof(out)) > 0 &&
          strstr(out, "wifi") == nullptr,
          "boş listede \"wifi\" alanı yok");

    /* v1 kayıt (OTA öncesi tampondan): kuyruksuz, hâlâ çözülüyor. */
    RecBuf r1;
    build_v1(r1, full_hdr(), nullptr, 0);
    check(telemetry_encode_json(r1.bytes, r1.len, "1", 0, 0, 0xAABBCCDD,
                                out, sizeof(out)) > 0 &&
          strstr(out, "wifi") == nullptr,
          "v1 kayıt okunuyor (geriye uyum)");

    /* Bozuk kuyruklar: sayaç sınırı ve kırpılmış girdi. */
    {
        RecBuf b;
        build(b, full_hdr(), nullptr, 0);
        b.bytes[telemetry_rec_size_v1(0)] = TELEMETRY_WIFI_MAX + 1;
        b.len = telemetry_rec_size(0, TELEMETRY_WIFI_MAX + 1);
        check(telemetry_encode_json(b.bytes, b.len, "1", 0, 0, 0xAABBCCDD,
                                    out, sizeof(out)) == TELEMETRY_ERR_BAD_RECORD,
              "sayaç sınırı aşan kuyruk reddediliyor");
    }
    {
        RecBuf b;
        buildw(b, full_hdr(), nullptr, 0, w, 2);
        b.len -= TELEMETRY_WIFI_SIZE;   /* sayaç 2 ama tek girdi var */
        check(telemetry_encode_json(b.bytes, b.len, "1", 0, 0, 0xAABBCCDD,
                                    out, sizeof(out)) == TELEMETRY_ERR_BAD_RECORD,
              "kırpılmış kuyruk reddediliyor");
    }
}

/* ── 5. Zaman damgası çözümü ───────────────────────────────────────────── */
static void test_timestamp(void)
{
    printf("\n[5] zaman damgası: dört hâl\n");
    char out[1024];

    /* (a) Kayıtta duvar saati var — en iyi hâl. */
    {
        RecBuf r;
        build(r, full_hdr(), nullptr, 0);
        int n = telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                                      out, sizeof(out));
        check(n > 0 && std::string(out).find(
                  "\"dateTime\":\"2026-01-01T00:00:00.123Z\"") != std::string::npos,
              "(a) kayıttaki damga doğrudan kullanıldı");
    }

    /* (b) Kayıt damgasız ama BU açılıştan; saat sonradan geldi.
     *     Kapalı otoparkta veri toplayıp açık havada şebekeye giren araç. */
    {
        telemetry_hdr_t h = full_hdr();
        h.wall_ms   = 0;
        h.uptime_ms = 60000;          /* boot'tan 60 sn sonra yakalandı */
        RecBuf r;
        build(r, h, nullptr, 0);

        /* Şu an boot'tan 20 dk sonra ve saat yeni öğrenildi. */
        int64_t now_up   = 1200000;
        int64_t now_wall = EPOCH_2026 + 1200000;   /* 2026-01-01T00:20:00Z */
        int n = telemetry_encode_json(r.bytes, r.len, "1", now_wall, now_up,
                                      0xAABBCCDD, out, sizeof(out));
        /* 20 dk - 60 sn = 19 dk önce → 00:01:00 */
        check(n > 0 && std::string(out).find(
                  "\"dateTime\":\"2026-01-01T00:01:00.000Z\"") != std::string::npos,
              "(b) damga monoton saatten GERİYE hesaplandı");
    }

    /* (c) Kayıt damgasız, bu açılıştan, saat hâlâ yok → BEKLET. */
    {
        telemetry_hdr_t h = full_hdr();
        h.wall_ms = 0;
        RecBuf r;
        build(r, h, nullptr, 0);
        int n = telemetry_encode_json(r.bytes, r.len, "1", 0, 120000,
                                      0xAABBCCDD, out, sizeof(out));
        check(n == TELEMETRY_ERR_NO_TIME,
              "(c) saat yokken NO_TIME — kayıt tamponda beklemeli");
    }

    /* (d) Kayıt damgasız ve ÖNCEKİ açılıştan → bir daha asla çözülemez.
     *     Monoton saat boot'ta sıfırlandığı için geriye hesap rastgele bir
     *     tarih üretirdi; beklemek de sonsuza kadar sürerdi. */
    {
        telemetry_hdr_t h = full_hdr();
        h.wall_ms = 0;
        h.boot_id = 0x11111111;
        RecBuf r;
        build(r, h, nullptr, 0);
        int n = telemetry_encode_json(r.bytes, r.len, "1",
                                      EPOCH_2026, 120000, 0xAABBCCDD,
                                      out, sizeof(out));
        check(n == TELEMETRY_ERR_STALE,
              "(d) önceki açılıştan damgasız kayıt STALE — düşürülmeli");
    }

    /* ISO biçimi: saat/dakika/saniye ve milisaniye dolgusu. */
    {
        char iso[32];
        telemetry_format_iso8601(EPOCH_2026 + 3661000 + 7, iso, sizeof(iso));
        check_str(iso, "2026-01-01T01:01:01.007Z", "ISO 8601 sıfır dolgulu");
    }
}

/* ── 6. Bozuk kayıtlar ─────────────────────────────────────────────────── */
static void test_bad_records(void)
{
    printf("\n[6] bozuk / tanınmayan kayıtlar reddediliyor\n");
    char out[1024];
    RecBuf r;
    build(r, full_hdr(), nullptr, 0);

    check(telemetry_encode_json(r.bytes, TELEMETRY_HDR_SIZE - 1, "1", 0, 0,
                                0xAABBCCDD, out, sizeof(out))
          == TELEMETRY_ERR_BAD_RECORD, "başlıktan kısa kayıt");

    { RecBuf b = r; b.bytes[0] ^= 0xFF;
      check(telemetry_encode_json(b.bytes, b.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out)) == TELEMETRY_ERR_BAD_RECORD,
            "magic tutmuyor"); }

    { RecBuf b = r; b.bytes[2] = TELEMETRY_REC_VER + 1;
      /* OTA sonrası tamponda kalan yeni/eski sürüm kayıt — bu yüzden ver var. */
      check(telemetry_encode_json(b.bytes, b.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out)) == TELEMETRY_ERR_BAD_RECORD,
            "tanınmayan kayıt sürümü"); }

    { RecBuf b = r;
      /* frame_count boyutla tutmuyor: yarım yazılmış ya da bozulmuş kayıt.
       * Kontrol olmasaydı kod tamponun dışını okurdu. */
      b.bytes[41] = 5;
      check(telemetry_encode_json(b.bytes, b.len, "1", 0, 0, 0xAABBCCDD,
                                  out, sizeof(out)) == TELEMETRY_ERR_BAD_RECORD,
            "frame sayısı kayıt boyutuyla tutmuyor"); }
}

/* ── 7. Tampon sınırları ───────────────────────────────────────────────── */
static void test_buffer_limits(void)
{
    printf("\n[7] tampon: yarım JSON üretilmiyor, en kötü hâl sığıyor\n");

    RecBuf r;
    telemetry_frame_t f[64];
    for (int i = 0; i < 64; i++)
        f[i] = frame(0x18FEE500u + (uint32_t)i, 8,
                     {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04});
    build(r, full_hdr(), f, 64);

    char big[4096];
    int n = telemetry_encode_json(r.bytes, r.len, "351234567890123",
                                  0, 0, 0xAABBCCDD, big, sizeof(big));
    check(n > 0, "64 frame'lik en kötü hâl kodlandı");
    /* mqtt_service'in JSON tamponu 3072 bayt — sığmalı. */
    check(n < 3072, "en kötü hâl mqtt_service tamponuna (3072) sığıyor");
    printf("      en kötü hâl: %d bayt\n", n);

    char small[64];
    memset(small, 'X', sizeof(small));
    int e = telemetry_encode_json(r.bytes, r.len, "351234567890123",
                                  0, 0, 0xAABBCCDD, small, sizeof(small));
    check(e == TELEMETRY_ERR_BUF_SMALL, "küçük tampon hata döndü");
    /* Yarım JSON göndermek sunucuda çözümlenemeyen, sebebi aylarca
     * bulunamayan bir kayıt üretirdi. */
    check(small[sizeof(small) - 1] == 'X' || e < 0,
          "yarım JSON geçerliymiş gibi dönmedi");
}

/* ── 8. IMEI süzme ve kenar durumlar ───────────────────────────────────── */
static void test_imei_and_edges(void)
{
    printf("\n[8] IMEI süzme, boş CAN listesi, kısa dlc\n");
    char out[2048];

    {
        RecBuf r;
        build(r, full_hdr(), nullptr, 0);
        /* NVS'e başka bir yoldan girmiş bozuk bir değer JSON'u bozmamalı. */
        int n = telemetry_encode_json(r.bytes, r.len, "35\"12}34xx567890123",
                                      0, 0, 0xAABBCCDD, out, sizeof(out));
        check(n > 0, "kodlandı");
        check(std::string(out).find("\"imei\":\"351234567890123\"")
                  != std::string::npos,
              "rakam olmayan karakterler süzüldü (JSON bozulmuyor)");
        check(std::string(out).find("\"can\":[]") != std::string::npos,
              "boş CAN listesi geçerli JSON");
    }

    {
        RecBuf r;
        telemetry_frame_t f[2] = {
            frame(0x18FEE500, 0, {}),            /* veri yok */
            frame(0x18FEE501, 3, {0xA1,0xB2,0xC3}),
        };
        build(r, full_hdr(), f, 2);
        telemetry_encode_json(r.bytes, r.len, "1", 0, 0, 0xAABBCCDD,
                              out, sizeof(out));
        std::string s(out);
        check(s.find("\"0x18FEE500\"") != std::string::npos,
              "dlc 0 -> yalnız kimlik");
        check(s.find("\"0x18FEE501,A1,B2,C3\"") != std::string::npos,
              "dlc 3 -> üç bayt");
    }
}

int main(void)
{
    printf("telemetry_codec testi (kayıt sürümü %d, başlık %d bayt)\n",
           TELEMETRY_REC_VER, TELEMETRY_HDR_SIZE);

    test_golden();
    test_can_id_width();
    test_fixed_point();
    test_sentinels();
    test_position_source();
    test_wifi();
    test_timestamp();
    test_bad_records();
    test_buffer_limits();
    test_imei_and_edges();

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
