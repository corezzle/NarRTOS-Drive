#pragma once
/**
 * @brief OTA manifest ayrıştırıcısı — saf, host'ta test edilir.
 *
 * nmea_parser ve telemetry_codec ile aynı disiplin: ağ, donanım, FreeRTOS
 * bilmiyor. Sebebi burada özellikle ağır: sürüm karşılaştırmasındaki bir hata
 * ya cihazı hiç güncellenmez yapar (fark edilmesi aylar sürer) ya da sonsuz
 * güncelleme döngüsüne sokar — ikisi de sahada, ulaşılamayan cihazlarda.
 *
 * Manifest biçimi Wear ile AYNI (`corezzle-wearos-backend`):
 *
 *   {"version":"1.2.3","version_code":10203,"url":"https://...",
 *    "size":812345,"notes":"...","forced":false}
 *
 * ⚠ Uç nokta AYRI olmalı. Wear `/api/wear-os/manifest`, Drive kendi kanalını
 * kullanacak; yoksa Wear cihazına Drive firmware'i iner (Session 1 kararı).
 *
 * `forced`: yöneticinin ittiği kritik güncelleme. Drive'da normalde kurulum
 * yalnız araç park hâlindeyken yapılıyor; forced bu pencereyi atlıyor.
 *
 * cJSON kullanılmıyor — Wear'daki strstr yaklaşımı korundu, ama anahtar
 * eşleşmesi sıkılaştırıldı (bkz. .cpp): anahtarın hemen ardından ':' gelmeli,
 * yoksa `notes` metninin içindeki bir kelime alan sanılabilir.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     version[24];
    int32_t  version_code;
    char     url[192];
    uint32_t size;
    char     notes[96];
    bool     forced;
} ota_manifest_t;

typedef enum {
    OTA_MF_OK = 0,
    OTA_MF_ERR_ARG,
    OTA_MF_ERR_PARSE,      /**< JSON'a benzemiyor */
    OTA_MF_ERR_FIELDS,     /**< version_code ya da url yok/geçersiz */
} ota_manifest_result_t;

/** Manifest JSON'unu ayrıştır. Yalnız OTA_MF_OK dönerse `out` geçerlidir. */
ota_manifest_result_t ota_manifest_parse(const char* json, ota_manifest_t* out);

/**
 * @brief Bu manifest kurulmalı mı?
 *
 * Yalnız SÜRÜM KODU büyükse. Dize karşılaştırması bilinçli olarak yok:
 * "0.10.0" < "0.2.0" tuzağı os_config.h'de zaten not edilmiş.
 *
 * Eşit ya da küçük kod = kurulmaz. Sunucunun yanlışlıkla eski bir sürüm
 * yayınlaması, sahadaki filoyu geri düşürmemeli.
 */
bool ota_manifest_is_newer(const ota_manifest_t* m, int32_t current_code);

#ifdef __cplusplus
}
#endif
