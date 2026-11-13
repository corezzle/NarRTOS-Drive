#pragma once
/**
 * @brief Durum LED'i — ekransız cihazın TEK yerel geri bildirimi.
 *
 * Drive'ın ekranı, butonu, hoparlörü yok. Kablo takılıyken seri konsol,
 * bağlantı varken MQTT komut konusu var; ama kurulumcu aracın altında,
 * elinde dizüstü olmadan ve cihaz henüz ağa girmemişken bu ikisinden de
 * yararlanamıyor. O anda "çalışıyor mu, ayarı eksik mi, ağa girdi mi"
 * sorusuna bakacağı tek yer bu LED.
 *
 * ── Desen dili: NE KADAR ÇOK IŞIK, O KADAR KÖTÜ ───────────────────────────
 *
 *   sürekli yanık          cihaz işini yapamıyor (tampon kurulamadı)
 *   hızlı yanıp sönme      ayar eksik — müdahale bekliyor
 *   uzun tek flaş / 2 sn   yaşıyor ama bağlantı yok
 *   çift kısa flaş / 2 sn  ağ var, broker'a bağlanamıyor
 *   çok kısa kırpma / 3 sn her şey yolunda
 *   hiç yanmıyor           besleme yok ya da cihaz ölü
 *
 * "Hiç yanmıyor" en önemli sinyal ve bedavaya geliyor: sağlıklı durumda bile
 * LED düzenli kırptığı için, hiç kırpmaması tek başına arıza demek. Sağlıklı
 * durumu "sürekli yanık" yapan tasarımlarda bu ayrım kaybolur.
 *
 * ── Durum ÖNCELİKLİ ───────────────────────────────────────────────────────
 *
 * Aynı anda birden çok sorun olabilir (ayar da eksik, ağ da yok). En derindeki
 * sorun gösteriliyor: ayarı olmayan cihazın "çevrimdışı" demesi kurulumcuyu
 * ağ/anten aramaya iterdi, oysa sorun kendi girmediği ayarda.
 *
 * ── Servis kimseye BAĞLANMIYOR ────────────────────────────────────────────
 *
 * Durumu diğer servislere sormak yerine onlardan bildirim beklemedik: LED
 * kendi görevinde net/mqtt/store_forward durumunu yokluyor. Böylece hiçbir
 * servis LED'i bilmek zorunda kalmıyor ve yeni bir durum eklemek tek dosyayı
 * değiştiriyor.
 *
 * PIN_STATUS_LED -1 ise servis hiç başlamaz (LED'siz kart geçerli bir hâl).
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_ERROR = 0,   /**< sürekli yanık — tampon yok, rapor saklanamıyor */
    LED_STATE_UNCONFIGURED,/**< hızlı — ayar eksik */
    LED_STATE_OFFLINE,     /**< uzun tek flaş — bağlantı yok */
    LED_STATE_NO_BROKER,   /**< çift flaş — ağ var, broker yok */
    LED_STATE_ONLINE,      /**< kısa kırpma — her şey yolunda */
} led_state_t;

/** GPIO'yu kur ve desen görevini başlat. Diğer servislerden SONRA çağrılmalı
 *  (durumlarını yokluyor). PIN_STATUS_LED < 0 ise hiçbir şey yapmaz. */
esp_err_t status_led_init(void);

/** Şu an gösterilen durum — `led status` komutu için. */
led_state_t status_led_current(void);

const char* status_led_state_name(led_state_t s);

/** Deseni geçici olarak zorla (montajda LED'i bulmak için). Süre dolunca
 *  otomatik durumu göstermeye döner. 0 = zorlamayı kaldır. */
void status_led_force(led_state_t s, uint32_t ms);

#ifdef __cplusplus
}
#endif
