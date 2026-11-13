/**
 * @file test_ota_manifest.cpp
 * @brief OTA manifest ayrıştırıcısını Mac üzerinde sınar.
 *
 * GERÇEK ota_manifest.cpp derleniyor; modül saf, shim gerekmiyor.
 *
 * Neden bu test önemli: sürüm karşılaştırmasındaki bir hata ya cihazı hiç
 * güncellenmez yapar (fark edilmesi aylar sürer) ya da sonsuz güncelleme
 * döngüsüne sokar. İkisi de sahada, ULAŞILAMAYAN cihazlarda olur.
 *
 * Neyi kanıtlıyor:
 *   - manifest alanlarının doğru çıkması
 *   - zorunlu alan eksikse REDDEDİLMESİ (yarım manifest kurulum başlatmamalı)
 *   - `notes` serbest metninin diğer alanları kirletmemesi
 *   - `forced` alanının yokluğunda false kalması ("zorla" asla varsayılan olamaz)
 *   - eşit/eski sürüm koduyla güncelleme YAPILMAMASI
 */

#include "../../src/services/ota_manifest.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

static const char* FULL =
    "{\"version\":\"1.2.3\",\"version_code\":10203,"
    "\"url\":\"https://ota.ornek.com/drive/1.2.3.bin\","
    "\"size\":812345,\"notes\":\"CAN düzeltmesi\",\"forced\":false}";

/* ── 1. Tam manifest ───────────────────────────────────────────────────── */
static void test_full(void)
{
    printf("\n[1] tam manifest\n");
    ota_manifest_t m;
    check(ota_manifest_parse(FULL, &m) == OTA_MF_OK, "ayrıştırıldı");
    check(strcmp(m.version, "1.2.3") == 0, "version");
    check(m.version_code == 10203, "version_code");
    check(strcmp(m.url, "https://ota.ornek.com/drive/1.2.3.bin") == 0, "url");
    check(m.size == 812345, "size");
    check(strcmp(m.notes, "CAN düzeltmesi") == 0, "notes");
    check(m.forced == false, "forced false");
}

/* ── 2. Sürüm karşılaştırması ──────────────────────────────────────────── */
static void test_compare(void)
{
    printf("\n[2] sürüm karşılaştırması — yalnız KOD, dize değil\n");
    ota_manifest_t m;
    ota_manifest_parse(FULL, &m);   /* kod 10203 */

    check(ota_manifest_is_newer(&m, 10202), "10203 > 10202 -> güncelle");
    check(!ota_manifest_is_newer(&m, 10203), "eşit kod -> güncelleme YOK");
    /* Sunucunun yanlışlıkla eski sürüm yayınlaması filoyu geri düşürmemeli. */
    check(!ota_manifest_is_newer(&m, 10300), "sunucu ESKİ -> geri düşürme YOK");
    check(!ota_manifest_is_newer(nullptr, 0), "NULL güvenli");

    /* os_config.h'deki tuzak: "0.10.0" < "0.2.0" dize karşılaştırmasında.
     * Kod tabanlı olduğu için burada sorun çıkmıyor. */
    ota_manifest_t a, b;
    ota_manifest_parse("{\"version\":\"0.10.0\",\"version_code\":1000,\"url\":\"x\"}", &a);
    ota_manifest_parse("{\"version\":\"0.2.0\",\"version_code\":200,\"url\":\"x\"}", &b);
    check(a.version_code > b.version_code,
          "0.10.0 kodu 0.2.0'dan büyük (dize olsaydı ters olurdu)");
}

