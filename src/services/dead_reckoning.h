#pragma once
/**
 * @brief Ölü hesap: uydu kesildiğinde konumu tahmin ederek sürdürme.
 *
 * Saf modül. Donanım, ağ, FreeRTOS bilmiyor; host'ta baştan sona sınanıyor.
 *
 * ── Nasıl çalışıyor ────────────────────────────────────────────────────────
 *
 * Son bilinen uydu konumundan başlanıyor. Her adımda araç ne kadar yol aldı
 * (aracın kendi hız verisinden) ve ne kadar döndü (jiroskoptan) bilgisi
 * ekleniyor. Konum bu iki bilgiyle yürütülüyor.
 *
 * ── Neden ivmeölçer DEĞİL ──────────────────────────────────────────────────
 *
 * İvmeölçerden konuma gitmek ivmeyi İKİ KEZ toplamayı gerektiriyor. Sensörün
 * küçük ve kaçınılmaz sapması bu işlemde zamanın KARESİYLE büyüyor: tipik bir
 * MEMS ivmeölçerde yarım dakikada onlarca metre, birkaç dakikada yüzlerce
 * metre hata birikiyor. Tünel geçişi bu yöntemle takip edilemez.
 *
 * Mesafe bu yüzden aracın kendi tekerlek ölçümünden alınıyor; hatası mesafeyle
 * DOĞRUSAL büyüyor ve tipik olarak %2-3 civarında kalıyor. Jiroskop yalnız
 * yön değişimi için kullanılıyor, ki orada da tek kez toplama var.
 *
 * ── Belirsizlik takip ediliyor ve sınırı var ───────────────────────────────
 *
 * Her adımda tahminin hata yarıçapı da büyütülüyor. Belirsizlik eşiği aşınca
 * ya da süre dolunca tahmin GEÇERSİZ sayılıyor.
 *
 * Bu, GNSS'teki "bayat sabitleme, sabitleme yokluğundan kötüdür" kuralının
 * aynısı: kilometrelerce sapmış bir tahmini "konum" diye göndermek, konum
 * göndermemekten kötüdür. Sunucu ikisini ayırt edemez ve güzergâh sessizce
 * bozulur.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint16_t heading_deg_x10;   /**< kuzeyden saat yönünde, 0-3599 */
    uint32_t distance_m;        /**< tohumlamadan beri alınan yol */
    uint32_t elapsed_ms;
    /** Tahmini hata yarıçapı, SANTİMETRE.
     *
     *  Metre yerine santimetre tutuluyor çünkü büyüme adım başına metrenin
     *  altında kalıyor (90 km/sa'te saniyede ~0,75 m). Metre olarak tam sayı
     *  tutulsaydı her adımda aşağı kırpılır ve belirsizlik HİÇ büyümezdi;
     *  aşırı sapmış tahmini geçersiz kılan koruma da sessizce çalışmazdı. */
    uint32_t uncertainty_cm;
} dr_state_t;

/** Metre cinsinden belirsizlik (rapor ve teşhis için). */
static inline uint16_t dr_uncertainty_m(const dr_state_t* st)
{
    if (!st) return 0;
    uint32_t m = st->uncertainty_cm / 100u;
    return (m > 65535u) ? 65535u : (uint16_t)m;
}

/** Mesafe hatası payı: tekerlek ölçümü lastik aşınması ve basıncına göre
 *  birkaç yüzde sapıyor. Binde cinsinden (30 = %3). */
#define DR_DISTANCE_ERROR_PERMILLE 30u

/** Jiroskop sapmasının yön hatasına katkısı, saniyede derece cinsinden
 *  binde. Yön hatası yanal sapmaya dönüşüyor. */
#define DR_HEADING_DRIFT_DPS_X1000 200u   /* 0,2 derece/sn */

/** Bu eşikleri aşan tahmin geçersiz sayılır. */
#ifndef DR_MAX_UNCERTAINTY_M
#define DR_MAX_UNCERTAINTY_M 500u
#endif
#ifndef DR_MAX_ELAPSED_MS
#define DR_MAX_ELAPSED_MS (10u * 60u * 1000u)
#endif

/** Bilinen bir konumdan başlat. Uydu sabitlemesi her geldiğinde çağrılıyor. */
void dr_seed(dr_state_t* st, int32_t lat_e7, int32_t lon_e7,
             uint16_t heading_deg_x10);

/** Tahmini geçersiz kıl (konum bilinmiyor hâline dön). */
void dr_reset(dr_state_t* st);

/**
 * @brief Bir adım ilerlet.
 *
 * @param speed_kmh_x10  aracın hızı (CAN'den)
 * @param yaw_rate_dps_x10  dönüş hızı, saniyede derecenin 10 katı; sağa
 *                          dönüş pozitif. Jiroskop yoksa 0 verilebilir,
 *                          o zaman yön sabit varsayılır ve belirsizlik
 *                          daha hızlı büyür.
 * @param dt_ms          bir önceki adımdan beri geçen süre
 * @param have_gyro      jiroskop verisi gerçek mi
 */
void dr_step(dr_state_t* st, uint16_t speed_kmh_x10, int16_t yaw_rate_dps_x10,
             uint32_t dt_ms, bool have_gyro);

#ifdef __cplusplus
}
#endif
