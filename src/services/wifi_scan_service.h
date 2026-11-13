#pragma once
/**
 * @brief WiFi tarama politikası ve önbelleği (M9).
 *
 * Sürücü (wifi_scan.h) donanımı bilir; bu servis NE ZAMAN taranacağını
 * bilir. Politika:
 *
 *   konum uydudan geliyor  -> taranmaz (katma değeri yok, güç israfı)
 *   tahmin ya da konum yok -> WIFI_SCAN_INTERVAL_MS aralıkla taranır
 *
 * Son liste burada tutulur; rapor üreticisi listeyi buradan okur ve
 * WIFI_SCAN_MAX_AGE_MS'ten eskiyse rapora KOYMAZ. Bayat liste, GNSS'teki
 * bayat sabitleme kuralının aynısı: araç ilerlemişken iki dakika önceki
 * otoparkın ağlarını "şu an buradayım" diye göndermek yanıltır.
 *
 * `wifi inject` ile donanımsız test edilebilir; can inject / gnss inject
 * kalıbının aynısı.
 */

#include "../drivers/net/wifi_scan.h"

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t scans_ok;
    uint32_t scans_failed;
    uint32_t injects;
    uint8_t  last_count;   /**< son taramadaki ağ sayısı */
    uint32_t age_ms;       /**< son taramanın yaşı; hiç tarama yoksa UINT32_MAX */
    bool     hw;           /**< gerçek tarama derlemede var mı (ENABLE_WIFI) */
} wifi_scan_stats_t;

/** position_service'ten SONRA çağrılmalı (politika konum kaynağına bakıyor). */
esp_err_t wifi_scan_service_init(void);

/** Son listeyi kopyalar. age_ms NULL olabilir.
 *  @return ağ sayısı; hiç tarama yapılmadıysa 0. */
size_t wifi_scan_service_get(wifi_scan_ap_t* out, size_t max, uint32_t* age_ms);

/** Politikayı beklemeden hemen tara (komut için). BLOKLAYICI (~1-2 sn). */
esp_err_t wifi_scan_service_scan_now(void);

/** Test: listeye elle AP ekle, yaş sıfırlanır. Liste doluysa sonuncusu düşer. */
void wifi_scan_service_inject(const uint8_t mac[6], int8_t rssi);
void wifi_scan_service_clear(void);

void wifi_scan_service_stats(wifi_scan_stats_t* out);

#ifdef __cplusplus
}
#endif
