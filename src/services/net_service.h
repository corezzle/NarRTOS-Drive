#pragma once
/**
 * @brief Ağ servisi — bağlantıyı ayakta tutar, arka ucu soyutlar.
 *
 * Üstündeki katmanlar (mqtt_service) hangi taşıyıcının kullanıldığını bilmez.
 * Arka uç DERLEME bayrağıyla değil NVS ile seçiliyor (`set net backend wifi`):
 * aynı firmware hem atölyede WiFi'yle hem araçta modemle çalışsın, ikinci bir
 * yapı üretmek gerekmesin.
 *
 * Servis kendi görevinde bir durum makinesi çevirir: ayar var mı → bağlan →
 * koptu mu → üstel bekleyerek yeniden dene. Yeniden deneme politikası burada,
 * arka uçta değil (bkz. net_backend.h).
 *
 * ── `net offline` (zorlanmış kapsama kaybı) ────────────────────────────────
 *
 * Kapsama kaybını masa üstünde üretmenin yolu. Araçta tünele girmek doğal bir
 * olay ama atölyede WiFi çalışırken store&forward'ın gerçekten devreye
 * girdiğini görmenin başka yolu yok — WiFi'yi router'dan kapatmak yerine
 * cihaza söylüyoruz. `can inject`'in ağ tarafındaki karşılığı.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_STATE_DISABLED = 0,   /**< arka uç "none" ya da derleme dışı */
    NET_STATE_UNCONFIGURED,   /**< ayar eksik (SSID / APN yok) */
    NET_STATE_OFFLINE,        /**< bağlanmaya çalışıyor ya da kapsama yok */
    NET_STATE_ONLINE,
} net_state_t;

/** Arka ucu seç, durum makinesini başlat. config_store'dan SONRA. */
esp_err_t net_service_init(void);

net_state_t net_service_state(void);
bool        net_service_is_online(void);

const char* net_service_state_name(net_state_t s);
/** Etkin arka ucun adı; yoksa "none". */
const char* net_service_backend_name(void);

/** Sonraki denemeye kalan süre (ms) — `net status` için. */
uint32_t net_service_retry_in_ms(void);

/** Durum DEĞİŞİMLERİNDE çağrılır, net_service görevinden.
 *
 *  Aboneler: mqtt_service (istemciyi başlat) ve time_service (SNTP'yi başlat).
 *  M3'te tek yuva vardı; M4'te ikinci abone gelince küçük bir diziye
 *  çevrildi. Hâlâ event_bus değil — abone sayısı sabit ve derleme anında
 *  belli, konu/filtre gerekmiyor. Üçüncü bağımsız üretici çıkarsa pub/sub
 *  kendini haklı çıkarmaya başlar.
 *
 *  Geri çağrı net görevinde çalışıyor: KISA tutulmalı, bloklamamalı. */
typedef void (*net_state_cb_t)(net_state_t s, void* ctx);

/** @return abone yuvası kalmadıysa ESP_ERR_NO_MEM */
esp_err_t net_service_add_state_cb(net_state_cb_t cb, void* ctx);

/** Kapsama kaybını taklit et (test). Açıkken bağlantı kapatılır ve durum
 *  OFFLINE'da tutulur. */
void net_service_force_offline(bool off);
bool net_service_forced_offline(void);

/** Bekleme süresini sıfırla ve hemen yeniden dene. */
void net_service_reconnect_now(void);

#ifdef __cplusplus
}
#endif
