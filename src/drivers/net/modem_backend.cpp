/**
 * @file modem_backend.cpp
 * @brief 4G modem arka ucu — sahadaki gerçek bağlantı yolu.
 *
 * M3'te yer tutucuydu, M7'de yazıldı. `net_backend_t` sözleşmesini uyguluyor;
 * net_service hangi taşıyıcıyı kullandığını hâlâ bilmiyor.
 *
 * ── DONANIM YOK — ne doğrulandı, ne doğrulanmadı ──────────────────────────
 *
 * Doğrulanan: AT komut katmanı ve yanıt ayrıştırıcıları host'ta senaryolu
 * modemle baştan sona sınandı (`test_at_client.cpp`, 37 doğrulama).
 *
 * DOĞRULANMAYAN: bu dosyanın tamamı. UART kurulumu, güç sırası, PPP çevirme
 * ve veri yolu yalnız DERLENİYOR. TWAI sürücüsü ve NMEA UART ile aynı statü.
 * Modem geldiğinde beklenen iş, yazmak değil hata ayıklamak.
 *
 * ── Açılış sırası ─────────────────────────────────────────────────────────
 *
 *   PWRKEY darbesi → AT yanıt verene kadar yokla → ATE0 → CPIN? (SIM var mı)
 *   → CGSN (IMEI) → CREG bekle (şebeke) → CGDCONT (APN) → ATD*99# → PPP
 *
 * Her adım ayrı ayrı raporlanıyor (`modem status`). Ekransız cihazda "modem
 * bağlanmıyor" tek başına işe yaramaz; SIM mi yok, kapsama mı yok, APN mi
 * yanlış — kurulumcunun bunu ayırt etmesi gerekiyor.
 *
 * ── IMEI modemden okunuyor ve NVS'i EZİYOR ────────────────────────────────
 *
 * Sunucu cihazları IMEI ile anahtarlıyor. M1'den beri elle giriliyordu çünkü
 * modem yoktu; artık gerçek kaynak modem. Elle girilen değer yalnızca
 * modemsiz çalışırken geçerli.
 */

#include "net_backend.h"

#if ENABLE_MODEM

#include "../../core/config_store.h"
#include "../../services/at_client.h"
#include "../../services/time_service.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_ppp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "modem";

#define MODEM_UART_RX_BUF 2048
#define MODEM_PPP_MTU     1500

/* ── Durum ──────────────────────────────────────────────────────────────── */

typedef enum {
    MS_OFF = 0, MS_BOOTING, MS_NO_SIM, MS_SEARCHING, MS_REGISTERED,
    MS_DIALING, MS_ONLINE, MS_ERROR,
} modem_step_t;

static modem_step_t s_step        = MS_OFF;
static bool         s_inited      = false;
static volatile bool s_got_ip     = false;
static bool         s_data_mode   = false;
static esp_netif_t* s_netif       = NULL;
static TaskHandle_t s_pump        = NULL;
static char         s_imei[24]    = {0};
static int          s_csq         = -1;
static char         s_last_err[48] = {0};

const char* modem_step_name(modem_step_t s)
{
    switch (s) {
        case MS_OFF:        return "kapalı";
        case MS_BOOTING:    return "açılıyor";
        case MS_NO_SIM:     return "SIM YOK";
        case MS_SEARCHING:  return "şebeke aranıyor";
        case MS_REGISTERED: return "şebekeye kayıtlı";
        case MS_DIALING:    return "çevriliyor";
        case MS_ONLINE:     return "çevrimiçi";
        default:            return "hata";
    }
}

/* ── AT taşıyıcısı (UART) ───────────────────────────────────────────────── */

static bool uart_write_line(void* ctx, const char* line)
{
    (void)ctx;
    int n = uart_write_bytes((uart_port_t)MODEM_UART_NUM, line, strlen(line));
    if (n < 0) return false;
    return uart_write_bytes((uart_port_t)MODEM_UART_NUM, "\r", 1) >= 0;
}

