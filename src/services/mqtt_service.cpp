/**
 * @file mqtt_service.cpp
 * @brief MQTT istemcisi ve tampon boşaltıcı (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **enqueue_report ENABLE_MQTT'den bağımsız.** Bayrak kapalıyken de rapor
 *    store_forward'a yazılıyor. Böylece M4'te telemetri yükü, broker sözleşmesi
 *    hâlâ bilinmezken yazılıp `sf peek` ile gözle doğrulanabilir.
 *
 *  - **İstemci ilk ONLINE'da başlatılıp bir daha durdurulmuyor.** esp-mqtt'nin
 *    kendi yeniden bağlanma mantığı var; her kopmada stop/start yapmak
 *    abonelikleri düşürür ve durumu iki yerden yönetmek olurdu. net_service
 *    taşımayı, esp-mqtt oturumu ayakta tutuyor.
 *
 *  - **Gelen komutlar KUYRUĞA alınıp kendi görevimizde işleniyor**, esp-mqtt
 *    olay işleyicisinde değil. Olay işleyicisi içinden publish etmek (komut
 *    yanıtı) esp-mqtt'de kilitlenmeye açık; ayrıca `reboot` gibi bir komut
 *    istemcinin kendi görevini bloklardı.
 *
 *  - **Boşaltma tik başına en çok MQTT_FLUSH_PER_TICK kayıt.** Birikmiş 800
 *    kayıtlık tamponu tek döngüde boşaltmak gelen komutları dakikalarca
 *    bekletirdi; ekransız cihazda komut yolu tıkanmamalı.
 */

#include "mqtt_service.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "command_service.h"
#include "mqtt_contract.h"
#include "net_service.h"
#include "store_forward.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "mqtt";

/* Telemetri her koşulda tampondan geçer — bkz. header. ENABLE_MQTT kapalıyken
 * de geçerli: tampon birikir, `sf stats` ile görülür. */
esp_err_t mqtt_service_enqueue_report(const void* payload, size_t len)
{
    return store_forward_push(payload, len);
}

/* Yük üretici — ENABLE_MQTT'den bağımsız tutuluyor ki komut arayüzü
 * (`sf peek`) MQTT derleme dışıyken de kaydı çözebilsin. */
static mqtt_render_fn s_render     = NULL;
static void*          s_render_ctx = NULL;

void mqtt_service_set_render(mqtt_render_fn fn, void* ctx)
{
    s_render     = fn;
    s_render_ctx = ctx;
}

#if ENABLE_MQTT

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#define MQTT_TICK_MS        200
#define MQTT_FLUSH_PER_TICK 8
#define MQTT_CMD_LINE_MAX   192
#define MQTT_CMD_QUEUE_LEN  4
#define MQTT_KEEPALIVE_S    60
/* İstemci başlatma denemesi aralığı. Broker adresi boot'ta boş olabilir —
 * kurulumcu `set mqtt host` yazınca reboot beklemesin. */
#define MQTT_START_RETRY_MS 5000

static esp_mqtt_client_handle_t s_client     = NULL;
static volatile bool            s_connected  = false;
static bool                     s_started    = false;
/* net_service geri çağrısı yalnız BUNU yazar; istemciyi kurma işi mqtt
 * görevinde. Aksi hâlde client_start iki görevden (net görevi + app_main)
 * aynı anda çağrılabilir ve iki istemci kurulabilirdi. */
static volatile bool            s_net_online = false;
/* Ayar değişti: istemci kapatılıp yeni ayarlarla kurulacak. Bayrağı komut
 * görevi koyuyor, işi mqtt görevi yapıyor. */
static volatile bool            s_reconfig   = false;

/* Onay takibi: olay işleyicisi son PUBACK'in msg_id'sini YAZAR, semaforu
 * yalnız uyandırmak için kullanıyoruz. "Beklenen id"yi işleyiciye söyleyip
 * orada karşılaştırmak yarışa açıktı: PUBACK, esp_mqtt_client_publish()
 * dönmeden işlenebiliyor ve beklenen id henüz yazılmamış oluyordu. */
