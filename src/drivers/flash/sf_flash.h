#pragma once
/**
 * @brief store_forward'ın altındaki blok aygıtı — kasten ince bir soyutlama.
 *
 * Tek sebebi test edilebilirlik. Hedefte `esp_partition` ("buffer" bölümü),
 * Mac'te NOR flash semantiğini taklit eden bir RAM sahtesi. `store_forward.cpp`
 * hangisiyle konuştuğunu bilmez, bu yüzden sahada çalışacak kodun aynısı
 * host'ta sınanabiliyor — can_service'te izlenen yolun aynısı.
 *
 * UYGULAMALAR ŞU SEMANTİĞİ ZORUNLU KILAR (host sahtesi ihlalleri sayar):
 *   - Yazma yalnız 1 bitlerini 0 yapar. 0'ı 1 yapmanın tek yolu silmek.
 *     store_forward "gönderildi" işaretini bu yüzden 0xFFFFFFFF -> 0x00000000
 *     olarak yazıyor: aynı slotu silmeden güncelleyebilmek için.
 *   - Silme sektör granülerliğinde (SF_SLOT_SIZE) ve ofset sektöre hizalı.
 *   - Yazma ofseti ve uzunluğu 4 bayta hizalı. Okumada hizalama şartı yok.
 *
 * Boyut çalışma zamanında öğrenilir (sf_flash_size); partisyon tablosu
 * değişirse kod değişmesin.
 */

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bölümü bul/hazırla. Bulunamazsa ESP_ERR_NOT_FOUND. */
esp_err_t sf_flash_init(void);

/** Kullanılabilir bayt sayısı. init edilmediyse 0. */
size_t sf_flash_size(void);

esp_err_t sf_flash_read(size_t off, void* dst, size_t len);

/** off ve len 4 bayta hizalı olmalı. Yalnız 1->0 geçişleri yapılabilir. */
esp_err_t sf_flash_write(size_t off, const void* src, size_t len);

/** off ve len SF_SLOT_SIZE'a hizalı olmalı. */
esp_err_t sf_flash_erase(size_t off, size_t len);

#ifdef __cplusplus
}
#endif
