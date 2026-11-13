/**
 * @file dead_reckoning.cpp
 * @brief Ölü hesap matematiği (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Burada kayan nokta KULLANILIYOR**, telemetry_codec'in aksine. Oradaki
 *    yasak çıktı biçimlendirmesi içindi: gömülü printf'in kayan nokta desteği
 *    güvenilmez ve yuvarlama platforma göre değişiyor. Burada ise trigonometri
 *    var ve sonuç tam sayıya (1e-7 derece) yuvarlanarak çıkıyor; ara hesabın
 *    kayan nokta olması bir sorun değil.
 *
 *  - **Düz yüzey yaklaşımı.** Bir adımda alınan yol en fazla birkaç yüz metre;
 *    bu ölçekte enlem/boylam dönüşümü için küresel formüllere gerek yok.
 *    Boylamda enlem kosinüsü ile daraltma yapılıyor, bu yeterli.
 *
 *  - **Yön açısı yalnız jiroskoptan.** Hız vektöründen yön çıkarmak da mümkün
 *    ama araç duruyorken yön tanımsız kalıyor ve gürültü yönü rastgele
 *    çeviriyor. Jiroskop duruyorken de doğru davranıyor.
 */

#include "dead_reckoning.h"

#include <math.h>
#include <string.h>

/* Bir derece enlemin metre karşılığı. Boylamda enlem kosinüsüyle daralıyor. */
#define METERS_PER_DEG 111320.0

void dr_reset(dr_state_t* st)
{
    if (st) memset(st, 0, sizeof(*st));
}

void dr_seed(dr_state_t* st, int32_t lat_e7, int32_t lon_e7,
             uint16_t heading_deg_x10)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->valid           = true;
    st->lat_e7          = lat_e7;
    st->lon_e7          = lon_e7;
    st->heading_deg_x10 = (uint16_t)(heading_deg_x10 % 3600u);
    /* Tohumlama anında hata, uydu sabitlemesinin kendi hatası kadar.
     * Tipik açık hava değeri birkaç metre. */
    st->uncertainty_cm  = 500u;
}

void dr_step(dr_state_t* st, uint16_t speed_kmh_x10, int16_t yaw_rate_dps_x10,
             uint32_t dt_ms, bool have_gyro)
{
    if (!st || !st->valid || dt_ms == 0) return;

    const double dt_s = (double)dt_ms / 1000.0;

    /* ── Yön ── */
    if (have_gyro) {
        double dheading = ((double)yaw_rate_dps_x10 / 10.0) * dt_s;
        double h = (double)st->heading_deg_x10 / 10.0 + dheading;
        while (h < 0.0)      h += 360.0;
        while (h >= 360.0)   h -= 360.0;
        st->heading_deg_x10 = (uint16_t)(h * 10.0 + 0.5) % 3600u;
    }

    /* ── Mesafe ── */
    const double speed_ms = ((double)speed_kmh_x10 / 10.0) / 3.6;
    const double dist_m   = speed_ms * dt_s;

    if (dist_m > 0.0) {
        const double hrad = (double)st->heading_deg_x10 / 10.0 * M_PI / 180.0;
        const double dn   = dist_m * cos(hrad);   /* kuzey bileşeni */
        const double de   = dist_m * sin(hrad);   /* doğu bileşeni  */

        const double lat_deg = (double)st->lat_e7 / 1e7;
        double coslat = cos(lat_deg * M_PI / 180.0);
        /* Kutuplara yakın bölünme patlamasın; pratikte araç oralarda değil. */
        if (coslat < 0.01) coslat = 0.01;

        const double dlat = dn / METERS_PER_DEG;
        const double dlon = de / (METERS_PER_DEG * coslat);

        st->lat_e7 += (int32_t)lround(dlat * 1e7);
        st->lon_e7 += (int32_t)lround(dlon * 1e7);
    }

    st->distance_m += (uint32_t)(dist_m + 0.5);
    st->elapsed_ms += dt_ms;

    /* ── Belirsizlik ──
     * İki kaynak: mesafe ölçümünün yüzdesel hatası ve yön sapmasının yol
     * boyunca yarattığı yanal kayma. Jiroskop yoksa yön hatası çok daha
     * hızlı büyüyor, çünkü dönüşler hiç görülmüyor. */
    const double dist_err = dist_m * (double)DR_DISTANCE_ERROR_PERMILLE / 1000.0;

    double drift_dps = (double)DR_HEADING_DRIFT_DPS_X1000 / 1000.0;
    if (!have_gyro) drift_dps = 5.0;   /* körlemesine düz gidiyoruz varsayımı */

    const double heading_err_deg = drift_dps * dt_s;
    const double lateral_err     = dist_m * sin(heading_err_deg * M_PI / 180.0);

    /* Santimetre olarak biriktiriliyor: adım başına artış metrenin altında
     * kalıyor ve metre cinsinden tutulsaydı her adımda kırpılıp kaybolurdu. */
    double add_cm = (dist_err + fabs(lateral_err)) * 100.0;
    double u = (double)st->uncertainty_cm + add_cm;
    if (u > 4.0e9) u = 4.0e9;
    st->uncertainty_cm = (uint32_t)(u + 0.5);

    /* ── Geçerlilik sınırı ──
     * Aşırı sapmış bir tahmin, tahmin yokluğundan kötüdür (bkz. header). */
    if (st->uncertainty_cm > (uint32_t)DR_MAX_UNCERTAINTY_M * 100u ||
        st->elapsed_ms     > DR_MAX_ELAPSED_MS) {
        st->valid = false;
    }
}