static SemaphoreHandle_t s_ack_sem     = NULL;
static volatile int      s_last_ack_id = -1;

static QueueHandle_t s_cmd_q = NULL;

static mqtt_stats_t s_st = {};

/* Konular istemci başlatılırken bir kez kuruluyor. IMEI değişirse yeniden
 * başlatma gerekir — `set imei` bunu söylüyor. */
static char s_topic_telemetry[128];
static char s_topic_status[128];
static char s_topic_cmd[128];
static char s_topic_reply[128];

/* esp-mqtt bu dizeleri kendi kopyalıyor ama ömürleri konusunda garanti
 * vermemek yerine statik tutuyoruz. */
static char s_imei[24];
static char s_user[64];
static char s_pass[64];
static char s_uri[96];

/* ── Olay işleyicisi ────────────────────────────────────────────────────── */
static void on_mqtt_event(void* arg, esp_event_base_t base, int32_t id,
                          void* data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        s_st.reconnects++;
        ESP_LOGI(TAG, "bağlandı (%s)", s_uri);
        esp_mqtt_client_subscribe(s_client, s_topic_cmd, MQTT_QOS_CMD);
        /* Durum retained: panel cihaza bakınca son bilinen hâli görsün. */
        esp_mqtt_client_publish(s_client, s_topic_status, MQTT_STATUS_ONLINE, 0,
                                MQTT_QOS_STATUS, MQTT_RETAIN_STATUS ? 1 : 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "bağlantı koptu");
        break;

    case MQTT_EVENT_PUBLISHED:
        /* QoS1 PUBACK. Karşılaştırma bekleyen tarafta yapılıyor (bkz. yukarı). */
        s_last_ack_id = e->msg_id;
        if (s_ack_sem) xSemaphoreGive(s_ack_sem);
        break;

    case MQTT_EVENT_DATA: {
        /* Yalnız cmd konusuna abone olduğumuz için konu ayrımı yapmıyoruz.
         * Parçalanmış yükü işlemiyoruz: komut satırı kısa olmak zorunda. */
        if (e->data_len != e->total_data_len ||
            e->data_len <= 0 || e->data_len >= MQTT_CMD_LINE_MAX) {
            ESP_LOGW(TAG, "komut yükü uygun değil (%d/%d bayt)",
                     e->data_len, e->total_data_len);
            break;
        }
        char line[MQTT_CMD_LINE_MAX];
        memcpy(line, e->data, (size_t)e->data_len);
        line[e->data_len] = '\0';
        s_st.cmds_received++;
        if (!s_cmd_q || xQueueSend(s_cmd_q, line, 0) != pdTRUE)
            ESP_LOGW(TAG, "komut kuyruğu dolu, satır atıldı");
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "hata (tip %d)", (int)e->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* ── Komut yanıt kanalı ─────────────────────────────────────────────────── */
static void mqtt_reply(const char* line, void* ctx)
{
    (void)ctx;
    if (!s_client || !s_connected) return;
    /* QoS 0: yanıt kaybı telemetri kaybı gibi değil, tekrar sorulabilir. */
    esp_mqtt_client_publish(s_client, s_topic_reply, line, 0, 0, 0);
}

/* ── Tampon boşaltma ────────────────────────────────────────────────────── */
/* Görev yığınına sığmaz (4 KB yük, 4 KB yığın) — statik. */
static uint8_t s_txbuf[SF_PAYLOAD_MAX];
/* Üretilen JSON. 64 CAN frame + 8 WiFi ağlık en kötü hâl ~2,9 KB. */
static char    s_jsonbuf[3456];

/** @return gönderilen kayıt sayısı */
static uint32_t flush_pending(void)
{
    uint32_t done = 0;

    for (int i = 0; i < MQTT_FLUSH_PER_TICK && s_connected; i++) {
        size_t   len = 0;
        uint32_t seq = 0;
        esp_err_t err = store_forward_peek(s_txbuf, sizeof(s_txbuf), &len, &seq);
        if (err != ESP_OK) break;              /* bekleyen kayıt yok */

        /* İkili kaydı yayınlanacak yüke çevir. */
        const char* body  = (const char*)s_txbuf;
        int         blen  = (int)len;
        if (s_render) {
            int r = s_render(s_txbuf, len, s_jsonbuf, sizeof(s_jsonbuf),
                             s_render_ctx);
            if (r == MQTT_RENDER_RETRY) {
                /* Tipik sebep: saat henüz kurulmadı. Kayıt tamponda kalıyor —
                 * yanlış damgalı rapor göndermektense beklemek doğru. Kuyruk
                 * sıralı olduğu için bu tik burada bitiyor. */
                s_st.render_held++;
                break;
            }
            if (r < 0) {
                /* Asla üretilemez (bozuk ya da eski açılıştan damgasız kayıt).
                 * Kuyruğu tıkamasın diye gönderilmiş sayılıyor. */
                s_st.render_dropped++;
                ESP_LOGW(TAG, "kayıt çözülemedi (seq %lu), düşürülüyor",
                         (unsigned long)seq);
                store_forward_pop(seq);
                continue;
            }
            body = s_jsonbuf;
            blen = r;
        }

        /* Onay durumunu publish'ten ÖNCE temizle. Sonra temizlemek, publish
         * dönmeden işlenen bir PUBACK'i silmek olurdu. */
        s_last_ack_id = -1;
        if (s_ack_sem) xSemaphoreTake(s_ack_sem, 0);

        int msg_id = esp_mqtt_client_publish(s_client, s_topic_telemetry,
                                            body, blen,
                                            MQTT_QOS_TELEMETRY, 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "publish başarısız, kayıt tamponda kalıyor");
            break;
        }

        /* Semafor yalnız uyandırıyor; karar s_last_ack_id ile burada veriliyor.
         * Böylece onay publish'ten önce işlenmiş olsa da yakalanıyor. */
        bool acked = false;
        for (uint32_t waited = 0; waited < MQTT_ACK_TIMEOUT_MS; waited += 50) {
            if (s_last_ack_id == msg_id) { acked = true; break; }
            if (!s_connected) break;
            if (s_ack_sem) xSemaphoreTake(s_ack_sem, pdMS_TO_TICKS(50));
            else vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_last_ack_id == msg_id) acked = true;

        if (acked) {
            /* Onay geldi — kayıt ancak ŞİMDİ silinebilir. */
            store_forward_pop(seq);
            s_st.published++;
            done++;
        } else {
            /* Onay yok: kayıt tamponda KALIYOR. Tekrarlamak kaybetmekten iyi;
             * sunucu dateTime ile tekilleştirebilir. */
            s_st.ack_timeouts++;
            ESP_LOGW(TAG, "PUBACK gelmedi (seq %lu), kayıt korundu",
                     (unsigned long)seq);
            break;
        }
    }
    return done;
}

/* ── Görev ──────────────────────────────────────────────────────────────── */
static esp_err_t client_start(void);

static void mqtt_task(void* arg)
{
    (void)arg;
    static char line[MQTT_CMD_LINE_MAX];
    int64_t last_start_try = 0;

    for (;;) {
        /* Komutlar önce: ekransız cihazda müdahale yolu tıkanmamalı. */
        while (s_cmd_q && xQueueReceive(s_cmd_q, line, 0) == pdTRUE) {
            ESP_LOGI(TAG, "uzak komut: %s", line);
            command_service_exec_ex(line, mqtt_reply, NULL);
        }

        /* Ayar değişikliği: istemciyi kapat. Aşağıdaki kurulum döngüsü onu
         * yeni adres/konularla yeniden ayağa kaldıracak. */
        if (s_reconfig) {
            s_reconfig = false;
            if (s_client) {
                ESP_LOGW(TAG, "ayar değişti — istemci yeniden kuruluyor");
                esp_mqtt_client_stop(s_client);
                esp_mqtt_client_destroy(s_client);
                s_client = NULL;
            }
            s_started      = false;
            s_connected    = false;
            last_start_try = 0;   /* hemen dene, 5 sn bekleme */
        }

        /* İstemciyi TEK yerden kuruyoruz. Periyodik deneme, boot'ta broker
         * adresi boş olan cihazın `set mqtt host` sonrası kendiliğinden
         * ayağa kalkmasını da sağlıyor. */
        if (s_net_online && !s_started) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - last_start_try >= MQTT_START_RETRY_MS) {
                last_start_try = now;
                client_start();
            }
        }

        if (s_connected) flush_pending();
        vTaskDelay(pdMS_TO_TICKS(MQTT_TICK_MS));
    }
}

