#pragma once
/**
 * @brief Kalıcı ayar deposu — NVS üzerinde ince bir katman.
 *
 * Drive'ın saha ayarları burada durur: cihaz kimliği (IMEI), MQTT sunucusu,
 * APN. Hepsi USB komut arayüzünden yazılır (`set imei ...`), yeniden
 * başlatmaya dayanır.
 *
 * IMEI neden elle giriliyor: araç takip sunucusu cihazları IMEI ile anahtarlıyor
 * (devices.imei birincil anahtar, vehicles.device_imei ile bağlanıyor).
 * Normalde IMEI modemden okunur ama modem donanımı henüz yok; o gelene kadar
 * elle yazılıyor. Modem geldiğinde config_store'daki değer varsayılan olarak
 * kalır, modemden okunan onu ezer.
 */

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NVS namespace'ini açar. nvs_flash_init'ten SONRA çağrılmalı. */
esp_err_t config_store_init(void);

/** Dizi ayar oku. Anahtar yoksa `def` kopyalanır ve ESP_ERR_NVS_NOT_FOUND
 *  döner — çağıran isterse "ayarlanmamış" durumunu ayırt edebilir. */
esp_err_t config_store_get_str(const char* key, char* out, size_t out_len,
                               const char* def);

/** Dizi ayar yaz ve hemen commit et. Boş dize anahtarı siler. */
esp_err_t config_store_set_str(const char* key, const char* val);

esp_err_t config_store_get_u16(const char* key, uint16_t* out, uint16_t def);
esp_err_t config_store_set_u16(const char* key, uint16_t val);

/** 16 bite sığmayan sayılar için (ör. GNSS baud 115200). Ölçekleyip u16'ya
 *  sıkıştırmak yerine doğru tipi kullanıyoruz — "baud/100" gibi kodlamalar
 *  okuyan tarafta hataya davetiye. */
esp_err_t config_store_get_u32(const char* key, uint32_t* out, uint32_t def);
esp_err_t config_store_set_u32(const char* key, uint32_t val);

/** Tüm Drive ayarlarını sil (fabrika ayarı). WiFi/OTA gibi başka
 *  namespace'lere dokunmaz. */
esp_err_t config_store_erase_all(void);

#ifdef __cplusplus
}
#endif