/* ── 3. Zorunlu alanlar ────────────────────────────────────────────────── */
static void test_required(void)
{
    printf("\n[3] zorunlu alan eksikse REDDEDİLİR\n");
    ota_manifest_t m;

    check(ota_manifest_parse("{\"version\":\"1.0\",\"url\":\"x\"}", &m)
          == OTA_MF_ERR_FIELDS, "version_code yok -> reddedildi");
    check(ota_manifest_parse("{\"version_code\":1,\"version\":\"1.0\"}", &m)
          == OTA_MF_ERR_FIELDS, "url yok -> reddedildi");
    check(ota_manifest_parse("{\"version_code\":1,\"url\":\"\"}", &m)
          == OTA_MF_ERR_FIELDS, "url boş -> reddedildi");
    check(ota_manifest_parse("bu json değil", &m) == OTA_MF_ERR_PARSE,
          "JSON olmayan -> reddedildi");
    check(ota_manifest_parse(nullptr, &m) == OTA_MF_ERR_ARG, "NULL güvenli");

    /* Sunucu hatası negatif kod gönderirse kurulum başlamamalı. */
    check(ota_manifest_parse("{\"version_code\":-5,\"url\":\"x\"}", &m)
          == OTA_MF_ERR_FIELDS, "negatif kod -> reddedildi");
}

/* ── 4. notes alanı diğerlerini kirletmemeli ───────────────────────────── */
static void test_notes_pollution(void)
{
    printf("\n[4] serbest metin diğer alanları kirletmiyor\n");

    /* `notes` içinde "url" kelimesi geçiyor. Naif bir strstr araması bunu
     * alan sanıp yanlış değeri okurdu. */
    const char* tricky =
        "{\"notes\":\"yeni url yapisi ve version_code duzeni\","
        "\"version_code\":500,\"url\":\"https://dogru/adres.bin\"}";

    ota_manifest_t m;
    check(ota_manifest_parse(tricky, &m) == OTA_MF_OK, "ayrıştırıldı");
    check(strcmp(m.url, "https://dogru/adres.bin") == 0,
          "url notes'taki kelimeden etkilenmedi");
    check(m.version_code == 500, "version_code notes'tan etkilenmedi");

    /* Alan sırası değişse de sonuç aynı olmalı. */
    const char* reordered =
        "{\"url\":\"https://a/b.bin\",\"forced\":true,\"version_code\":7}";
    check(ota_manifest_parse(reordered, &m) == OTA_MF_OK, "farklı sıra");
    check(m.version_code == 7 && m.forced, "sıra bağımsız çözüldü");
}

/* ── 5. forced ─────────────────────────────────────────────────────────── */
static void test_forced(void)
{
    printf("\n[5] forced — yokluğunda ASLA true olmamalı\n");
    ota_manifest_t m;

    ota_manifest_parse("{\"version_code\":1,\"url\":\"x\"}", &m);
    check(m.forced == false, "alan yok -> false");

    ota_manifest_parse("{\"version_code\":1,\"url\":\"x\",\"forced\":true}", &m);
    check(m.forced == true, "forced true");

    /* "forced":"true" (dize) boolean değil — kabul edilmemeli, çünkü
     * forced park penceresini atlıyor ve yol ortasında yeniden başlatıyor. */
    ota_manifest_parse("{\"version_code\":1,\"url\":\"x\",\"forced\":\"true\"}", &m);
    check(m.forced == false, "dize \"true\" boolean sayılmıyor");
}

/* ── 6. Sınırlar ───────────────────────────────────────────────────────── */
static void test_limits(void)
{
    printf("\n[6] sınırlar\n");
    ota_manifest_t m;

    /* url alanına sığmayan değer kırpılmamalı — kırpılmış bir adresten
     * indirme denemek sessiz bir hata olurdu. */
    char big[512];
    snprintf(big, sizeof(big), "{\"version_code\":1,\"url\":\"https://%0400d\"}", 1);
    check(ota_manifest_parse(big, &m) == OTA_MF_ERR_FIELDS,
          "url alana sığmıyorsa reddedilir (kırpılmaz)");

    /* Kapanmamış dize. */
    check(ota_manifest_parse("{\"version_code\":1,\"url\":\"acik", &m)
          == OTA_MF_ERR_FIELDS, "kapanmamış dize reddedilir");
}

int main(void)
{
    printf("ota_manifest testi\n");

    test_full();
    test_compare();
    test_required();
    test_notes_pollution();
    test_forced();
    test_limits();

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