/* ── İstemciyi kur ──────────────────────────────────────────────────────── */
static esp_err_t client_start(void)
{
    if (s_started) return ESP_OK;

    char host[64] = {0};
    uint16_t port = MQTT_PORT_DEFAULT;
    config_store_get_str(NVS_KEY_MQTT_HOST, host, sizeof(host), "");
    config_store_get_u16(NVS_KEY_MQTT_PORT, &port, MQTT_PORT_DEFAULT);
    if (!host[0]) {
        /* Beş saniyede bir denendiği için uyarı yalnız bir kez basılıyor;
         * ekransız cihazın konsolu tek bir sorunla dolmasın. */
        static bool uyarildi = false;
        if (!uyarildi) {
            uyarildi = true;
            ESP_LOGW(TAG, "broker adresi ayarlı değil — `set mqtt host <adres>` "
                          "yazın, istemci kendiliğinden başlar");
        }
        return ESP_ERR_INVALID_STATE;
    }

    config_store_get_str(NVS_KEY_IMEI, s_imei, sizeof(s_imei), "");
    config_store_get_str(NVS_KEY_MQTT_USER, s_user, sizeof(s_user), "");
    config_store_get_str(NVS_KEY_MQTT_PASS, s_pass, sizeof(s_pass), "");
    if (!s_imei[0])
        ESP_LOGW(TAG, "IMEI ayarlı değil — konular 'unknown' ile kurulacak, "
                      "sunucu tanımaz");

    mqtt_topic(s_topic_telemetry, sizeof(s_topic_telemetry), s_imei,
               MQTT_LEAF_TELEMETRY);
    mqtt_topic(s_topic_status, sizeof(s_topic_status), s_imei, MQTT_LEAF_STATUS);
    mqtt_topic(s_topic_cmd, sizeof(s_topic_cmd), s_imei, MQTT_LEAF_CMD);
    mqtt_topic(s_topic_reply, sizeof(s_topic_reply), s_imei, MQTT_LEAF_CMD_REPLY);

    /* TLS yok: broker sözleşmesi bilinmiyor, sertifika da yok. Gerçek adres
     * öğrenilince mqtts:// ve crt_bundle_attach eklenecek (bkz.
     * mqtt_contract.h doğrulama listesi). */
    snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%u", host, (unsigned)port);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = s_uri;
    cfg.credentials.client_id = s_imei[0] ? s_imei : NULL;
    if (s_user[0]) cfg.credentials.username = s_user;
    if (s_pass[0]) cfg.credentials.authentication.password = s_pass;

    /* Last Will: broker kopmayı fark ettiğinde "offline" yayınlar. Araç
     * takibinde "cihaz sustu" ile "araç durdu"yu ayırmanın en ucuz yolu. */
    cfg.session.last_will.topic  = s_topic_status;
    cfg.session.last_will.msg    = MQTT_LWT_PAYLOAD;
    cfg.session.last_will.msg_len = (int)strlen(MQTT_LWT_PAYLOAD);
    cfg.session.last_will.qos    = MQTT_QOS_STATUS;
    cfg.session.last_will.retain = MQTT_RETAIN_STATUS ? 1 : 0;
    cfg.session.keepalive        = MQTT_KEEPALIVE_S;

    /* Temiz oturum: kalıcılığı store_forward yapıyor, broker'ın oturum
     * kuyruğuna güvenmiyoruz. Kaybı flash'tan telafi edebiliyoruz, broker
     * kuyruğunun ne kadar tuttuğunu bilemeyiz. */

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) { ESP_LOGE(TAG, "istemci kurulamadı"); return ESP_FAIL; }

    esp_err_t err = esp_mqtt_client_register_event(
        s_client, MQTT_EVENT_ANY, on_mqtt_event, NULL);
    if (err != ESP_OK) return err;

    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "başlatılamadı: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "istemci başlatıldı (%s), konu: %s", s_uri, s_topic_telemetry);
    return ESP_OK;
}

