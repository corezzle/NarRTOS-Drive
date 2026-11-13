/**
 * @file test_power_policy.cpp
 * @brief Uyku kararını Mac üzerinde sınar.
 *
 * GERÇEK power_policy.cpp derleniyor. Bu kararın yanlış olması iki uçta da
 * pahalı: gereksiz uyanık kalmak aracın aküsünü bitirir, erken uyumak
 * raporları ve güncellemeyi yarıda keser. İki yönü de sınıyoruz.
 *
 * Çalıştırma:  tools/host-test/run.sh
 */

#include "../../src/services/power_policy.h"

#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/** Tipik park hâli: uyku açık, kontak kapalı, bekleme dolmuş, iş yok. */
static power_inputs_t parked(void)
{
    power_inputs_t in = {};
    in.enabled         = true;
    in.ignition        = false;
    in.net_online      = true;
    in.ign_off_ms      = 10 * 60 * 1000;
    in.pending_records = 0;
    in.sleep_delay_ms  = 5 * 60 * 1000;
    in.max_awake_ms    = 15 * 60 * 1000;
    return in;
}

int main(void)
{
    printf("power_policy testi\n");

    printf("\n[1] uyumaya engel hâller\n");
    {
        power_inputs_t in = parked();
        in.enabled = false;
        const char* why = NULL;
        check(power_policy_eval(&in, &why) == POWER_STAY_AWAKE, "uyku kapalıyken uyumaz");
        check(why && strstr(why, "kapalı") != NULL, "gerekçe uyku ayarını söylüyor");
    }
    {
        power_inputs_t in = parked();
        in.ignition = true;
        check(power_policy_eval(&in, NULL) == POWER_STAY_AWAKE, "kontak açıkken uyumaz");
    }
    {
        power_inputs_t in = parked();
        in.ign_off_ms = 60 * 1000;   /* bekleme 5 dk, daha 1 dk oldu */
        check(power_policy_eval(&in, NULL) == POWER_STAY_AWAKE,
              "kısa duraklamada uyumaz (market, trafik)");
    }
    {
        power_inputs_t in = parked();
        in.ota_busy = true;
        check(power_policy_eval(&in, NULL) == POWER_STAY_AWAKE,
              "güncelleme inerken uyumaz");
    }
    {
        power_inputs_t in = parked();
        in.ota_waiting_install = true;
        check(power_policy_eval(&in, NULL) == POWER_STAY_AWAKE,
              "kurulum penceresi beklenirken uyumaz (ağ varsa)");
    }
    {
        power_inputs_t in = parked();
        in.pending_records = 12;
        check(power_policy_eval(&in, NULL) == POWER_STAY_AWAKE,
              "tampon boşalırken uyumaz (ağ varsa, tavan dolmadıysa)");
    }

    printf("\n[2] uyunması gereken hâller\n");
    {
        power_inputs_t in = parked();
        const char* why = NULL;
        check(power_policy_eval(&in, &why) == POWER_GO_SLEEP, "iş yoksa uyur");
        check(why && strstr(why, "iş kalmadı") != NULL, "gerekçe doğru");
    }
    {
        /* Akü kayıttan kıymetli: ağ yoksa tampon dolu diye uyanık kalınmaz. */
        power_inputs_t in = parked();
        in.pending_records = 40;
        in.net_online = false;
        const char* why = NULL;
        check(power_policy_eval(&in, &why) == POWER_GO_SLEEP,
              "ağ yokken tampon dolu olsa da uyur");
        check(why && strstr(why, "kalıcı bellekte") != NULL,
              "gerekçe kaydın kaybolmadığını söylüyor");
    }
    {
        power_inputs_t in = parked();
        in.ota_waiting_install = true;
        in.net_online = false;
        check(power_policy_eval(&in, NULL) == POWER_GO_SLEEP,
              "güncelleme hazır ama ağ yoksa uyur (garaj altı aküyü bitirmesin)");
    }
    {
        /* Gönderilemeyen tampon sonsuza dek uyutmasın: tavan var. */
        power_inputs_t in = parked();
        in.pending_records = 40;
        in.ign_off_ms = in.max_awake_ms;
        const char* why = NULL;
        check(power_policy_eval(&in, &why) == POWER_GO_SLEEP,
              "tampon boşalmıyorsa tavanda uyur");
        check(why && strstr(why, "tavan") != NULL, "gerekçe tavanı söylüyor");
    }

    printf("\n[3] kenar durumlar\n");
    {
        check(power_policy_eval(NULL, NULL) == POWER_STAY_AWAKE,
              "girdi yoksa güvenli taraf: uyumaz");
    }
    {
        /* Bekleme tam sınırda: >= uyur (delay dolmuş sayılır). */
        power_inputs_t in = parked();
        in.ign_off_ms = in.sleep_delay_ms;
        check(power_policy_eval(&in, NULL) == POWER_GO_SLEEP, "bekleme tam dolunca uyur");
    }

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
