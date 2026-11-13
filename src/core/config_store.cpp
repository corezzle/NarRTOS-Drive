/**
 * @file config_store.cpp
 * @brief NVS tabanlı ayar deposu (bkz. header).
 *
 * Tasarım notu: her yazmada handle açıp kapatmıyoruz — handle boot'ta bir kez
 * açılıp açık kalıyor. Ayar yazma seyrek bir iş (saha kurulumunda birkaç kez),
 * ama okuma her boot'ta ve her rapor turunda olabiliyor.
 *
 * Her `set` sonrası nvs_commit() çağrılıyor. Maliyeti var ama ayarı yazıp
 * cihazı hemen fişten çeken bir kurulumcu ayarını kaybetmemeli.
 */

#include "config_store.h"
#include "os_config.h"

#include <esp_log.h>
#include <nvs.h>
#include <string.h>

static const char* TAG = "cfg";

static nvs_handle_t s_nvs   = 0;
static bool         s_ready = false;

esp_err_t config_store_init(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open('%s') başarısız: %s",
                 NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "init OK (namespace='%s')", NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t config_store_get_str(const char* key, char* out, size_t out_len,
                               const char* def)
{
    if (!out || out_len == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';

    if (!s_ready) {
        if (def) strlcpy(out, def, out_len);
        return ESP_ERR_INVALID_STATE;
    }

    size_t len = out_len;
    esp_err_t err = nvs_get_str(s_nvs, key, out, &len);
    if (err != ESP_OK) {
        /* Anahtar yok ya da buffer küçük — ikisinde de varsayılana düş.
         * Çağıran hata kodundan hangisi olduğunu anlayabilir. */
        if (def) strlcpy(out, def, out_len);
        else out[0] = '\0';
    }
    return err;
}

esp_err_t config_store_set_str(const char* key, const char* val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    esp_err_t err;
    if (!val || val[0] == '\0') {
        /* Boş değer = ayarı sil. "set apn" yazıp APN'i temizlemek isteyen
         * kurulumcu için; boş dize yazsaydık "APN = boş dize" gibi geçerli
         * ama anlamsız bir ayar kalırdı. */
        err = nvs_erase_key(s_nvs, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_str(s_nvs, key, val);
    }
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "set '%s': %s", key, esp_err_to_name(err));
    return err;
}

esp_err_t config_store_get_u16(const char* key, uint16_t* out, uint16_t def)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = def;
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    uint16_t v = 0;
    esp_err_t err = nvs_get_u16(s_nvs, key, &v);
    if (err == ESP_OK) *out = v;
    return err;
}

esp_err_t config_store_set_u16(const char* key, uint16_t val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_u16(s_nvs, key, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t config_store_get_u32(const char* key, uint32_t* out, uint32_t def)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = def;
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    uint32_t v = 0;
    esp_err_t err = nvs_get_u32(s_nvs, key, &v);
    if (err == ESP_OK) *out = v;
    return err;
}

esp_err_t config_store_set_u32(const char* key, uint32_t val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_u32(s_nvs, key, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t config_store_erase_all(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_all(s_nvs);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    ESP_LOGW(TAG, "tüm ayarlar silindi (%s)", esp_err_to_name(err));
    return err;
}
