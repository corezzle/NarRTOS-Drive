#pragma once
/**
 * @brief NarRTOS-Drive donanım self-test checklist'i.
 *
 * Ekransız cihaz komut-güdümlü olduğu için test de bir KOMUT olarak çalışır
 * (`selftest`), boot'ta OTOMATİK değil — sahadaki/araçtaki açılışı hiç
 * etkilemez. Wear/board-selftest'teki PASS/FAIL/UYARI/ATLA tablo kalıbını
 * izler ama Drive'ın KENDİ çevrebirimlerini sınar (store&forward, CAN, GNSS,
 * modem, IMU, Wi-Fi, kontak, LED).
 *
 * Güvenlik: yalnızca MEVCUT servislerin salt-okunur durum API'lerini ve
 * zaten komut arayüzünde kullanılan güvenli probları (wifi scan, led test)
 * çağırır — çalışan hiçbir servisi yeniden başlatmaz/bozmaz. Donanımı
 * olmayan/derleme dışı (ENABLE_x=0) çevrebirimler ATLA satırı olarak görünür;
 * çerçeve hazır, kart gelince otomatik koşar.
 */

#include "command_service.h"   /* cmd_reply_fn_t */

#ifdef __cplusplus
extern "C" {
#endif

/** Checklist'i çalıştırır; her satırı `reply(line, ctx)` ile yollar.
 *  command_service'in `selftest` komutundan, aktif yanıt kanalıyla çağrılır. */
void selftest_run(cmd_reply_fn_t reply, void* ctx);

#ifdef __cplusplus
}
#endif