static bool uart_read_line(void* ctx, char* out, size_t cap, uint32_t timeout_ms)
{
    (void)ctx;
    size_t  pos      = 0;
    int64_t deadline = esp_timer_get_time() / 1000 + timeout_ms;

    while (esp_timer_get_time() / 1000 < deadline) {
        uint8_t ch;
        int r = uart_read_bytes((uart_port_t)MODEM_UART_NUM, &ch, 1,
                                pdMS_TO_TICKS(50));
        if (r <= 0) continue;
        if (ch == '\n') { out[pos] = '\0'; return true; }
        if (ch == '\r') continue;
        if (pos + 1 < cap) out[pos++] = (char)ch;
    }
    out[0] = '\0';
    return false;
}

static at_io_t s_io = { uart_write_line, uart_read_line, NULL };

/* ── PPP sürücüsü — UART ile netif arasındaki köprü ─────────────────────── */

/* lwIP'ten gelen çerçeveyi modeme yaz. */
static esp_err_t ppp_transmit(void* h, void* buffer, size_t len)
{
    (void)h;
    int n = uart_write_bytes((uart_port_t)MODEM_UART_NUM, (const char*)buffer, len);
    return (n == (int)len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ppp_post_attach(esp_netif_t* netif, void* args)
{
    (void)args;
    static esp_netif_driver_ifconfig_t cfg = {};
    cfg.handle          = (esp_netif_iodriver_handle)1;   /* sahte tutamak */
    cfg.transmit        = ppp_transmit;
    cfg.driver_free_rx_buffer = NULL;
    return esp_netif_set_driver_config(netif, &cfg);
}

/* Modemden geleni lwIP'e ver. */
static void ppp_pump_task(void* arg)
{
    (void)arg;
    static uint8_t buf[512];
    for (;;) {
        int n = uart_read_bytes((uart_port_t)MODEM_UART_NUM, buf, sizeof(buf),
                                pdMS_TO_TICKS(100));
        if (n > 0 && s_netif && s_data_mode)
            esp_netif_receive(s_netif, buf, (size_t)n, NULL);
    }
}

static void on_ip_event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_PPP_GOT_IP) {
        s_got_ip = true;
        s_step   = MS_ONLINE;
        ESP_LOGI(TAG, "PPP IP aldı");
    } else if (base == IP_EVENT && id == IP_EVENT_PPP_LOST_IP) {
        s_got_ip = false;
        ESP_LOGW(TAG, "PPP IP kaybetti");
    }
}

/* ── Güç ve açılış ──────────────────────────────────────────────────────── */

static bool power_pulse(void)
{
    if (PIN_MODEM_PWRKEY < 0) return true;   /* kart sürekli açık tutuyorsa */

    gpio_set_level((gpio_num_t)PIN_MODEM_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(MODEM_PWRKEY_PULSE_MS));
    gpio_set_level((gpio_num_t)PIN_MODEM_PWRKEY, 0);
    return true;
}

