#pragma once
/**
 * @brief Ağ arka ucu arayüzü — net_service'in altına takılan taşıyıcı.
 *
 * İki arka uç planlı:
 *   wifi   atölye/geliştirme bağlantısı. Bugün çalışıyor.
 *   modem  4G (SIM7600 / EC200 sınıfı). SAHADAKİ gerçek yol; donanım yok, M7.
 *
 * Arka uç YALNIZCA "bağlantı var mı" sorusundan sorumlu. Yeniden deneme
 * politikası (üstel bekleme, zaman aşımı) net_service'te — Wear'daki
 * wifi_service kendi içinde 5 kez deniyordu, o mantık modem gelince ikinci kez
 * yazılmak zorunda kalırdı. Politika tek yerde durursa iki arka uç da aynı
 * davranışı miras alır.
 *
 * `is_configured()` ayrı bir soru: ayarı eksik cihazı sürekli bağlanmayı
 * denemekten korur. Ekransız cihazda "neden bağlanmıyor" sorusunun cevabı
 * `net status` çıktısında görünmeli, boşa dönen bir döngüde değil.
 *
 * Derleme dışı bırakılan arka ucun fabrikası NULL döner; net_service bunu
 * "yok" olarak yorumlar, hata değil.
 */

/* ENABLE_WIFI / ENABLE_MODEM için: arka uç dosyaları bu başlığı dahil edip
 * hemen #if ile dallanıyor, bayrak o noktada tanımlı olmalı. */
#include "../../core/os_config.h"

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    /** Donanımı/yığını hazırla. Bağlanmayı denemez. */
    esp_err_t (*init)(void);
    /** BLOKLAYICI bağlanma denemesi. net_service görevinden çağrılır. */
    esp_err_t (*connect)(uint32_t timeout_ms);
    void      (*disconnect)(void);
    bool      (*is_connected)(void);
    /** Gerekli ayarlar (SSID / APN) var mı? */
    bool      (*is_configured)(void);
} net_backend_t;

/** Derleme dışıysa NULL. */
const net_backend_t* net_backend_wifi(void);
const net_backend_t* net_backend_modem(void);

/* ── Modem teşhisi ───────────────────────────────────────────────────────────
 * Ekransız cihazda "modem bağlanmıyor" tek başına işe yaramaz: SIM mi yok,
 * kapsama mı yok, APN mi yanlış? Kurulumcunun bunu ayırt etmesi gerekiyor.
 * ENABLE_MODEM=0 iken zararsız saplamalar. */
int         modem_backend_csq(void);        /**< 0-31, -1 = bilinmiyor */
const char* modem_backend_imei(void);       /**< modemden okunan */
const char* modem_backend_step(void);       /**< açılış sırasında nerede */
const char* modem_backend_last_error(void);

#ifdef __cplusplus
}
#endif
