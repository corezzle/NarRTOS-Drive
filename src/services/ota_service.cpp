/**
 * @file ota_service.cpp
 * @brief OTA — manifest kontrolü, kurulum ve geri alma doğrulaması.
 *
 * Tasarım notları:
 *
 *  - **Tek görev, durum makinesi.** Wear'da check ve install ayrı task
 *    spawn ediyordu (UI'dan tetiklendikleri için). Drive'da tetikleyen bir
 *    kullanıcı yok; tek görev periyodik kontrol, pencere bekleme ve kurulumu
 *    sırayla yürütüyor. İki görevin aynı anda indirmeye kalkması da böylece
 *    imkânsız.
 *
 *  - **Geri alma doğrulaması EN BAŞTA.** init() ilk iş olarak bu açılışın
 *    bir OTA adayı olup olmadığına bakıyor. Ağ, manifest, hiçbiri gerekmiyor.
 *
 *  - **Kurulum penceresi kontak KAPALI.** Yol ortasında yeniden başlatmak
 *    canlı takibi kör bırakır. `forced` bunu atlıyor.
 *
 *  - **İndirme sırasında yeniden başlatmıyoruz.** esp_https_ota bitince
 *    bölüm işaretleniyor ve kısa bir gecikmeyle restart. Gecikme, komut
 *    yanıtının seri porttan/MQTT'den çıkması için.
 */

#include "ota_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "mqtt_service.h"
#include "net_service.h"
#include "ota_manifest.h"
#include "telemetry_service.h"

#include <esp_app_format.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "ota";

#define OTA_TICK_MS 2000

static ota_status_t s_st = {};
static ota_manifest_t s_pending = {};
static bool     s_have_pending  = false;
static bool     s_install_req   = false;   /* elle tetikleme */
static bool     s_check_req     = false;
static int64_t  s_next_check    = 0;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

const char* ota_service_state_name(ota_state_t s)
{
    switch (s) {
        case OTA_IDLE:        return "boşta";
        case OTA_CHECKING:    return "kontrol ediliyor";
        case OTA_UP_TO_DATE:  return "güncel";
        case OTA_AVAILABLE:   return "güncelleme var (pencere bekleniyor)";
        case OTA_DOWNLOADING: return "indiriliyor";
        case OTA_INSTALLED:   return "kuruldu, yeniden başlatılıyor";
        case OTA_FAILED:      return "başarısız";
        default:              return "?";
    }
}

/* ── Geri alma doğrulaması ──────────────────────────────────────────────── */

static void validate_tick(void)
{
    if (s_st.validated || !s_st.pending_verify) return;

    /* (a) Güçlü kanıt: boru hattı uçtan uca çalıştı.
     * (b) Zaman: açılış döngüsü ve erken çökme olmadığı kanıtlandı.
     * Hangisi önce gelirse — gerekçe header'da.
     *
     * Yayın bilgisini mqtt_service'ten YOKLUYORUZ, ona bildirim çağrısı
     * eklemek yerine. Böylece mqtt_service OTA'yı hiç bilmiyor ve bağımlılık
     * tek yönlü kalıyor. */
    mqtt_stats_t mq;
    mqtt_service_stats(&mq);
    const bool by_publish = (mq.published > 0);
    const bool by_uptime  = now_ms() >= OTA_VALIDATE_UPTIME_MS;
    if (!by_publish && !by_uptime) return;

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        s_st.validated      = true;
        s_st.pending_verify = false;
        ESP_LOGI(TAG, "firmware GEÇERLİ işaretlendi (%s) — geri alma iptal",
                 by_publish ? "yayın onaylandı" : "çalışma süresi");
    } else {
        ESP_LOGE(TAG, "geçerleme başarısız: %s", esp_err_to_name(err));
    }
}

/* ── Manifest kontrolü ──────────────────────────────────────────────────── */

typedef struct { char* buf; size_t cap; size_t len; } http_ctx_t;

