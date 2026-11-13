/**
 * @file sf_flash_esp.cpp
 * @brief sf_flash'ın hedef uygulaması — partisyon tablosundaki "buffer" bölümü.
 *
 * Dosya sistemi YOK. Bölüm ham blok aygıtı olarak kullanılıyor. Gerekçe:
 *
 *  - SPIFFS/LittleFS mount süresi, RAM'i ve bozulma riski taşır; 2,4 MB'lık bir
 *    bölümde küçük ve sık yazma SPIFFS'in en kötü çalıştığı desen.
 *  - store_forward'ın ihtiyacı sabit boyutlu slotlardan oluşan bir halka;
 *    dosya adı, dizin, boyut değiştirme gibi hiçbir dosya sistemi özelliği
 *    kullanılmayacak. Altına dosya sistemi koymak sadece araya bir bozulma
 *    kaynağı eklemek olurdu.
 *
 * partitions_drive.csv'de bölüm tipi `data, spiffs` — bu yalnızca bir alt tip
 * etiketi; içine ne yazdığımızı bootloader'ın umurunda değil. Etiketi
 * değiştirmiyoruz ki mevcut tablo ve elde kalan flash imajları geçerli kalsın.
 */

#include "sf_flash.h"
#include "../../core/os_config.h"

#include <esp_log.h>
#include <esp_partition.h>

static const char* TAG = "sfflash";

static const esp_partition_t* s_part = NULL;

esp_err_t sf_flash_init(void)
{
    if (s_part) return ESP_OK;

    /* Alt tipi ANY veriyoruz: tabloda "spiffs" yazıyor ama bizim için anlamı
     * yok, ileride alt tip değişse de kod bulmaya devam etsin. Etiket
     * ("buffer") tek bağlayıcı nokta. */
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY,
                                      SF_PARTITION_LABEL);
    if (!s_part) {
        ESP_LOGE(TAG, "'%s' bölümü yok — partitions_drive.csv devrede mi?",
                 SF_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "init OK ('%s' @0x%06lX, %lu bayt)", SF_PARTITION_LABEL,
             (unsigned long)s_part->address, (unsigned long)s_part->size);
    return ESP_OK;
}

size_t sf_flash_size(void)
{
    return s_part ? (size_t)s_part->size : 0;
}

esp_err_t sf_flash_read(size_t off, void* dst, size_t len)
{
    if (!s_part) return ESP_ERR_INVALID_STATE;
    if (off + len > s_part->size) return ESP_ERR_INVALID_SIZE;
    return esp_partition_read(s_part, off, dst, len);
}

esp_err_t sf_flash_write(size_t off, const void* src, size_t len)
{
    if (!s_part) return ESP_ERR_INVALID_STATE;
    if (off + len > s_part->size) return ESP_ERR_INVALID_SIZE;
    if ((off % 4) || (len % 4)) return ESP_ERR_INVALID_ARG;
    return esp_partition_write(s_part, off, src, len);
}

esp_err_t sf_flash_erase(size_t off, size_t len)
{
    if (!s_part) return ESP_ERR_INVALID_STATE;
    if (off + len > s_part->size) return ESP_ERR_INVALID_SIZE;
    if ((off % SF_SLOT_SIZE) || (len % SF_SLOT_SIZE)) return ESP_ERR_INVALID_ARG;
    return esp_partition_erase_range(s_part, off, len);
}
