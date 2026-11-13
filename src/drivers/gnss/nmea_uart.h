#pragma once
/**
 * @brief NMEA alıcısı UART sürücüsü — satırları gnss_service'e taşır.
 *
 * twai_driver ile aynı kalıp: ENABLE_GNSS=0 iken dosya boş çevrilir ve
 * ESP_ERR_NOT_SUPPORTED dönen saplamalar kalır. Servis donanımdan bağımsız
 * olduğu için modül yokken `gnss inject` ile beslenir.
 *
 * Sürücü yalnız OKUR. Alıcıya yapılandırma cümlesi (PMTK/UBX) göndermek
 * bilinçli olarak yok: modül elimizde değil, hangi protokolü konuştuğu belli
 * değil ve varsayılan NMEA çıktısı ihtiyacımızın tamamını karşılıyor.
 */

#include "../../core/os_config.h"

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UART'ı kur ve okuyucu görevini başlat.
 *  @return ESP_ERR_NOT_SUPPORTED — ENABLE_GNSS=0 ile derlendiyse */
esp_err_t gnss_uart_start(uint32_t baud);

esp_err_t gnss_uart_stop(void);
bool      gnss_uart_running(void);

#ifdef __cplusplus
}
#endif
