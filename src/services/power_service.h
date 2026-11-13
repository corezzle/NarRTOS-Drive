#pragma once
/**
 * @brief Güç yönetimi: kontak kapanınca derin uyku (M10).
 *
 * Karar power_policy'de (saf, test edilen); bu servis girdileri toplar ve
 * kararı uygular. Uyanma iki yoldan:
 *
 *   kontak      cihaz kontak pini yükselince ANINDA uyanır (araç çalıştı)
 *   zamanlayıcı park raporu periyodunda uyanıp bir rapor üretir, işi
 *               bitince tekrar uyur
 *
 * Derin uyku YENİDEN AÇILIŞTIR: RAM silinir, servisler baştan kurulur.
 * Kalıcı olan üç şey yeter: tampon (flash), ayarlar (NVS) ve saat (RTC
 * derin uykuda saymaya devam eder, bkz. time_service).
 *
 * Varsayılan KAPALI (`set power sleep 1` ile açılır): masada uyuyakalan
 * cihazın USB konsolu ölür ve geliştirici cihazı "bozuldu" sanır.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool        enabled;        /**< NVS ayarı */
    const char* why;            /**< son kararın gerekçesi (statik metin) */
    const char* wake_cause;     /**< bu açılışın sebebi (statik metin) */
    uint32_t    sleep_cycles;   /**< toplam uyku sayısı (RTC, uykuda korunur) */
    int64_t     ign_off_ms;     /**< kontak ne zamandır kapalı; açıksa -1 */
    bool        timer_wake;     /**< bu açılış zamanlayıcı uyanışı mı */
} power_stats_t;

/** Diğer servislerden SONRA çağrılmalı: kararı onların durumuyla veriyor,
 *  zamanlayıcı uyanışında da hemen bir rapor ürettiriyor. */
esp_err_t power_service_init(void);

void power_service_stats(power_stats_t* out);

/** Politikayı atlayıp düzgünce uyu (test/komut). Geri dönmez. */
void power_service_sleep_now(void);

#ifdef __cplusplus
}
#endif
