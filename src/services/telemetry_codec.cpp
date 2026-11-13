/**
 * @file telemetry_codec.cpp
 * @brief Telemetri kaydı → telemetri JSON'u (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **printf'in %f ve %lld'si KULLANILMIYOR.** ESP-IDF varsayılan olarak
 *    newlib'in "nano" biçimlendiricisini kullanır; 64 bit tam sayı desteği
 *    orada kapalıdır ve kayan nokta çıktısı yuvarlamada platforma göre
 *    değişebilir. Enlem/boylam 1e-7 hassasiyetinde ve yanlış yuvarlama sahada
 *    metrelerce kayma demek. Bu yüzden sayılar tam sayı aritmetiğiyle elle
 *    biçimlendiriliyor — host testi ile hedef bit bit aynı çıktı üretiyor.
 *
 *  - **Tampon taşması sessiz kalmıyor.** jbuf taşarsa bayrak kalkıyor ve
 *    kodlayıcı hata dönüyor. Yarım bir JSON göndermek, sunucuda çözümlenemeyen
 *    ve sebebi aylarca bulunamayan bir kayıt üretirdi.
 *
 *  - **IMEI'yi kaçış (escape) yapmıyoruz, SÜZÜYORUZ.** Komut arayüzü zaten
 *    yalnız rakam kabul ediyor; yine de buradan yalnız rakamlar geçiyor ki
 *    NVS'e başka bir yoldan girmiş bir değer JSON'u bozamasın.
 */

#include "telemetry_codec.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Kayıt biçimi flash'a yazılıyor ve OTA'dan sonra da okunacak: derleyici
 * hizalama yüzünden boyutu değiştirirse eski kayıtlar sessizce yanlış
 * çözülürdü. Burada derlemede duruyoruz. */
static_assert(sizeof(telemetry_hdr_t) == TELEMETRY_HDR_SIZE,
              "telemetry_hdr_t 44 bayt olmalı — TELEMETRY_REC_VER artırılmalı mı?");
static_assert(sizeof(telemetry_frame_t) == TELEMETRY_FRAME_SIZE,
              "telemetry_frame_t 13 bayt olmalı");

/* ── Tam sayı biçimlendirme ─────────────────────────────────────────────── */

