/**
 * @file at_client.cpp
 * @brief AT komut istemcisi ve yanıt ayrıştırıcıları (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Sonuç kodu yanıta karışmıyor.** `OK` satırını da `resp`e koysaydık her
 *    ayrıştırıcının onu elemesi gerekirdi; sonuç kodu dönüş değerinde.
 *
 *  - **Yankı eleniyor.** ATE0 ile kapatılabilir ama her modem kabul etmez ve
 *    açılışta bir süre yankı açık kalır. Gönderilen komutun aynısı gelirse
 *    atlıyoruz — yankıya güvenip ilk satırı atmak, yankı kapalıyken gerçek
 *    veriyi atmak olurdu.
 *
 *  - **Kayan nokta yok**, telemetry_codec ve nmea_parser'daki karar burada da.
 */

#include "at_client.h"

#include <stdlib.h>
#include <string.h>

/* ── Yardımcılar ────────────────────────────────────────────────────────── */

static void trim(char* s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static bool is_final_ok(const char* s)  { return strcmp(s, "OK") == 0; }

static bool is_final_err(const char* s, int* code)
{
    if (strcmp(s, "ERROR") == 0) { if (code) *code = -1; return true; }
    if (strncmp(s, "+CME ERROR:", 11) == 0) {
        if (code) *code = (int)strtol(s + 11, NULL, 10);
        return true;
    }
    if (strncmp(s, "+CMS ERROR:", 11) == 0) {
        if (code) *code = (int)strtol(s + 11, NULL, 10);
        return true;
    }
    /* "NO CARRIER" / "BUSY" / "NO ANSWER" — çevirme başarısızlıkları. */
    if (strcmp(s, "NO CARRIER") == 0 || strcmp(s, "BUSY") == 0 ||
        strcmp(s, "NO ANSWER") == 0) { if (code) *code = -2; return true; }
    return false;
}

/* ── Komut ──────────────────────────────────────────────────────────────── */

at_result_t at_command(const at_io_t* io, const char* cmd, uint32_t timeout_ms,
                       char* resp, size_t resp_cap, int* err_code)
{
    if (!io || !io->write_line || !io->read_line || !cmd) return AT_ERR_ARG;
    if (resp && resp_cap) resp[0] = '\0';
    if (err_code) *err_code = 0;

    if (!io->write_line(io->ctx, cmd)) return AT_ERR_IO;

    size_t used = 0;
    char   line[AT_LINE_MAX];

    /* Sonuç kodu gelene kadar oku. Satır sayısı sınırı, bozuk bir modemin
     * bizi sonsuza kadar meşgul etmesini önlüyor. */
    for (int i = 0; i < 64; i++) {
        if (!io->read_line(io->ctx, line, sizeof(line), timeout_ms))
            return AT_ERR_TIMEOUT;

        trim(line);
        if (line[0] == '\0') continue;                 /* boş satır */
        if (strcmp(line, cmd) == 0) continue;          /* yankı */

        if (is_final_ok(line)) return AT_OK;
        if (is_final_err(line, err_code)) return AT_ERR_ERROR;

        if (resp && resp_cap) {
            size_t l = strlen(line);
            if (used + l + 2 < resp_cap) {
                if (used) resp[used++] = '\n';
                memcpy(resp + used, line, l);
                used += l;
                resp[used] = '\0';
            }
        }
    }
    return AT_ERR_TIMEOUT;
}

/* ── Ayrıştırıcılar ─────────────────────────────────────────────────────── */

/** `+PREFIX:` satırını bulup değer kısmına işaret eder. */
static const char* after_prefix(const char* resp, const char* prefix)
{
    const char* p = strstr(resp, prefix);
    if (!p) return NULL;
    p += strlen(prefix);
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

bool at_parse_csq(const char* resp, int* rssi_index)
{
    if (!resp || !rssi_index) return false;
    const char* p = after_prefix(resp, "+CSQ:");
    if (!p) return false;

    char* end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    /* 99 = "bilinmiyor" (3GPP 27.007). Geçerli bir seviye gibi göndermek,
     * sunucuda çok güçlü bir sinyal olarak yorumlanabilirdi. */
    if (v == 99 || v < 0 || v > 31) return false;

    *rssi_index = (int)v;
    return true;
}

int at_csq_to_dbm(int rssi_index)
{
    if (rssi_index < 0)  rssi_index = 0;
    if (rssi_index > 31) rssi_index = 31;
    return -113 + 2 * rssi_index;
}

bool at_parse_imei(const char* resp, char* out, size_t cap)
{
    if (!resp || !out || cap < 16) return false;

    /* Bazı modemler `+CGSN: 351234567890123`, bazıları çıplak sayı döner. */
    const char* p = after_prefix(resp, "+CGSN:");
    if (!p) p = resp;

    while (*p && (*p < '0' || *p > '9')) p++;   /* ilk rakama git */

    size_t n = 0;
    while (p[n] >= '0' && p[n] <= '9' && n + 1 < cap) n++;
    if (n != 14 && n != 15) return false;       /* command_service ile aynı kural */

    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

bool at_parse_creg(const char* resp, int* stat)
{
    if (!resp || !stat) return false;
    /* Hem +CREG (2G/3G) hem +CGREG/+CEREG (paket/LTE) kabul. */
    const char* p = after_prefix(resp, "+CREG:");
    if (!p) p = after_prefix(resp, "+CGREG:");
    if (!p) p = after_prefix(resp, "+CEREG:");
    if (!p) return false;

    /* Biçim: <n>,<stat>[,...] — ilk alan bizim ayarımız, ikincisi durum. */
    const char* comma = strchr(p, ',');
    if (!comma) return false;
    comma++;
    while (*comma == ' ') comma++;

    char* end = NULL;
    long v = strtol(comma, &end, 10);
    if (end == comma || v < 0 || v > 10) return false;

    *stat = (int)v;
    return true;
}

bool at_creg_registered(int stat)
{
    /* 1 = yerel şebekeye kayıtlı, 5 = dolaşımda kayıtlı. Diğerleri
     * (0 aranmıyor, 2 aranıyor, 3 reddedildi, 4 bilinmiyor) kayıtlı değil. */
    return stat == 1 || stat == 5;
}

/* Howard Hinnant'ın algoritması — nmea_parser'dakiyle aynı. */
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

bool at_parse_cclk(const char* resp, int64_t* utc_ms)
{
    if (!resp || !utc_ms) return false;
    const char* p = after_prefix(resp, "+CCLK:");
    if (!p) return false;
    if (*p == '"') p++;

    /* yy/MM/dd,hh:mm:ss±zz  — zz ÇEYREK SAAT ve yerel saate göre. */
    if (strlen(p) < 17) return false;

    int yy, mo, dd, hh, mi, ss;
    if (!two_digits(p, &yy) || p[2] != '/') return false;
    if (!two_digits(p + 3, &mo) || p[5] != '/') return false;
    if (!two_digits(p + 6, &dd) || p[8] != ',') return false;
    if (!two_digits(p + 9, &hh) || p[11] != ':') return false;
    if (!two_digits(p + 12, &mi) || p[14] != ':') return false;
    if (!two_digits(p + 15, &ss)) return false;

    if (mo < 1 || mo > 12 || dd < 1 || dd > 31) return false;
    if (hh > 23 || mi > 59 || ss > 60) return false;

    int tz_q = 0;                      /* çeyrek saat */
    const char* z = p + 17;
    if (*z == '+' || *z == '-') {
        int sign = (*z == '-') ? -1 : 1;
        int q;
        if (!two_digits(z + 1, &q)) return false;
        if (q > 56) return false;      /* ±14 saat sınırı */
        tz_q = sign * q;
    }

    const int64_t days  = days_from_civil(2000 + yy, (unsigned)mo, (unsigned)dd);
    const int64_t local = (days * 86400LL) + hh * 3600LL + mi * 60LL + ss;
    /* YEREL saatten dilimi çıkararak UTC. Atlanırsa damga saatlerce kayar. */
    const int64_t utc   = local - (int64_t)tz_q * 15 * 60;

    *utc_ms = utc * 1000LL;
    return true;
}
