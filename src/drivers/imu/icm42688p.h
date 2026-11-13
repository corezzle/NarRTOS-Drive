#pragma once
/**
 * @brief ICM-42688-P jiroskop sürücüsü. Ölü hesabın yön kaynağı.
 *
 * Sensörden YALNIZCA Z ekseni dönüş hızı okunuyor. İvme ölçümü konum için
 * kullanılmıyor (gerekçe dead_reckoning.h'de: ivmeyi iki kez toplamak hatayı
 * zamanın karesiyle büyütüyor), yalnız aracın hareket edip etmediğini ayırt
 * etmek için okunuyor.
 *
 * twai_driver ve nmea_uart ile aynı kalıp: ENABLE_IMU=0 iken dosya boş
 * çevriliyor ve ESP_ERR_NOT_SUPPORTED dönen saplamalar kalıyor. Sensör
 * yokken ölü hesap jiroskopsuz çalışıyor, yalnız belirsizliği daha hızlı
 * büyüyor.
 *
 * ⚠ DONANIM YOK. Bu dosya yalnız derleniyor; hiçbir satırı gerçek sensörle
 * çalıştırılmadı.
 */

#include "../../core/os_config.h"

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** I2C'yi kur ve sensörü uyandır. WHO_AM_I doğrulanır. */
esp_err_t imu_start(void);

esp_err_t imu_stop(void);
bool      imu_running(void);

/**
 * @brief Z ekseni dönüş hızı, saniyede derecenin 10 katı.
 *
 * Sağa dönüş pozitif. Sensörün kart üzerindeki yönelimi ters ise
 * IMU_YAW_INVERT ile çevrilir.
 *
 * @return sensör yoksa ya da okuma başarısızsa false
 */
bool imu_read_yaw_rate(int16_t* out_dps_x10);

/** Ham ivme büyüklüğü (mg). Hareket/duruş ayrımı için. */
bool imu_read_accel_mag(uint16_t* out_mg);

#ifdef __cplusplus
}
#endif