static int u64_to_str(char* buf, uint64_t v)
{
    char tmp[24];
    int  n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

/** Ölçekli tam sayıyı ondalık yazar: (410123456, 7) → "41.0123456" */
static int fmt_fixed(char* out, int64_t value, int decimals)
{
    int      p = 0;
    uint64_t mag;
    if (value < 0) {
        out[p++] = '-';
        /* -INT64_MIN taşmasını önlemek için iki adımda. */
        mag = (uint64_t)(-(value + 1)) + 1;
    } else {
        mag = (uint64_t)value;
    }

    uint64_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;

    p += u64_to_str(out + p, mag / scale);
    if (decimals > 0) {
        out[p++] = '.';
        uint64_t frac = mag % scale;
        for (uint64_t s = scale / 10; s > 0; s /= 10)
            out[p++] = (char)('0' + (frac / s) % 10);
    }
    out[p] = '\0';
    return p;
}

void telemetry_format_iso8601(int64_t utc_ms, char* out, size_t cap)
{
    if (!out || cap < 25) { if (out && cap) out[0] = '\0'; return; }

    time_t    secs = (time_t)(utc_ms / 1000);
    int       ms   = (int)(utc_ms % 1000);
    struct tm tmv;
    gmtime_r(&secs, &tmv);

    /* Yalnız int argümanlar — nano printf'te güvenli. */
    snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
}

/* ── JSON tamponu ───────────────────────────────────────────────────────── */

typedef struct {
    char*  p;
    size_t cap;
    size_t n;
    bool   ovf;
} jbuf_t;

static void jputc(jbuf_t* j, char c)
{
    if (j->n + 1 >= j->cap) { j->ovf = true; return; }
    j->p[j->n++] = c;
}

static void jputs(jbuf_t* j, const char* s)
{
    while (*s) jputc(j, *s++);
}

static void jputfixed(jbuf_t* j, int64_t v, int decimals)
{
    char tmp[32];
    fmt_fixed(tmp, v, decimals);
    jputs(j, tmp);
}

static void jputint(jbuf_t* j, int64_t v) { jputfixed(j, v, 0); }

static void jputhex2(jbuf_t* j, uint8_t v)
{
    static const char H[] = "0123456789ABCDEF";
    jputc(j, H[(v >> 4) & 0xF]);
    jputc(j, H[v & 0xF]);
}

/** Kimliği "0x" + TAM 8 hex hane olarak yazar.
 *  Sunucu tarafı buna bağımlı: CAN çözücü kimliği sabit konumdan
 *  (3. karakterden, son 2 karakter hariç) yeniden kuruyor. Hane sayısı
 *  8'den farklı olursa yanlış PGN'e çözülür — sessizce yanlış veri. */
static void jput_can_id(jbuf_t* j, uint32_t id)
{
    static const char H[] = "0123456789ABCDEF";
    jputs(j, "0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        jputc(j, H[(id >> shift) & 0xF]);
}

/* ── Kodlayıcı ──────────────────────────────────────────────────────────── */

int telemetry_encode_json(const void* rec, size_t rec_len, const char* imei,
                          int64_t now_wall_ms, int64_t now_uptime_ms,
                          uint32_t boot_id, char* out, size_t cap)
{
    if (!rec || !out || cap == 0) return TELEMETRY_ERR_BUF_SMALL;
    if (rec_len < TELEMETRY_HDR_SIZE) return TELEMETRY_ERR_BAD_RECORD;

    telemetry_hdr_t h;
    memcpy(&h, rec, sizeof(h));

    if (h.magic != TELEMETRY_REC_MAGIC) return TELEMETRY_ERR_BAD_RECORD;
    if (h.ver < TELEMETRY_REC_VER_MIN || h.ver > TELEMETRY_REC_VER)
        return TELEMETRY_ERR_BAD_RECORD;

    /* v1: kayıt frame'lerle biter. v2: 1 bayt WiFi sayacı + girdiler.
     * Boyut her iki sürümde de TAM tutmalı; kuyruğu kırpılmış kayıt bozuk
     * kayıttır, "eldeki kadarını gönder" yapılmaz. */
    uint8_t        wifi_count = 0;
    const uint8_t* wifi_p     = NULL;
    const size_t   base       = telemetry_rec_size_v1(h.frame_count);
    if (h.ver == 1) {
        if (rec_len != base) return TELEMETRY_ERR_BAD_RECORD;
    } else {
        if (rec_len < base + 1) return TELEMETRY_ERR_BAD_RECORD;
        wifi_count = ((const uint8_t*)rec)[base];
        if (wifi_count > TELEMETRY_WIFI_MAX) return TELEMETRY_ERR_BAD_RECORD;
        if (rec_len != base + 1 + (size_t)wifi_count * TELEMETRY_WIFI_SIZE)
            return TELEMETRY_ERR_BAD_RECORD;
        wifi_p = (const uint8_t*)rec + base + 1;
    }

    /* ── Damga çözümü ── */
    int64_t stamp_ms;
    if (h.wall_ms > 0) {
        stamp_ms = h.wall_ms;
    } else if (h.boot_id != boot_id) {
        /* Önceki açılıştan kalmış, damgasız kayıt. Monoton saati bu oturumda
         * anlamsız — geriye hesaplasak rastgele bir tarih üretirdik. */
        return TELEMETRY_ERR_STALE;
    } else if (now_wall_ms <= 0) {
        /* Saat henüz yok ama kayıt bu oturumdan: saat gelince çözülecek. */
        return TELEMETRY_ERR_NO_TIME;
    } else {
        stamp_ms = now_wall_ms - (now_uptime_ms - h.uptime_ms);
    }

    char iso[32];
    telemetry_format_iso8601(stamp_ms, iso, sizeof(iso));

    jbuf_t j = { out, cap, 0, false };

    jputs(&j, "{\"imei\":\"");
    for (const char* p = imei ? imei : ""; *p; p++)
        if (*p >= '0' && *p <= '9') jputc(&j, *p);   /* bkz. dosya başı */

    jputs(&j, "\",\"dateTime\":\"");
    jputs(&j, iso);

    /* Fix yoksa 0,0 — sunucu güzergâh sorgularında {$ne: 0} ile eliyor. */
    bool fix = (h.flags & TELEMETRY_F_FIX) != 0;
    jputs(&j, "\",\"latitude\":");
    jputfixed(&j, fix ? h.lat_e7 : 0, 7);
    jputs(&j, ",\"longitude\":");
    jputfixed(&j, fix ? h.lon_e7 : 0, 7);

    jputs(&j, ",\"speed\":");
    jputfixed(&j, h.speed_kmh_x10, 1);

    jputs(&j, ",\"ignitionState\":");
    jputs(&j, (h.flags & TELEMETRY_F_IGNITION) ? "true" : "false");

    jputs(&j, ",\"direction\":");
    jputfixed(&j, h.heading_deg_x10, 1);

    jputs(&j, ",\"gsmSignalQuality\":");
    jputint(&j, h.gsm_csq);

    jputs(&j, ",\"satelliteCount\":");
    jputint(&j, h.sats);

    jputs(&j, ",\"fuel\":");
    jputfixed(&j, h.fuel_pct_x10, 1);

    /* Konumun ölçülmüş mü tahmin mi olduğu. Telemetri dokümanında bu alanlar
     * HENÜZ YOK; sunucu tarafıyla mutabakat gerekiyor. Eklenmemesi daha
     * riskli olurdu: tahmini konum uydu konumundan ayırt edilemezdi. */
    jputs(&j, ",\"positionSource\":\"");
    if (!fix)                        jputs(&j, "none");
    else if (h.flags & TELEMETRY_F_DR) jputs(&j, "dr");
    else                             jputs(&j, "gnss");
    jputc(&j, '"');

    jputs(&j, ",\"positionAccuracy\":");
    jputint(&j, fix ? (int64_t)h.pos_uncertainty_m : -1);

    /* CAN: ham frame dizeleri. Çözümü SUNUCU yapıyor (J1939 DBC). */
    jputs(&j, ",\"can\":[");
    const uint8_t* fp = (const uint8_t*)rec + TELEMETRY_HDR_SIZE;
    for (unsigned i = 0; i < h.frame_count; i++) {
        telemetry_frame_t f;
        memcpy(&f, fp + (size_t)i * TELEMETRY_FRAME_SIZE, sizeof(f));

        if (i) jputc(&j, ',');
        jputc(&j, '"');
        jput_can_id(&j, f.id);
        uint8_t dlc = (f.dlc > 8) ? 8 : f.dlc;
        for (uint8_t b = 0; b < dlc; b++) {
            jputc(&j, ',');
            jputhex2(&j, f.data[b]);
        }
        jputc(&j, '"');
    }
    jputc(&j, ']');

    /* WiFi listesi (v2, varsa). Liste boşsa alan hiç yazılmıyor: sunucu
     * "alan yok" ile "boş liste" ayrımıyla uğraşmasın. */
    if (wifi_count) {
        jputs(&j, ",\"wifi\":[");
        for (unsigned i = 0; i < wifi_count; i++) {
            telemetry_wifi_t w;
            memcpy(&w, wifi_p + (size_t)i * TELEMETRY_WIFI_SIZE, sizeof(w));

            if (i) jputc(&j, ',');
            jputs(&j, "{\"mac\":\"");
            for (int b = 0; b < 6; b++) {
                if (b) jputc(&j, ':');
                jputhex2(&j, w.mac[b]);
            }
            jputs(&j, "\",\"rssi\":");
            jputint(&j, w.rssi);
            jputc(&j, '}');
        }
        jputc(&j, ']');
    }
    jputc(&j, '}');

    if (j.ovf) return TELEMETRY_ERR_BUF_SMALL;
    j.p[j.n] = '\0';
    return (int)j.n;
}
