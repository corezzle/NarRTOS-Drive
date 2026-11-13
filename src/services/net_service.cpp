/**
 * @file net_service.cpp
 * @brief Bağlantı durum makinesi (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Arka uç TEMBEL kurulur.** SSID/APN girilmemiş bir cihazda WiFi yığınını
 *    ya da modemi hiç ayağa kaldırmıyoruz. Ekransız cihaz kurulum masasında
 *    saatlerce ayarsız bekleyebilir; boşa akım ve boşa ısı.
 *
 *  - **Üstel bekleme.** Sabit aralık araçta yanlış: tünelde ya da kırsalda
 *    saatlerce kapsama olmayabilir, saniyede bir modem kaydı denemek hem akım
 *    hem hücre trafiği. 5 sn'den 5 dk'ya kadar ikiye katlanıyor; bağlantı
 *    kurulunca taban değere dönüyor.
 *
 *  - **connect() BLOKLAYICI ve bu görevde çalışıyor.** Çekirdek 0 / öncelik 4
 *    (NET_TASK_*); CAN yakalama çekirdek 1 / öncelik 6'da olduğu için modem AT
 *    komutu 20 sn bloklasa bile frame kaçmıyor. M0'daki görev dağılımı kararı
 *    tam bunun içindi.
 *
 *  - Durum değişimi geri çağrıyla bildiriliyor, event_bus ile değil. M3'te
 *    toplam iki üretici-tüketici ilişkisi var (net -> mqtt, mqtt -> store).
 *    Araya pub/sub koymak henüz kazanç değil; ilişki sayısı artınca gelecek.
 */

#include "net_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "../drivers/net/net_backend.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "net";

/* Durum makinesi tik aralığı. connect() zaten bloklayıcı; bu değer yalnız
 * "bağlantı koptu mu" kontrolünün gecikmesini belirliyor. */
#define NET_TICK_MS 500

static const net_backend_t* s_be        = NULL;
static bool                 s_be_inited = false;
static net_state_t          s_state     = NET_STATE_DISABLED;
static bool                 s_forced    = false;
static uint32_t             s_backoff   = NET_RETRY_MIN_MS;
static int64_t              s_next_try  = 0;      /* ms, esp_timer tabanlı */
/* Sabit yuva: dinamik liste yönetmek bu boyutta kazanç değil, ayrıca
 * abonelik yalnızca boot sırasında yapılıyor. */
#define NET_STATE_CB_MAX 4
static struct { net_state_cb_t fn; void* ctx; } s_cbs[NET_STATE_CB_MAX];
static uint8_t s_cb_count = 0;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

const char* net_service_state_name(net_state_t s)
{
    switch (s) {
        case NET_STATE_DISABLED:     return "kapalı";
        case NET_STATE_UNCONFIGURED: return "ayarsız";
        case NET_STATE_OFFLINE:      return "çevrimdışı";
        case NET_STATE_ONLINE:       return "çevrimiçi";
        default:                     return "?";
    }
}

const char* net_service_backend_name(void) { return s_be ? s_be->name : "none"; }

net_state_t net_service_state(void)  { return s_state; }
bool net_service_is_online(void)     { return s_state == NET_STATE_ONLINE; }
bool net_service_forced_offline(void){ return s_forced; }

uint32_t net_service_retry_in_ms(void)
{
    int64_t d = s_next_try - now_ms();
    return (d > 0) ? (uint32_t)d : 0;
}

esp_err_t net_service_add_state_cb(net_state_cb_t cb, void* ctx)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (s_cb_count >= NET_STATE_CB_MAX) {
        ESP_LOGE(TAG, "durum abonesi yuvası dolu (%d)", NET_STATE_CB_MAX);
        return ESP_ERR_NO_MEM;
    }
    s_cbs[s_cb_count].fn  = cb;
    s_cbs[s_cb_count].ctx = ctx;
    s_cb_count++;
    return ESP_OK;
}

static void set_state(net_state_t s)
{
    if (s == s_state) return;
    ESP_LOGI(TAG, "%s -> %s", net_service_state_name(s_state),
             net_service_state_name(s));
    s_state = s;
    for (uint8_t i = 0; i < s_cb_count; i++) s_cbs[i].fn(s, s_cbs[i].ctx);
}

