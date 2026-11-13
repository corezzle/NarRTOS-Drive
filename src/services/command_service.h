#pragma once
/**
 * @brief Command Service — Drive'ı dışarıdan metin komutlarıyla sürme.
 *
 * Wear'daki komut servisinin kalıbı aynen taşındı: taşıyıcıdan bağımsız bir
 * parser çekirdeği + üstüne takılan taşıyıcılar. Wear'da USB ve BLE vardı;
 * Drive'da USB (M1) ve M3'ten beri MQTT konusu (`<önek>/<imei>/cmd`) — ikisi
 * de aynı çekirdeğe giriyor, yanıtlar kendi kanalına dönüyor.
 *
 * Drive'da bu servis Wear'dakinden daha kritik: ekran yok, buton yok. Cihazla
 * konuşmanın tek yolu bu. Bu yüzden boot sırasında diğer servislerden ÖNCE
 * başlar — yanlış ayarla açılan cihaza müdahale edilebilsin diye.
 *
 * Komut seti:
 *   help                        komut listesi
 *   info                        sürüm/uptime/heap/ağ/tampon özeti (JSON)
 *   get config                  tüm ayarları dök (şifreler değer olarak DEĞİL)
 *   set imei <imei>             cihaz kimliği — sunucu IMEI ile anahtarlıyor
 *   set mqtt host|port|user|pass <x>
 *   set wifi ssid|pass <x>      atölye WiFi'si — değer boşluk içerebilir
 *   set net backend <wifi|modem|none>
 *   set apn <apn>               modem APN'i
 *   set can bitrate <kbps>      125|250|500|1000
 *   config clear                fabrika ayarı
 *   can inject <0xID,b0,..,b7>  ham CAN frame'i enjekte et (donanımsız test)
 *   can stats|dump [n]|clear    CAN kimlik tablosu
 *   set ntp <host>              NTP sunucusu (saatsiz cihaz rapor gönderemez)
 *   net status|offline <0|1>|reconnect|modem
 *   mqtt status                 istemci sayaçları
 *   sf stats|push <metin>|peek|clear   store&forward tamponu
 *   time status                 duvar saati ve kaynağı
 *   telemetry now|status|ignition <0|1|auto>
 *   set gnss baud <baud>        NMEA alıcısı hızı
 *   gnss status|inject $GPRMC,...*HH
 *   set ota url <adres>         manifest adresi (boş = OTA kapalı)
 *   ota status|check|install    kablosuz güncelleme
 *   led status|test [sn]        durum LED'i
 *   reboot                      yeniden başlat
 *
 * `can inject`, `sf push`, `net offline` ve `telemetry ignition` aynı amaca
 * hizmet ediyor: donanım ve saha koşulu olmadan boru hattını uçtan uca
 * sürebilmek.
 *
 * `sf peek` tampondaki İKİLİ kaydı broker'a gidecek JSON'a çevirerek
 * gösteriyor — aynı üreticiyi kullanıyor, yani konsolda görülen ile
 * gönderilen birebir aynı.
 *
 * Yanıtlar "[cmd]" önekiyle döner — log satırlarından ayırt edilebilsin diye.
 */

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UART okuyucu görevini başlat. nvs + config_store'dan SONRA çağrılmalı. */
esp_err_t command_service_init(void);

/** Yanıt satırı alıcısı — taşıyıcı kendi kanalına yazar. */
typedef void (*cmd_reply_fn_t)(const char* line, void* ctx);

/** Tek komut satırını çalıştır — taşıyıcıdan bağımsız çekirdek.
 *  Eşzamanlı çağrılar içeride mutex ile serileşir.
 *  @param line NUL-sonlu, DEĞİŞTİRİLEBİLİR buffer (strtok_r kullanılıyor). */
void command_service_exec_ex(char* line, cmd_reply_fn_t reply, void* ctx);

/** exec_ex'in seri (printf) yanıtlı kısayolu. */
void command_service_exec(char* line);

/** `can inject` ile gelen frame'i teslim alacak geri çağrı. M2'de can_service
 *  buraya kendini kaydedecek; şu an kayıtlı değilse komut frame'i doğrulayıp
 *  "tüketici yok" der. Böylece M1'de tel formatını donanımsız sınayabiliyoruz.
 *  @param id   11/29-bit CAN kimliği
 *  @param ext  29-bit (extended) ise true
 *  @param data 0-8 bayt
 *  @param dlc  bayt sayısı */
typedef void (*can_frame_sink_fn_t)(uint32_t id, bool ext,
                                    const uint8_t* data, uint8_t dlc);
void command_service_set_can_sink(can_frame_sink_fn_t fn);

#ifdef __cplusplus
}
#endif