static esp_err_t http_evt(esp_http_client_event_t* e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
        http_ctx_t* c = (http_ctx_t*)e->user_data;
        int n = e->data_len;
        if (c->len + (size_t)n >= c->cap) n = (int)(c->cap - c->len - 1);
        if (n > 0) {
            memcpy(c->buf + c->len, e->data, (size_t)n);
            c->len += (size_t)n;
            c->buf[c->len] = '\0';
        }
    }
    return ESP_OK;
}

static void build_url(char* out, size_t cap)
{
    char base[192] = {0};
    config_store_get_str(NVS_KEY_OTA_URL, base, sizeof(base),
                         OTA_MANIFEST_URL_DEFAULT);
    if (!base[0]) { out[0] = '\0'; return; }

    /* Cihaz kimliği IMEI — sunucu cihazları IMEI ile anahtarlıyor. Wear MAC
     * kullanıyor; aynı uç noktayı paylaşmamalarının bir sebebi daha. */
    char imei[24] = {0};
    config_store_get_str(NVS_KEY_IMEI, imei, sizeof(imei), "");

    const char* sep = strchr(base, '?') ? "&" : "?";
    snprintf(out, cap, "%s%sdevice_id=%s&current=%d&product=%s",
             base, sep, imei[0] ? imei : "unknown", OS_VERSION_CODE,
             NARRTOS_PRODUCT);
}

static bool do_check(void)
{
    char url[320];
    build_url(url, sizeof(url));
    if (!url[0]) {
        snprintf(s_st.error, sizeof(s_st.error), "OTA adresi ayarlı değil");
        return false;
    }

    s_st.state = OTA_CHECKING;
    s_st.checks++;

    static char body[1024];
    body[0] = '\0';
    http_ctx_t ctx = { body, sizeof(body), 0 };

    esp_http_client_config_t cfg = {};
    cfg.url           = url;
    cfg.timeout_ms    = OTA_HTTP_TIMEOUT_MS;
    cfg.event_handler = http_evt;
    cfg.user_data     = &ctx;
    cfg.crt_bundle_attach = NULL;   /* https ise sertifika demeti gerekir */

    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) { snprintf(s_st.error, sizeof(s_st.error), "http init"); return false; }

    esp_err_t err = esp_http_client_perform(cl);
    int status = esp_http_client_get_status_code(cl);
    esp_http_client_cleanup(cl);

    if (err != ESP_OK || status != 200) {
        snprintf(s_st.error, sizeof(s_st.error), "manifest HTTP %d (%s)",
                 status, esp_err_to_name(err));
        return false;
    }

    ota_manifest_t m;
    if (ota_manifest_parse(body, &m) != OTA_MF_OK) {
        snprintf(s_st.error, sizeof(s_st.error), "manifest çözülemedi");
        return false;
    }

    s_st.latest_code = m.version_code;
    strlcpy(s_st.latest_version, m.version, sizeof(s_st.latest_version));
    s_st.forced = m.forced;

    if (!ota_manifest_is_newer(&m, OS_VERSION_CODE)) {
        s_st.state = OTA_UP_TO_DATE;
        s_have_pending = false;
        ESP_LOGI(TAG, "güncel (sunucu %ld, cihaz %d)",
                 (long)m.version_code, OS_VERSION_CODE);
        return true;
    }

    s_pending = m;
    s_have_pending = true;
    s_st.state = OTA_AVAILABLE;
    ESP_LOGI(TAG, "güncelleme var: %s (kod %ld)%s", m.version,
             (long)m.version_code, m.forced ? " [ZORUNLU]" : "");
    return true;
}

/* ── Kurulum ────────────────────────────────────────────────────────────── */

static bool do_install(void)
{
    s_st.state = OTA_DOWNLOADING;
    s_st.progress_pct = 0;

    esp_http_client_config_t http_cfg = {};
    http_cfg.url        = s_pending.url;
    http_cfg.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    http_cfg.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &h);
    if (err != ESP_OK || !h) {
        snprintf(s_st.error, sizeof(s_st.error), "ota_begin: %s",
                 esp_err_to_name(err));
        return false;
    }

    const int total = esp_https_ota_get_image_size(h);
    while ((err = esp_https_ota_perform(h)) ==
           ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (total > 0) {
            int done = esp_https_ota_get_image_len_read(h);
            s_st.progress_pct = (uint8_t)((int64_t)done * 100 / total);
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        snprintf(s_st.error, sizeof(s_st.error), "indirme: %s",
                 esp_err_to_name(err));
        return false;
    }

    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        snprintf(s_st.error, sizeof(s_st.error), "ota_finish: %s",
                 esp_err_to_name(err));
        return false;
    }

    s_st.state = OTA_INSTALLED;
    s_st.progress_pct = 100;
    ESP_LOGW(TAG, "kuruldu (%s) — yeniden başlatılıyor", s_pending.version);
    return true;
}