/* net_service görevinden çağrılıyor — burada YALNIZ bayrak yazılır, iş
 * mqtt görevinde yapılır. Kopmada istemciyi durdurmuyoruz: esp-mqtt kendi
 * yeniden bağlanıyor, stop/start abonelikleri düşürürdü. */
static void on_net_state(net_state_t s, void* ctx)
{
    (void)ctx;
    s_net_online = (s == NET_STATE_ONLINE);
}

esp_err_t mqtt_service_init(void)
{
    s_ack_sem = xSemaphoreCreateBinary();
    s_cmd_q   = xQueueCreate(MQTT_CMD_QUEUE_LEN, MQTT_CMD_LINE_MAX);
    if (!s_ack_sem || !s_cmd_q) return ESP_ERR_NO_MEM;

    net_service_add_state_cb(on_net_state, NULL);
    /* net_service bizden önce başladı ve ONLINE'a geçmiş olabilir; o geçişin
     * bildirimini kaçırdık, başlangıç değerini elle okuyoruz. */
    s_net_online = net_service_is_online();

    BaseType_t r = xTaskCreatePinnedToCore(mqtt_task, "mqtt_svc", 4096, NULL,
                                           TELEMETRY_TASK_PRIORITY, NULL,
                                           TELEMETRY_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init OK (önek %s, QoS %d, havada en çok %d kayıt)",
             MQTT_TOPIC_PREFIX, MQTT_QOS_TELEMETRY, MQTT_INFLIGHT_MAX);
    return ESP_OK;
}

