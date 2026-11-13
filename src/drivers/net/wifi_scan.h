#pragma once
/**
 * @brief WiFi BSSID taraması: konumlama için ham veri.
 *
 * Bağlantıyla ilgisi yok. Çevredeki erişim noktalarının MAC + sinyal gücü
 * listesini verir; liste rapora eklenir ve konumu SUNUCU çözer (cihaz
 * çözmez, CAN'deki kalıbın aynısı). Wear'ın wifi_service_scan_bssids'inden
 * uyarlandı.
 *
 * Bağlanmış olmak gerekmez: radyo kapalıysa STA modunda ayağa kaldırılır ve
 * bağlanma DENENMEZ. Net arka ucu WiFi ise ve o an bağlanma denemesi
 * sürüyorsa sürücü meşgul dönebilir; çağıran sonraki turda yeniden dener.
 */
#include "../../core/os_config.h"

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bssid[6];   /**< AP MAC */
    int8_t  rssi;
} wifi_scan_ap_t;

/** BLOKLAYICI aktif tarama (~1-2 sn); düşük öncelikli görevden çağrılmalı.
 *  ENABLE_WIFI=0 iken ESP_ERR_NOT_SUPPORTED. */
esp_err_t wifi_scan_bssids(wifi_scan_ap_t* out, uint8_t max, uint8_t* out_count);

#ifdef __cplusplus
}
#endif
