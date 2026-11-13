#pragma once
/**
 * @brief Ham CAN frame'inden TEK bir sinyal çıkarır: aracın tekerlek hızı.
 *
 * ── Bu modül temel bir karara istisna ──────────────────────────────────────
 *
 * Projenin kurucu kararlarından biri "araç verisini cihaz çözmez, sunucu
 * çözer" idi (bkz. can_service.h). Gerekçesi hâlâ geçerli: 5,7 MB'lık DBC
 * dosyası cihaza sığmaz ve sinyal tanımları cihaz güncellenmeden
 * değişebilmeli.
 *
 * Burada o karara SINIRLI bir istisna yapılıyor. Sebebi: tünelde uydu
 * kesildiğinde konumu sürdürebilmek için cihazın ne kadar yol aldığını
 * BİLMESİ gerekiyor ve bu bilgi yalnız araçtan gelebilir. Sunucudan sormak
 * mümkün değil, çünkü tünelde şebeke de yok.
 *
 * İstisnanın sınırları:
 *   - YALNIZCA tekerlek hızı (SPN 84). Başka hiçbir sinyal çözülmüyor.
 *   - Ham frame yine değiştirilmeden sunucuya gidiyor. Bu modül veriyi
 *     tüketiyor, akışa müdahale etmiyor.
 *   - Çözülemezse ölü hesap devre dışı kalıyor; başka hiçbir şey bozulmuyor.
 *
 * ── Eşleştirme ÖNCELİKTEN BAĞIMSIZ ─────────────────────────────────────────
 *
 * Sunucudaki DBC eşleştirmesi CAN kimliğinin öncelik bitlerine duyarlı ve bu
 * sahada sorun çıkarıyor (bkz. Sorun Giderme). Cihaz içinde böyle bir kısıt
 * yok: yalnız PGN'e bakıyoruz, öncelik ve kaynak adresi ne olursa olsun
 * eşleşiyor. Aynı aracın farklı önceliklerle yayın yapması ölü hesabı
 * bozmuyor.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** J1939 PGN 65265 (CCVS1) — tekerlek hızını taşıyan mesaj grubu. */
#define CAN_PGN_CCVS1 65265u

/** 29 bit CAN kimliğinden PGN çıkarır. PDU1/PDU2 ayrımı yapılır: PF < 240
 *  ise hedef adres PGN'e dahil değildir. */
uint32_t can_signal_pgn(uint32_t id);

/**
 * @brief Tekerlek hızı (SPN 84) çıkarır.
 *
 * CCVS1'de 2. ve 3. baytta, küçük sonlu, çözünürlük 1/256 km/sa.
 * Geçersiz/kullanılamaz değerler (0xFFFF, 0xFEFF üstü) reddedilir; J1939'da
 * bu aralık "veri yok" ve "hata" anlamına geliyor. Bunları 0 km/sa olarak
 * okumak, duran bir araç sanmaya yol açardı.
 *
 * @param out_kmh_x10 km/sa'in 10 katı (ör. 543 = 54,3 km/sa)
 * @return frame bu sinyali taşımıyorsa ya da değer geçersizse false
 */
bool can_signal_wheel_speed(uint32_t id, const uint8_t* data, uint8_t dlc,
                            uint16_t* out_kmh_x10);

#ifdef __cplusplus
}
#endif
