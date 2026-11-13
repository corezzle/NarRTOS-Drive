#pragma once
/**
 * @brief Konum birleştirme: uydu varsa uydudan, yoksa tahminden.
 *
 * Rapor üreticisi artık uydu alıcısına doğrudan bakmıyor; buraya soruyor.
 * Böylece "uydu mu tahmin mi" ayrımı tek yerde yönetiliyor.
 *
 * ── Nasıl karar veriyor ────────────────────────────────────────────────────
 *
 *   Uydu sabitlemesi taze mi?
 *     evet -> konumu ondan al, tahmini bu noktadan yeniden başlat
 *     hayır -> tahmini bir adım ilerlet (aracın hızı + jiroskop)
 *              tahmin de geçersizse konum YOK
 *
 * ── Neden yeniden tohumlama önemli ─────────────────────────────────────────
 *
 * Uydu her geldiğinde tahmin sıfırlanıyor. Böylece birikmiş hata atılıyor ve
 * bir sonraki tünelde temiz bir noktadan başlanıyor. Tahmini sürekli
 * yürütmek, kısa uydu kesintilerinde bile hatanın kalıcı olarak birikmesine
 * yol açardı.
 *
 * ── Jiroskop sapması duruşta ölçülüyor ─────────────────────────────────────
 *
 * MEMS jiroskoplar hareketsizken bile sıfırdan farklı okuyor. Bu sapma
 * düzeltilmezse duran araçta bile yön kayıyor ve tünele girildiği anda
 * tahmin yanlış yöne gidiyor. Araç durduğunda (hız sıfır) okuma ortalaması
 * alınıp sapma olarak çıkarılıyor.
 */

#include "dead_reckoning.h"

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POS_NONE = 0,   /**< konum bilinmiyor */
    POS_GNSS,       /**< uydudan ölçüldü */
    POS_DR,         /**< tahmin edildi (ölü hesap) */
} pos_source_t;

typedef struct {
    pos_source_t source;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint16_t speed_kmh_x10;
    uint16_t heading_deg_x10;
    uint8_t  sats;              /**< yalnız uydu kaynağında anlamlı */
    uint16_t uncertainty_m;     /**< uydu için küçük, tahmin için büyüyen */
} position_t;

typedef struct {
    pos_source_t source;
    uint32_t gnss_fixes;        /**< uydudan konum alınan tur sayısı */
    uint32_t dr_steps;          /**< tahminle ilerlenen tur sayısı */
    uint32_t dr_sessions;       /**< kaç kez tahmine düşüldü */
    uint32_t dr_expired;        /**< kaç kez tahmin sınırı aşıp düştü */
    bool     have_can_speed;    /**< araç hız verisi geliyor mu */
    bool     have_gyro;
    int16_t  gyro_bias_dps_x10;
    uint16_t uncertainty_m;
    uint32_t dr_distance_m;
} position_stats_t;

/** gnss_service ve can_service'ten SONRA çağrılmalı. */
esp_err_t position_service_init(void);

/** @return false ise konum bilinmiyor; çağıran 0,0 göndermeli */
bool position_service_get(position_t* out);

void position_service_stats(position_stats_t* out);
const char* position_source_name(pos_source_t s);

#ifdef __cplusplus
}
#endif
