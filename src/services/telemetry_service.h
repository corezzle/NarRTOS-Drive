#pragma once
/**
 * @brief Periyodik rapor üretimi — CAN tablosu + konum + kontak → tampon.
 *
 * Akış:
 *   can_service tablosu ──┐
 *   kontak girişi ────────┼──> telemetry_service ──> store_forward (ikili)
 *   konum (M5) ───────────┘                              │
 *                                                        v
 *                                          mqtt_service ──> JSON ──> broker
 *
 * Servis JSON ÜRETMEZ; ikili kayıt yazar ve JSON gönderim anında
 * telemetry_codec ile üretilir. Gerekçesi telemetry_codec.h'de (zaman
 * damgası + kapasite).
 *
 * ── Periyot kontağa bağlı ─────────────────────────────────────────────────
 *
 * Kontak açıkken TELEMETRY_PERIOD_MS, park hâlindeyken
 * TELEMETRY_PERIOD_PARKED_MS. Park eden araç saatlerce aynı noktada durur;
 * dakikada bir rapor göndermek hem hücre trafiği hem de tampon israfı olur.
 *
 * ── Kontak DEĞİŞİMİ periyodu beklemez ─────────────────────────────────────
 *
 * Kontak açılıp kapandığında anında bir rapor üretiliyor. Sunucu tarafında
 * hazır kontak raporu/alarmı var ve ilk açılış-son kapanış saatlerini
 * `dateTime` ile hesaplıyor (`getFirstIgnitionTimes`); değişimi 10 dakika
 * gecikmeli bildirmek o raporu doğrudan yanlışlar.
 *
 * ── Kontak kaynağı ────────────────────────────────────────────────────────
 *
 * PIN_IGNITION dijital girişi. Donanım yok, o yüzden `telemetry ignition`
 * komutuyla elle sürülebiliyor — `can inject` ve `net offline` ile aynı
 * mantık: saha koşulunu masa üstünde üretmek.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t captured;      /**< üretilen rapor */
    uint32_t push_failed;   /**< tampona yazılamayan */
    uint32_t last_frames;   /**< son raporda taşınan CAN frame sayısı */
    bool     ignition;      /**< şu anki kontak durumu */
    int8_t   ignition_sim;  /**< -1 = gerçek pin, 0/1 = elle sürülüyor */
    uint32_t next_in_ms;    /**< sonraki rapora kalan */
} telemetry_stats_t;

/** Kontak girişini kur ve yakalama görevini başlat.
 *  store_forward ve time_service'ten SONRA çağrılmalı. */
esp_err_t telemetry_service_init(void);

/** Hemen bir rapor üret ve tampona yaz. Periyodu etkilemez.
 *  @return store_forward_push sonucu */
esp_err_t telemetry_service_capture_now(void);

/** Kontağı elle sür: -1 gerçek pini kullan, 0/1 zorla. */
void telemetry_service_set_ignition_sim(int8_t v);

void telemetry_service_stats(telemetry_stats_t* out);

/** İkili kaydı telemetri JSON'una çevirir — mqtt_service'in yük üreticisi.
 *  `sf peek` komutu da bunu kullanıyor ki konsolda görülen ile broker'a
 *  giden birebir aynı olsun. İmza mqtt_render_fn ile uyumlu. */
int telemetry_render_for_mqtt(const void* rec, size_t len, char* out,
                              size_t cap, void* ctx);

#ifdef __cplusplus
}
#endif