/** Kurulum penceresi açık mı? */
static bool install_window_open(void)
{
    if (s_pending.forced) return true;   /* kritik yama park beklemez */

    telemetry_stats_t t;
    telemetry_service_stats(&t);
    /* Kontak kapalı = araç park hâlinde. Yol ortasında yeniden başlatmak
     * canlı takibi ~30 sn kör bırakır. */
    return !t.ignition;
}

/* ── Görev ──────────────────────────────────────────────────────────────── */

static void ota_task(void* arg)
{
    (void)arg;
    s_next_check = now_ms() + 60 * 1000;   /* boot'tan 1 dk sonra ilk kontrol */

    for (;;) {
        validate_tick();

        const bool online = net_service_is_online();

        if ((s_check_req || (online && now_ms() >= s_next_check)) &&
            s_st.state != OTA_DOWNLOADING) {
            s_check_req = false;
            s_next_check = now_ms() + OTA_CHECK_PERIOD_MS;
            if (online) {
                if (!do_check()) { s_st.state = OTA_FAILED; s_st.failures++; }
            } else {
                snprintf(s_st.error, sizeof(s_st.error), "ağ yok");
                s_st.state = OTA_FAILED;
            }
        }

        if (s_have_pending && online &&
            (s_install_req || install_window_open())) {
            s_install_req = false;
            if (do_install()) {
                vTaskDelay(pdMS_TO_TICKS(1500));  /* yanıt kanaldan çıksın */
                esp_restart();
            } else {
                s_st.state = OTA_FAILED;
                s_st.failures++;
                /* Bekleyen manifest duruyor; pencere/ağ düzelince tekrar
                 * denenecek. Sürekli denememek için kontrol periyodunu
                 * bekliyoruz. */
                s_have_pending = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(OTA_TICK_MS));
    }
}

/* ── Kurulum / durum ────────────────────────────────────────────────────── */

esp_err_t ota_service_init(void)
{
    memset(&s_st, 0, sizeof(s_st));

    /* Bu açılış bir OTA adayı mı? Geri alma açıkken yeni firmware kendini
     * geçerlemezse bootloader bir sonraki açılışta eskiye döner. */
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_ota_img_states_t    st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_st.pending_verify = true;
        ESP_LOGW(TAG, "bu açılış DOĞRULAMA BEKLİYOR — geçerlenmezse "
                      "sonraki açılışta eski sürüme dönülecek");
    } else {
        s_st.validated = true;   /* normal açılış */
    }

    BaseType_t r = xTaskCreatePinnedToCore(ota_task, "ota_svc", 6144, NULL,
                                           1, NULL, NET_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    char url[192] = {0};
    config_store_get_str(NVS_KEY_OTA_URL, url, sizeof(url),
                         OTA_MANIFEST_URL_DEFAULT);
    ESP_LOGI(TAG, "init OK (%s, kontrol %d saat)",
             url[0] ? url : "adres ayarlı DEĞİL — OTA kapalı",
             OTA_CHECK_PERIOD_MS / 3600000);
    return ESP_OK;
}

esp_err_t ota_service_check_now(void)
{
    if (!net_service_is_online()) return ESP_ERR_INVALID_STATE;
    s_check_req = true;
    return ESP_OK;
}

esp_err_t ota_service_install_now(void)
{
    if (!s_have_pending) return ESP_ERR_NOT_FOUND;
    if (!net_service_is_online()) return ESP_ERR_INVALID_STATE;
    s_install_req = true;
    return ESP_OK;
}

void ota_service_status(ota_status_t* out)
{
    if (out) *out = s_st;
}
