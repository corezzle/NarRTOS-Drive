/**
 * @file power_policy.cpp
 * @brief Uyku kararı (bkz. header).
 *
 * Karar sırası önemli ve bilinçli:
 *
 *  - OTA denetimleri bekleme süresinden SONRA değil ÖNCE geliyor deme
 *    tuzağına düşülmedi: kontak açıkken ve bekleme dolmadan zaten uyunmuyor,
 *    OTA ancak ondan sonra söz sahibi.
 *  - "Güncelleme hazır ama ağ yok" uyumayı ENGELLEMEZ. Ağ olmadan indirme
 *    olmaz; garaj altında güncelleme bekleyen cihaz aküyü bitirirdi.
 *  - "Tampon dolu ama ağ yok" da engellemez, aynı sebeple. Kayıtlar kalıcı
 *    bellekte; sonraki uyanışta ağ varsa gider.
 */

#include "power_policy.h"

#include <stddef.h>

power_action_t power_policy_eval(const power_inputs_t* in, const char** why)
{
    const char* w = "?";
    power_action_t act = POWER_STAY_AWAKE;

    if (!in) {
        if (why) *why = "girdi yok";
        return POWER_STAY_AWAKE;
    }

    if (!in->enabled) {
        w = "uyku kapalı (set power sleep 1 ile açılır)";
    } else if (in->ignition) {
        w = "kontak açık";
    } else if (in->ign_off_ms < in->sleep_delay_ms) {
        w = "kontak yeni kapandı, bekleme süresi doluyor";
    } else if (in->ota_busy) {
        /* İndirme yarıda kesilirse yeniden başlar (A/B bölüm güvenli) ama
         * her kesinti bant ve zaman israfı; bitmesini beklemek ucuz. */
        w = "güncelleme işi sürüyor";
    } else if (in->ota_waiting_install && in->net_online) {
        /* Park hâli tam da kurulum penceresi; uyursak pencereyi kaçırırız. */
        w = "güncelleme kurulumu bekleniyor (park penceresi)";
    } else if (in->pending_records > 0 && in->net_online &&
               in->ign_off_ms < in->max_awake_ms) {
        w = "tampon boşalıyor";
    } else {
        act = POWER_GO_SLEEP;
        if (in->pending_records == 0)      w = "iş kalmadı";
        else if (!in->net_online)          w = "ağ yok, tampon kalıcı bellekte bekler";
        else                               w = "uyanık kalma tavanı doldu";
    }

    if (why) *why = w;
    return act;
}
