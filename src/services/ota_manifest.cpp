/**
 * @file ota_manifest.cpp
 * @brief OTA manifest ayrıştırıcısı (bkz. header).
 *
 * Tasarım notu — anahtar eşleşmesi neden sıkı:
 *
 * Wear'ın yaklaşımı `strstr(json, "\"url\"")` ile anahtarı arayıp sonraki
 * tırnağa atlıyor. Manifest'te `notes` alanı serbest metin ve içinde
 * `"url"` geçen bir cümle olabilir ("yeni url yapısı" gibi) — o zaman yanlış
 * değer okunur. Burada anahtarın hemen ardından (boşluklar atlanarak) ':'
 * gelmesi şart koşuluyor.
 *
 * Ayrıca aranan anahtar HER ZAMAN tırnak içinde (`"url"`), yani `notes`
 * içindeki tırnaksız bir kelime eşleşmiyor.
 */

#include "ota_manifest.h"

#include <string.h>

/** `"key"` arar ve ardından ':' gelmesini şart koşar. Bulursa ':' sonrasına
 *  (boşluklar atlanmış) işaret eder, yoksa NULL. */
static const char* find_value(const char* json, const char* key)
{
    const size_t klen = strlen(key);
    const char*  p    = json;

    while ((p = strstr(p, key)) != NULL) {
        const char* q = p + klen;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            return q;
        }
        p += klen;   /* yanlış eşleşme — aramaya devam */
    }
    return NULL;
}

/** `"..."` değeri kopyalar. Basit kaçış desteği (\" ve \\). */
static bool extract_string(const char* json, const char* key,
                           char* out, size_t cap)
{
    const char* p = find_value(json, key);
    if (!p || *p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            c = *p;             /* \" ve \\ olduğu gibi alınır */
        }
        if (i + 1 >= cap) return false;   /* sığmıyorsa kırpma, reddet */
        out[i++] = c;
        p++;
    }
    if (*p != '"') return false;          /* kapanmamış dize */
    out[i] = '\0';
    return true;
}

static bool extract_int(const char* json, const char* key, int64_t* out)
{
    const char* p = find_value(json, key);
    if (!p) return false;

    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (*p < '0' || *p > '9') return false;

    int64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        if (v > 99999999999LL) return false;
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    return true;
}

static bool extract_bool(const char* json, const char* key, bool* out)
{
    const char* p = find_value(json, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0)  { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

ota_manifest_result_t ota_manifest_parse(const char* json, ota_manifest_t* out)
{
    if (!json || !out) return OTA_MF_ERR_ARG;
    memset(out, 0, sizeof(*out));

    if (!strchr(json, '{')) return OTA_MF_ERR_PARSE;

    /* Zorunlu alanlar: bunlarsız güncelleme yapılamaz. */
    int64_t code = 0;
    if (!extract_int(json, "\"version_code\"", &code)) return OTA_MF_ERR_FIELDS;
    if (code < 0 || code > 2147483647LL) return OTA_MF_ERR_FIELDS;
    if (!extract_string(json, "\"url\"", out->url, sizeof(out->url)))
        return OTA_MF_ERR_FIELDS;
    if (out->url[0] == '\0') return OTA_MF_ERR_FIELDS;

    out->version_code = (int32_t)code;

    /* İsteğe bağlı alanlar — yoksa varsayılan kalır. */
    extract_string(json, "\"version\"", out->version, sizeof(out->version));
    extract_string(json, "\"notes\"",   out->notes,   sizeof(out->notes));

    int64_t size = 0;
    if (extract_int(json, "\"size\"", &size) && size > 0 && size <= 0xFFFFFFFFLL)
        out->size = (uint32_t)size;

    /* Alan yoksa forced=false; "zorla" varsayılanı asla olmamalı. */
    if (!extract_bool(json, "\"forced\"", &out->forced)) out->forced = false;

    return OTA_MF_OK;
}

bool ota_manifest_is_newer(const ota_manifest_t* m, int32_t current_code)
{
    if (!m) return false;
    return m->version_code > current_code;
}