void mqtt_service_reconfigure(void) { s_reconfig = true; }

bool mqtt_service_is_connected(void) { return s_connected; }

void mqtt_service_stats(mqtt_stats_t* out)
{
    if (!out) return;
    *out = s_st;
    out->compiled  = true;
    out->connected = s_connected;
}

esp_err_t mqtt_service_publish_raw(const char* leaf, const char* payload,
                                   int qos, bool retain)
{
    if (!s_client || !s_connected) return ESP_ERR_INVALID_STATE;
    char topic[128];
    mqtt_topic(topic, sizeof(topic), s_imei, leaf);
    int id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos,
                                     retain ? 1 : 0);
    return (id < 0) ? ESP_FAIL : ESP_OK;
}

#else   /* ENABLE_MQTT == 0 */

esp_err_t mqtt_service_init(void)
{
    ESP_LOGI(TAG, "derleme dışı (ENABLE_MQTT=0) — raporlar tamponda birikir");
    return ESP_OK;
}

bool mqtt_service_is_connected(void) { return false; }

void mqtt_service_reconfigure(void) { }

void mqtt_service_stats(mqtt_stats_t* out)
{
    if (out) { *out = mqtt_stats_t{}; }
}

esp_err_t mqtt_service_publish_raw(const char* leaf, const char* payload,
                                   int qos, bool retain)
{
    (void)leaf; (void)payload; (void)qos; (void)retain;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif  /* ENABLE_MQTT */
