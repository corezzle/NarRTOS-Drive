#pragma once
/**
 * @file fake_flash.h
 * @brief sf_flash'ın host uygulaması — NOR flash semantiğini ZORLAYAN sahte.
 *
 * Bu sahtenin değeri sadece "RAM'de bir dizi" olması değil; gerçek flash'ın
 * kurallarını uygulaması. store_forward yanlış bir şey yaparsa test bunu
 * yakalar, oysa naif bir RAM tamponu sessizce kabul ederdi:
 *
 *   - Yazma yalnız 1 -> 0 yapabilir. 0 -> 1 denemesi İHLAL sayılır. Silinmemiş
 *     bir slota yazmaya kalkışmak burada patlar; gerçek donanımda ise sessizce
 *     bozuk kayıt üretirdi.
 *   - Yazma ofseti/uzunluğu 4 bayta, silme sektöre hizalı olmalı.
 *   - Sınır dışı erişim ihlal sayılır.
 *
 * Ayrıca ELEKTRİK KESİNTİSİ taklit edilebiliyor: bir bayt bütçesi verilir,
 * bütçe bir yazmanın ORTASINDA biterse o yazmanın yalnız sığan kısmı diske
 * iner, çağrı hata döner ve sonraki tüm yazma/silme çağrıları başarısız olur.
 * Araçta kontak kapanması tam bu desendir.
 */

#include <stddef.h>
#include <stdint.h>

/** Sahte flash'ı kur: verilen boyutta, tamamı 0xFF (silinmiş). */
void fake_flash_configure(size_t size);

/** `n` bayt yazıldıktan sonra elektriği kes. SIZE_MAX = kesinti yok. */
void fake_flash_power_cut_after(size_t n);

/** Elektriği geri ver (yeniden başlatma). İçerik korunur. */
void fake_flash_power_restore(void);

/** NOR semantiği ihlali sayısı — testin sonunda 0 olmalı. */
int fake_flash_violations(void);

/** Kurallara bakmadan tek bayt değiştir — bit çürümesi / bozulma taklidi.
 *  store_forward'ın yapamayacağı bir şey; yalnız test bozulmayı üretebilsin. */
void fake_flash_poke(size_t off, uint8_t val);

uint32_t fake_flash_erase_count(void);
uint32_t fake_flash_write_count(void);
void     fake_flash_reset_counters(void);
