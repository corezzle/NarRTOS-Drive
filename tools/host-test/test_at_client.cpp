/**
 * @file test_at_client.cpp
 * @brief AT istemcisini ve yanıt ayrıştırıcılarını modemsiz sınar.
 *
 * GERÇEK at_client.cpp derleniyor. Taşıyıcı bir arayüzün arkasında olduğu
 * için senaryolu yanıtlar besleyerek tüm komut akışı sürülebiliyor —
 * store_forward'ın sahte flash'ıyla aynı fikir.
 *
 * Neyi kanıtlıyor:
 *   - sonuç kodunun (OK/ERROR/+CME ERROR) doğru tanınması
 *   - yankı ve boş satırların elenmesi
 *   - zaman aşımının hata olarak dönmesi
 *   - +CSQ 99'un "bilinmiyor" sayılması (geçerli sinyal gibi gitmemesi)
 *   - IMEI'nin iki farklı modem biçiminden de çıkması
 *   - +CCLK'de SAAT DİLİMİNİN çıkarılması — atlanırsa damga saatlerce kayar
 */

#include "../../src/services/at_client.h"
#include "../../src/services/telemetry_codec.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/* ── Senaryolu modem ────────────────────────────────────────────────────── */
struct FakeModem {
    std::vector<std::string> lines;   /* okunacak satırlar */
    size_t                   pos = 0;
    std::vector<std::string> written;
    bool                     write_fails = false;
};

static bool fake_write(void* ctx, const char* line)
{
    FakeModem* m = (FakeModem*)ctx;
    if (m->write_fails) return false;
    m->written.push_back(line);
    return true;
}

static bool fake_read(void* ctx, char* out, size_t cap, uint32_t timeout_ms)
{
    (void)timeout_ms;
    FakeModem* m = (FakeModem*)ctx;
    if (m->pos >= m->lines.size()) return false;   /* zaman aşımı */
    snprintf(out, cap, "%s", m->lines[m->pos++].c_str());
    return true;
}

static at_io_t io_for(FakeModem& m)
{
    at_io_t io;
    io.write_line = fake_write;
    io.read_line  = fake_read;
    io.ctx        = &m;
    return io;
}

/* ── 1. Komut akışı ────────────────────────────────────────────────────── */
static void test_command(void)
{
    printf("\n[1] komut akışı: sonuç kodu, yankı, boş satır\n");

    FakeModem m;
    /* Gerçek modem tipik olarak böyle konuşur: yankı, boş satır, veri, OK. */
    m.lines = { "AT+CSQ", "", "+CSQ: 17,99", "", "OK" };
    at_io_t io = io_for(m);

    char resp[AT_RESP_MAX];
    int err = 0;
    check(at_command(&io, "AT+CSQ", 1000, resp, sizeof(resp), &err) == AT_OK,
          "OK döndü");
    check(m.written.size() == 1 && m.written[0] == "AT+CSQ", "komut yazıldı");
    /* Sonuç kodu ve yankı yanıta karışmamalı. */
    check(strcmp(resp, "+CSQ: 17,99") == 0,
          "yanıt yalnız bilgi satırı (yankı/OK elendi)");

    /* Çok satırlı yanıt. */
    FakeModem m2;
    m2.lines = { "+CREG: 0,1", "+CSQ: 20,99", "OK" };
    at_io_t io2 = io_for(m2);
    at_command(&io2, "AT+X", 1000, resp, sizeof(resp), &err);
    check(strcmp(resp, "+CREG: 0,1\n+CSQ: 20,99") == 0,
          "çok satırlı yanıt satır sonuyla birleşti");
}

/* ── 2. Hata yolları ───────────────────────────────────────────────────── */
static void test_errors(void)
{
    printf("\n[2] hata yolları\n");
    char resp[AT_RESP_MAX];
    int  err = 0;

    { FakeModem m; m.lines = { "ERROR" }; at_io_t io = io_for(m);
      check(at_command(&io, "AT+X", 100, resp, sizeof(resp), &err) == AT_ERR_ERROR,
            "ERROR yakalandı"); }

    { FakeModem m; m.lines = { "+CME ERROR: 10" }; at_io_t io = io_for(m);
      at_result_t r = at_command(&io, "AT+X", 100, resp, sizeof(resp), &err);
      check(r == AT_ERR_ERROR && err == 10, "+CME ERROR kodu çıkarıldı (10=SIM yok)"); }

    { FakeModem m; m.lines = { "+CSQ: 1,1" };   /* sonuç kodu HİÇ gelmiyor */
      at_io_t io = io_for(m);
      check(at_command(&io, "AT+X", 100, resp, sizeof(resp), &err) == AT_ERR_TIMEOUT,
            "sonuç kodu gelmezse zaman aşımı"); }

    { FakeModem m; m.write_fails = true; at_io_t io = io_for(m);
      check(at_command(&io, "AT", 100, resp, sizeof(resp), &err) == AT_ERR_IO,
            "yazma hatası"); }

    { FakeModem m; m.lines = { "NO CARRIER" }; at_io_t io = io_for(m);
      check(at_command(&io, "ATD*99#", 100, resp, sizeof(resp), &err) == AT_ERR_ERROR,
            "NO CARRIER sonuç kodu sayılıyor (çevirme başarısız)"); }

    check(at_command(nullptr, "AT", 100, resp, sizeof(resp), &err) == AT_ERR_ARG,
          "NULL güvenli");
}

