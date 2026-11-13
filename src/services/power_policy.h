#pragma once
/**
 * @brief Uyku kararı: cihaz şimdi derin uykuya geçmeli mi?
 *
 * Saf modül. Donanım, ağ, FreeRTOS bilmiyor; host'ta baştan sona sınanıyor.
 * power_service her turda girdileri toplayıp buraya soruyor; uyutma işinin
 * kendisi (zamanlayıcı, kontak pini, derin uyku çağrısı) serviste.
 *
 * ── Neden uyuyoruz ─────────────────────────────────────────────────────────
 *
 * Kontak kapalıyken cihaz tam güçte kalırsa aracın aküsünden beslenmeye
 * devam eder. Uzun park (havalimanı, servis) akü bitirir; sahada bunun adı
 * "cihaz aracı çalışmaz yaptı"dır ve ürünü bitirir. Derin uykuda tüketim
 * binlerce kat düşüyor.
 *
 * ── Neden hemen uyumuyoruz ─────────────────────────────────────────────────
 *
 * Kontak kapandığı anda uyumak üç işi yarıda keserdi:
 *   1. kontak kapandı raporu ve tamponda bekleyenlerin gönderimi
 *   2. park penceresini bekleyen yazılım güncellemesi
 *   3. kısa duraklamalar (trafik, market) için gereksiz uyu/uyan döngüsü
 *
 * ── Bilinen sınır ──────────────────────────────────────────────────────────
 *
 * Damgasız kayıtlar uykuda ölür: saat hiç öğrenilmemişse tampondaki
 * kayıtlar bir sonraki açılışta damgalanamaz ve düşer (bkz. telemetry_codec
 * damga kuralları). Uyku bunu YARATMIYOR, kesinleştiriyor: o kayıtlar zaten
 * herhangi bir yeniden başlatmada ölecekti. Saat gelmeden uyumamak ise
 * garajda unutulan aracın aküsünü bitirirdi; akü kayıttan kıymetli.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_STAY_AWAKE = 0,
    POWER_GO_SLEEP,
} power_action_t;

typedef struct {
    bool     enabled;             /**< `set power sleep 1` yapıldı mı */
    bool     ignition;            /**< kontak (elle sürme dahil, etkin değer) */
    bool     net_online;
    bool     ota_busy;            /**< indiriyor / yazdı, kesilmemeli */
    bool     ota_waiting_install; /**< güncelleme hazır, park penceresi bekliyor */
    uint32_t ign_off_ms;          /**< kontağın kapalı kaldığı süre */
    uint32_t pending_records;     /**< tamponda gönderilmeyi bekleyen kayıt */
    uint32_t sleep_delay_ms;      /**< kontak kapandıktan uykuya kadar bekleme */
    uint32_t max_awake_ms;        /**< tampon boşalmıyorsa bile uyku tavanı */
} power_inputs_t;

/**
 * @param why NULL olabilir; değilse kararın tek satırlık gerekçesi yazılır
 *            (statik metin, serbest bırakılmaz). `power status` bunu basıyor.
 */
power_action_t power_policy_eval(const power_inputs_t* in, const char** why);

#ifdef __cplusplus
}
#endif
