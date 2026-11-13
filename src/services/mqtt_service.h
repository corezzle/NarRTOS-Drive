#pragma once
/**
 * @brief MQTT istemcisi — store&forward tamponunu broker'a boşaltır.
 *
 * ── EN ÖNEMLİ KARAR: rapor HER ZAMAN tampondan geçer ──────────────────────
 *
 * Çevrimiçi olsa bile telemetri doğrudan publish edilmiyor; önce flash'a
 * yazılıp sonra oradan gönderiliyor. İki sebep:
 *
 *  1. **Tek yol.** "Çevrimiçiyse doğrudan gönder, değilse tamponla" tasarımında
 *     tampon yolu yalnızca tünelde çalışır — yani yılda birkaç kez. Nadiren
 *     çalışan yol, ihtiyaç duyulduğunda bozuk olan yoldur. Her rapor aynı
 *     yoldan geçince store&forward sürekli sınanıyor.
 *  2. **At-least-once.** publish ile PUBACK arasında bağlantı koparsa kayıt
 *     hâlâ flash'ta durur; onay gelmeden pop edilmiyor. Doğrudan gönderimde o
 *     rapor sessizce kaybolurdu.
 *
 * Bedeli rapor başına bir flash sektör silme+yazma. Hesap: 60 sn periyot =
 * 1440 rapor/gün, 624 slotluk halka = günde 2,3 tur. NOR flash sektör
 * dayanımı ~100.000 tur → 100000 / 2,3 ≈ 43.000 gün. Cihazın ömrü yanında
 * yok sayılır.
 *
 * ── Boşaltma: aynı anda TEK kayıt havada ──────────────────────────────────
 *
 * peek -> publish(QoS1) -> PUBACK -> pop. Sıra korunmak zorunda: güzergâh
 * kronolojik olmalı. Birikmiş 800 kayıtlık tamponu tek kayıtla boşaltmak
 * ~40 sn sürer, kabul edilebilir (MQTT_INFLIGHT_MAX ile artırılabilir).
 *
 * ── Komut taşıyıcısı ──────────────────────────────────────────────────────
 *
 * `<önek>/<imei>/cmd` konusuna gelen satırlar command_service'in aynı
 * çekirdeğine giriyor, yanıt `cmd/reply`ye dönüyor. Wear'da USB ve BLE vardı;
 * bu üçüncü taşıyıcı. Ekransız bir araç cihazında uzaktan `get config` /
 * `set mqtt host` yapabilmek sahada tek müdahale imkânı olabilir.
 *
 * ⚠ Konu şeması ve kimlik doğrulama TAHMİN — bkz. mqtt_contract.h.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     compiled;      /**< ENABLE_MQTT ile derlendi mi */
    bool     connected;
    uint32_t published;     /**< PUBACK alınan telemetri kaydı */
    uint32_t ack_timeouts;  /**< onay gelmeden zaman aşan (kayıt korundu) */
    uint32_t cmds_received;
    uint32_t reconnects;
    uint32_t render_held;   /**< yükü şu an üretilemeyen (bekletildi) */
    uint32_t render_dropped;/**< yükü asla üretilemeyen (düşürüldü) */
} mqtt_stats_t;

/* ── Yük üretici ─────────────────────────────────────────────────────────────
 *
 * Tampona İKİLİ kayıt yazılıyor; yayınlanacak JSON gönderim anında üretiliyor
 * (gerekçe: telemetry_codec.h — zaman damgası ve kapasite). mqtt_service
 * kayıt biçimini BİLMİYOR; telemetry_service boot'ta buraya bir üretici
 * takıyor. Üretici takılı değilse kayıt ham hâliyle yayınlanır.
 *
 * Dönüş: >=0 yazılan bayt, ya da aşağıdaki kodlar. */
#define MQTT_RENDER_RETRY (-1)  /**< şu an üretilemiyor → kayıt tamponda KALIR */
#define MQTT_RENDER_DROP  (-2)  /**< asla üretilemez → kayıt düşürülür */

typedef int (*mqtt_render_fn)(const void* rec, size_t len, char* out,
                              size_t cap, void* ctx);
void mqtt_service_set_render(mqtt_render_fn fn, void* ctx);

/** İstemciyi hazırla ve net_service'in durum bildirimine abone ol.
 *  net_service ve store_forward'dan SONRA çağrılmalı. */
esp_err_t mqtt_service_init(void);

bool mqtt_service_is_connected(void);
void mqtt_service_stats(mqtt_stats_t* out);

/** Broker ayarları değişti — istemciyi kapat, yeni ayarlarla kurulsun.
 *
 *  Bu olmadan `set mqtt host` çalışan bir istemciyi ETKİLEMİYORDU: adres ve
 *  konular istemci kurulurken bir kez okunuyor, sonrası reboot'a kadar
 *  sabit kalıyordu. Ekransız cihazda yanlış broker adresi yazan kurulumcu
 *  için tuzaktı — düzeltmesi işe yaramıyor, sebebi de görünmüyordu.
 *
 *  Gerçek iş mqtt görevinde yapılıyor; burası yalnız bayrak koyuyor, çünkü
 *  komut görevi istemciyi yayın ortasında yok edemez. */
void mqtt_service_reconfigure(void);

/** Telemetri kaydını sıraya koy. store_forward'a YAZAR; gönderimi servis
 *  kendi görevinde yapar. Çevrimdışıyken de başarılıdır — asıl amaç bu. */
esp_err_t mqtt_service_enqueue_report(const void* payload, size_t len);

/** Tamponu atlayan tek atımlık yayın — durum/teşhis içindir, telemetri için
 *  DEĞİL (kaybı sessiz olur). */
esp_err_t mqtt_service_publish_raw(const char* leaf, const char* payload,
                                   int qos, bool retain);

#ifdef __cplusplus
}
#endif