/* ── 3. Sinyal ─────────────────────────────────────────────────────────── */
static void test_csq(void)
{
    printf("\n[3] +CSQ — 99 'bilinmiyor' demek\n");
    int idx = 0;

    check(at_parse_csq("+CSQ: 17,99", &idx) && idx == 17, "17 çözüldü");
    check(at_csq_to_dbm(17) == -79, "17 -> -79 dBm");
    check(at_csq_to_dbm(0) == -113 && at_csq_to_dbm(31) == -51, "uç değerler");

    /* 3GPP 27.007: 99 = bilinmiyor/algılanamıyor. Geçerli seviye gibi
     * göndermek sunucuda çok güçlü sinyal olarak yorumlanabilirdi. */
    check(!at_parse_csq("+CSQ: 99,99", &idx), "99 reddedildi (bilinmiyor)");
    check(!at_parse_csq("+CSQ: 32,0", &idx), "aralık dışı reddedildi");
    check(!at_parse_csq("OK", &idx), "önek yoksa reddedilir");
}

/* ── 4. IMEI ───────────────────────────────────────────────────────────── */
static void test_imei(void)
{
    printf("\n[4] IMEI — iki modem biçimi de kabul\n");
    char imei[24];

    check(at_parse_imei("351234567890123", imei, sizeof(imei)) &&
          strcmp(imei, "351234567890123") == 0, "çıplak sayı");
    check(at_parse_imei("+CGSN: 351234567890123", imei, sizeof(imei)) &&
          strcmp(imei, "351234567890123") == 0, "+CGSN: önekli");
    check(at_parse_imei("35123456789012", imei, sizeof(imei)), "14 hane kabul");

    check(!at_parse_imei("12345", imei, sizeof(imei)), "kısa reddedildi");
    check(!at_parse_imei("OK", imei, sizeof(imei)), "rakam yok -> reddedildi");
}

/* ── 5. Şebeke kaydı ───────────────────────────────────────────────────── */
static void test_creg(void)
{
    printf("\n[5] şebeke kaydı\n");
    int stat = -1;

    check(at_parse_creg("+CREG: 0,1", &stat) && stat == 1, "+CREG 0,1");
    check(at_creg_registered(1), "1 = yerel şebeke -> kayıtlı");
    check(at_creg_registered(5), "5 = dolaşım -> kayıtlı");
    check(!at_creg_registered(2), "2 = aranıyor -> kayıtlı DEĞİL");
    check(!at_creg_registered(3), "3 = reddedildi -> kayıtlı DEĞİL");
    check(!at_creg_registered(0), "0 = aranmıyor -> kayıtlı DEĞİL");

    /* LTE modemleri +CEREG kullanır. */
    check(at_parse_creg("+CEREG: 2,5", &stat) && stat == 5, "+CEREG (LTE)");
    check(at_parse_creg("+CGREG: 0,1", &stat) && stat == 1, "+CGREG (paket)");
}

/* ── 6. Saat — dilim çıkarılmalı ───────────────────────────────────────── */
static void test_cclk(void)
{
    printf("\n[6] +CCLK — saat dilimi UTC'ye çevrilirken ÇIKARILIR\n");
    int64_t ms = 0;
    char iso[32];

    /* Türkiye: UTC+3 = 12 çeyrek saat. Yerel 13:22:33 -> UTC 10:22:33. */
    check(at_parse_cclk("+CCLK: \"26/08/14,13:22:33+12\"", &ms), "ayrıştırıldı");
    telemetry_format_iso8601(ms, iso, sizeof(iso));
    check(strcmp(iso, "2026-08-14T10:22:33.000Z") == 0,
          "UTC+3 yerel 13:22:33 -> UTC 10:22:33");
    if (strcmp(iso, "2026-08-14T10:22:33.000Z") != 0)
        printf("      gelen: %s\n", iso);

    /* Negatif dilim. UTC-5 = -20 çeyrek. Yerel 08:00 -> UTC 13:00. */
    check(at_parse_cclk("+CCLK: \"26/08/14,08:00:00-20\"", &ms), "negatif dilim");
    telemetry_format_iso8601(ms, iso, sizeof(iso));
    check(strcmp(iso, "2026-08-14T13:00:00.000Z") == 0,
          "UTC-5 yerel 08:00 -> UTC 13:00");

    /* Dilim alanı yoksa yerel = UTC varsayılıyor. */
    check(at_parse_cclk("+CCLK: \"26/08/14,10:22:33\"", &ms), "dilimsiz");
    telemetry_format_iso8601(ms, iso, sizeof(iso));
    check(strcmp(iso, "2026-08-14T10:22:33.000Z") == 0, "dilimsiz -> UTC");

    check(!at_parse_cclk("+CCLK: \"bozuk\"", &ms), "bozuk reddedildi");
    check(!at_parse_cclk("OK", &ms), "önek yoksa reddedilir");
}

int main(void)
{
    printf("at_client testi\n");

    test_command();
    test_errors();
    test_csq();
    test_imei();
    test_creg();
    test_cclk();

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