/** Modem AT'ye cevap verene kadar yokla. Soğuk açılış saniyeler sürüyor. */
static bool wait_for_at(void)
{
    const int64_t deadline = esp_timer_get_time() / 1000 + MODEM_BOOT_TIMEOUT_MS;
    while (esp_timer_get_time() / 1000 < deadline) {
        if (at_command(&s_io, "AT", 1000, NULL, 0, NULL) == AT_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

/* ── net_backend_t uygulaması ───────────────────────────────────────────── */

static esp_err_t modem_init(void)
{
    if (s_inited) return ESP_OK;

    if (PIN_MODEM_PWRKEY >= 0) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << PIN_MODEM_PWRKEY;
        io.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level((gpio_num_t)PIN_MODEM_PWRKEY, 0);
    }

    uart_config_t cfg = {};
    cfg.baud_rate  = MODEM_BAUD_DEFAULT;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install((uart_port_t)MODEM_UART_NUM,
                                        MODEM_UART_RX_BUF, MODEM_UART_RX_BUF,
                                        0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if ((err = uart_param_config((uart_port_t)MODEM_UART_NUM, &cfg)) != ESP_OK)
        return err;
    if ((err = uart_set_pin((uart_port_t)MODEM_UART_NUM, PIN_MODEM_TX,
                            PIN_MODEM_RX, UART_PIN_NO_CHANGE,
                            UART_PIN_NO_CHANGE)) != ESP_OK) return err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_PPP();
    esp_netif_config_t netif_cfg = {};
    netif_cfg.base   = &base;
    netif_cfg.driver = NULL;
    netif_cfg.stack  = ESP_NETIF_NETSTACK_DEFAULT_PPP;
    s_netif = esp_netif_new(&netif_cfg);
    if (!s_netif) { ESP_LOGE(TAG, "PPP netif kurulamadı"); return ESP_FAIL; }

    static esp_netif_driver_base_t drv = {};
    drv.netif      = s_netif;
    drv.post_attach = ppp_post_attach;
    esp_netif_attach(s_netif, &drv);

    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                        on_ip_event, NULL, NULL);

    if (xTaskCreatePinnedToCore(ppp_pump_task, "ppp_pump", 3072, NULL,
                                NET_TASK_PRIORITY + 1, &s_pump,
                                NET_TASK_CORE) != pdPASS)
        return ESP_FAIL;

    s_inited = true;
    ESP_LOGI(TAG, "init OK (UART%d, tx=%d rx=%d, pwrkey=%d)",
             MODEM_UART_NUM, PIN_MODEM_TX, PIN_MODEM_RX, PIN_MODEM_PWRKEY);
    return ESP_OK;
}

static bool modem_is_configured(void)
{
    char apn[64] = {0};
    config_store_get_str(NVS_KEY_APN, apn, sizeof(apn), "");
    return apn[0] != '\0';
}

static esp_err_t modem_connect(uint32_t timeout_ms)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    s_last_err[0] = '\0';
    s_data_mode = false;
    s_got_ip = false;

    char resp[AT_RESP_MAX];
    int  code = 0;

    /* 1. Güç ve açılış. */
    s_step = MS_BOOTING;
    if (at_command(&s_io, "AT", 500, NULL, 0, NULL) != AT_OK) {
        power_pulse();
        if (!wait_for_at()) {
            s_step = MS_ERROR;
            snprintf(s_last_err, sizeof(s_last_err), "modem AT'ye cevap vermiyor");
            return ESP_ERR_TIMEOUT;
        }
    }
    at_command(&s_io, "ATE0", MODEM_AT_TIMEOUT_MS, NULL, 0, NULL);

    /* 2. SIM. En sık saha arızası; ayrı raporlanıyor. */
    if (at_command(&s_io, "AT+CPIN?", MODEM_AT_TIMEOUT_MS, resp, sizeof(resp),
                   &code) != AT_OK || !strstr(resp, "READY")) {
        s_step = MS_NO_SIM;
        snprintf(s_last_err, sizeof(s_last_err), "SIM hazır değil (CME %d)", code);
        return ESP_FAIL;
    }

    /* 3. IMEI — gerçek kaynak modem, NVS'teki elle girilen değeri EZER. */
    if (at_command(&s_io, "AT+CGSN", MODEM_AT_TIMEOUT_MS, resp, sizeof(resp),
                   NULL) == AT_OK &&
        at_parse_imei(resp, s_imei, sizeof(s_imei))) {
        char cur[24] = {0};
        config_store_get_str(NVS_KEY_IMEI, cur, sizeof(cur), "");
        if (strcmp(cur, s_imei) != 0) {
            config_store_set_str(NVS_KEY_IMEI, s_imei);
            ESP_LOGW(TAG, "IMEI modemden güncellendi: %s", s_imei);
        }
    }

    /* 4. Şebeke kaydı. */
    s_step = MS_SEARCHING;
    const int64_t reg_deadline = esp_timer_get_time() / 1000 +
                                 (timeout_ms ? timeout_ms : MODEM_REG_TIMEOUT_MS);
    bool registered = false;
    while (esp_timer_get_time() / 1000 < reg_deadline) {
        int stat = 0;
        if (at_command(&s_io, "AT+CREG?", MODEM_AT_TIMEOUT_MS, resp,
                       sizeof(resp), NULL) == AT_OK &&
            at_parse_creg(resp, &stat) && at_creg_registered(stat)) {
            registered = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (!registered) {
        s_step = MS_SEARCHING;
        snprintf(s_last_err, sizeof(s_last_err), "şebekeye kayıt olunamadı");
        return ESP_ERR_TIMEOUT;
    }
    s_step = MS_REGISTERED;

    /* 5. Sinyal ve saat — üçüncü saat kaynağı. */
    if (at_command(&s_io, "AT+CSQ", MODEM_AT_TIMEOUT_MS, resp, sizeof(resp),
                   NULL) == AT_OK) {
        int idx = 0;
        s_csq = at_parse_csq(resp, &idx) ? idx : -1;
    }
    if (at_command(&s_io, "AT+CCLK?", MODEM_AT_TIMEOUT_MS, resp, sizeof(resp),
                   NULL) == AT_OK) {
        int64_t utc = 0;
        /* Makullük denetimi time_service'te (2020 öncesi reddediliyor) —
         * şebeke saati kurulmamış bir modem 1980 döndürebilir. */
        if (at_parse_cclk(resp, &utc)) time_service_set_wall_ms(utc, "modem");
    }

    /* 6. PDP bağlamı. */
    char apn[64] = {0};
    config_store_get_str(NVS_KEY_APN, apn, sizeof(apn), "");
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (at_command(&s_io, cmd, MODEM_AT_TIMEOUT_MS, NULL, 0, &code) != AT_OK) {
        s_step = MS_ERROR;
        snprintf(s_last_err, sizeof(s_last_err), "APN reddedildi (CME %d)", code);
        return ESP_FAIL;
    }

    /* 7. Veri modu. CONNECT sonrası UART artık AT değil PPP taşıyor. */
    s_step = MS_DIALING;
    if (at_command(&s_io, "ATD*99***1#", 10000, resp, sizeof(resp), NULL)
            != AT_OK && !strstr(resp, "CONNECT")) {
        s_step = MS_ERROR;
        snprintf(s_last_err, sizeof(s_last_err), "çevirme başarısız");
        return ESP_FAIL;
    }
    s_data_mode = true;

    esp_netif_ppp_set_auth(s_netif, NETIF_PPP_AUTHTYPE_NONE, NULL, NULL);
    esp_netif_action_start(s_netif, NULL, 0, NULL);
    esp_netif_action_connected(s_netif, NULL, 0, NULL);

    /* IP olay geri çağrısıyla gelecek; net_service zaten yokluyor. */
    const int64_t ip_deadline = esp_timer_get_time() / 1000 + 30000;
    while (esp_timer_get_time() / 1000 < ip_deadline) {
        if (s_got_ip) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    snprintf(s_last_err, sizeof(s_last_err), "PPP IP alamadı");
    return ESP_ERR_TIMEOUT;
}

static void modem_disconnect_(void)
{
    if (!s_inited) return;
    if (s_data_mode) {
        esp_netif_action_disconnected(s_netif, NULL, 0, NULL);
        esp_netif_action_stop(s_netif, NULL, 0, NULL);
        s_data_mode = false;
        /* +++ ile komut moduna dön; öncesinde 1 sn sessizlik şart. */
        vTaskDelay(pdMS_TO_TICKS(1100));
        uart_write_bytes((uart_port_t)MODEM_UART_NUM, "+++", 3);
        vTaskDelay(pdMS_TO_TICKS(1100));
        at_command(&s_io, "ATH", MODEM_AT_TIMEOUT_MS, NULL, 0, NULL);
    }
    s_got_ip = false;
    s_step   = MS_REGISTERED;
}

static bool modem_is_connected(void) { return s_got_ip; }

static const net_backend_t s_modem = {
    "modem", modem_init, modem_connect, modem_disconnect_,
    modem_is_connected, modem_is_configured,
};

const net_backend_t* net_backend_modem(void) { return &s_modem; }

/* Teşhis — command_service için. */
int         modem_backend_csq(void)      { return s_csq; }
const char* modem_backend_imei(void)     { return s_imei; }
const char* modem_backend_step(void)     { return modem_step_name(s_step); }
const char* modem_backend_last_error(void) { return s_last_err; }

#else   /* ENABLE_MODEM == 0 — donanım yok */

const net_backend_t* net_backend_modem(void) { return NULL; }

int         modem_backend_csq(void)        { return -1; }
const char* modem_backend_imei(void)       { return ""; }
const char* modem_backend_step(void)       { return "derleme dışı"; }
const char* modem_backend_last_error(void) { return ""; }

#endif  /* ENABLE_MODEM */