void net_service_force_offline(bool off)
{
    s_forced = off;
    ESP_LOGW(TAG, "zorlanmış çevrimdışı: %s", off ? "AÇIK" : "kapalı");
    if (!off) net_service_reconnect_now();
}

void net_service_reconnect_now(void)
{
    s_backoff  = NET_RETRY_MIN_MS;
    s_next_try = 0;
}

/* ── Arka uç seçimi ─────────────────────────────────────────────────────── */
static const net_backend_t* pick_backend(void)
{
    char want[16] = {0};
    config_store_get_str(NVS_KEY_NET_BACKEND, want, sizeof(want), "");

    if (strcasecmp(want, "none") == 0) return NULL;
    if (strcasecmp(want, "wifi") == 0) return net_backend_wifi();
    if (strcasecmp(want, "modem") == 0) return net_backend_modem();

    /* Ayar yok ya da tanınmıyor: elde olanı seç, modemi yeğle. Modem geldiğinde
     * bu satır sahadaki varsayılanı kendiliğinden doğru yapar. */
    const net_backend_t* be = net_backend_modem();
    if (!be) be = net_backend_wifi();
    return be;
}

/* ── Durum makinesi ─────────────────────────────────────────────────────── */
static void tick(void)
{
    if (!s_be) { set_state(NET_STATE_DISABLED); return; }

    if (s_forced) {
        /* Kapsama kaybı taklidi. Bağlıysak kopar — store&forward'ın gerçekten
         * devreye girdiği görülsün. */
        if (s_be->is_connected()) s_be->disconnect();
        set_state(NET_STATE_OFFLINE);
        return;
    }

    if (!s_be->is_configured()) { set_state(NET_STATE_UNCONFIGURED); return; }

    if (s_be_inited && s_be->is_connected()) {
        set_state(NET_STATE_ONLINE);
        s_backoff = NET_RETRY_MIN_MS;
        return;
    }

    set_state(NET_STATE_OFFLINE);
    if (now_ms() < s_next_try) return;

    if (!s_be_inited) {
        /* Tembel kurulum: ayar girilmemiş cihazda yığın hiç ayağa kalkmasın. */
        esp_err_t err = s_be->init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s arka ucu kurulamadı: %s", s_be->name,
                     esp_err_to_name(err));
            s_next_try = now_ms() + s_backoff;
            s_backoff  = (s_backoff * 2 > NET_RETRY_MAX_MS) ? NET_RETRY_MAX_MS
                                                           : s_backoff * 2;
            return;
        }
        s_be_inited = true;
    }

    esp_err_t err = s_be->connect(NET_CONNECT_TIMEOUT_MS);
    if (err == ESP_OK) {
        set_state(NET_STATE_ONLINE);
        s_backoff = NET_RETRY_MIN_MS;
        return;
    }

    ESP_LOGW(TAG, "bağlanamadı (%s), %lu ms sonra tekrar",
             esp_err_to_name(err), (unsigned long)s_backoff);
    s_next_try = now_ms() + s_backoff;
    s_backoff  = (s_backoff * 2 > NET_RETRY_MAX_MS) ? NET_RETRY_MAX_MS
                                                    : s_backoff * 2;
}

static void net_task(void* arg)
{
    (void)arg;
    for (;;) {
        tick();
        vTaskDelay(pdMS_TO_TICKS(NET_TICK_MS));
    }
}

esp_err_t net_service_init(void)
{
    s_be = pick_backend();
    if (!s_be) {
        ESP_LOGW(TAG, "ağ arka ucu yok (ENABLE_WIFI=%d ENABLE_MODEM=%d) — "
                      "store&forward tamponu birikir",
                 ENABLE_WIFI, ENABLE_MODEM);
        s_state = NET_STATE_DISABLED;
        return ESP_OK;   /* hata değil: donanımsız/ağsız çalışmak geçerli bir hâl */
    }

    BaseType_t r = xTaskCreatePinnedToCore(net_task, "net_svc", 4096, NULL,
                                           NET_TASK_PRIORITY, NULL,
                                           NET_TASK_CORE);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "görev oluşturulamadı");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "init OK (arka uç: %s, deneme %lu-%lu ms)", s_be->name,
             (unsigned long)NET_RETRY_MIN_MS, (unsigned long)NET_RETRY_MAX_MS);
    return ESP_OK;
}
