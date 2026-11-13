#pragma once
/**
 * @brief CAN frame toplayıcı — kimlik başına en son frame.
 *
 * ÖNEMLİ: bu bir FIFO DEĞİL. J1939'da her PGN periyodik tekrar eder
 * (10 ms – 1 sn) ve 250 kbit/s hat saniyede ~2000 frame taşır. Kuyruklasak
 * saniyeler içinde taşar, rapora hattın rastgele bir dilimi giderdi.
 * Sunucunun istediği şey her sinyalin GÜNCEL değeri, geçmişi değil —
 * o yüzden kimlik başına yalnız son frame saklanıyor.
 *
 * Servis DONANIMDAN BAĞIMSIZ. İki kaynaktan beslenir:
 *   - `drivers/can/twai_driver` (ENABLE_CAN=1 iken gerçek hat)
 *   - `command_service`'in `can inject` komutu (donanımsız test)
 * İkisi de aynı kapıya gider: can_service_ingest().
 *
 * Cihaz frame'i ÇÖZMEZ. Ham bayt saklanır ve "0xID,b0,..,b7" biçiminde
 * sunucuya iletilir; DBC çözümü araç takip sunucusunde yapılır.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;          /**< 11 veya 29 bit CAN kimliği */
    uint8_t  data[8];
    uint8_t  dlc;         /**< 0-8 */
    bool     ext;         /**< 29-bit genişletilmiş kimlik */
    int64_t  last_us;     /**< son görülme (esp_timer_get_time) */
    uint32_t count;       /**< bu kimlikten kaç frame görüldü */
} can_frame_t;

typedef struct {
    uint32_t rx_total;      /**< toplam alınan frame */
    uint32_t dropped_full;  /**< tablo dolu olduğu için atılan YENİ kimlik */
    uint16_t distinct;      /**< tablodaki farklı kimlik sayısı */
    uint16_t capacity;      /**< tablo kapasitesi */
} can_stats_t;

/** Tabloyu kur ve `can inject` sink'ini kaydet. */
esp_err_t can_service_init(void);

/** Bir frame'i tabloya işle. Hem TWAI görevinden hem komut arayüzünden
 *  çağrılır; içeride mutex ile korunur. */
void can_service_ingest(uint32_t id, bool ext, const uint8_t* data, uint8_t dlc);

/** Telemetri için anlık görüntü. En son görülenden en eskiye doğru sıralı
 *  yazar — rapor `max` ile sınırlıysa en taze veriler gitsin.
 *  @return yazılan frame sayısı */
size_t can_service_snapshot(can_frame_t* out, size_t max);

void can_service_stats(can_stats_t* out);

/** Tabloyu boşalt (test / kontak kapanışı). Sayaçlar da sıfırlanır. */
void can_service_clear(void);

#ifdef __cplusplus
}
#endif
