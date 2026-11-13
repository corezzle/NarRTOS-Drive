/**
 * @file status_led.cpp
 * @brief Durum LED'i desen motoru (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Desenler tablo hâlinde.** Her durum (yanık/söndük, süre) adımlarından
 *    oluşan bir dizi; motor onları döngüde çalıyor. Yeni bir durum eklemek
 *    tabloya bir satır; if/else zinciriyle zamanlama yazmak yerine.
 *
 *  - **Durum 500 ms'de bir yeniden hesaplanıyor, desen 20 ms'de bir
 *    ilerliyor.** İkisi ayrı: durum hesabı NVS okuması ve mutex alıyor,
 *    bunu saniyede 50 kez yapmanın anlamı yok. Desen ise ince adımlarla
 *    ilerlemeli ki durum değişince LED 3 saniye eski deseni sürdürmesin.
 *
 *  - **En derindeki sorun gösteriliyor.** Ayarı eksik cihaz aynı zamanda
 *    çevrimdışıdır; "çevrimdışı" demek kurulumcuyu anten/kapsama aramaya
 *    iterdi, oysa sorun girmediği ayarda.
 *
 *  - **IMEI eksikliği ayar eksikliği sayılıyor.** Sunucu cihazları IMEI ile
 *    anahtarlıyor; IMEI'siz cihaz bağlansa bile sunucuda karşılığı olmaz.
 *    Ağ ayarı tamamken bile "ayarsız" göstermek doğru.
 */

#include "status_led.h"
#include "../core/config_store.h"
#include "../core/os_config.h"
#include "mqtt_service.h"
#include "net_service.h"
#include "store_forward.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "led";

#if PIN_STATUS_LED >= 0

#define LED_TICK_MS 20    /* desen çözünürlüğü */
#define LED_EVAL_MS 500   /* durum yeniden hesaplama aralığı */

typedef struct {
    bool     on;
    uint16_t ms;
} led_step_t;

/* Desenler — bkz. header'daki "ne kadar çok ışık, o kadar kötü" kuralı. */
static const led_step_t P_ERROR[]        = { {true, 1000} };
static const led_step_t P_UNCONFIGURED[] = { {true, 100}, {false, 100} };
static const led_step_t P_OFFLINE[]      = { {true, 300}, {false, 1700} };
static const led_step_t P_NO_BROKER[]    = { {true, 120}, {false, 180},
                                             {true, 120}, {false, 1580} };
static const led_step_t P_ONLINE[]       = { {true, 50},  {false, 2950} };

typedef struct {
    const led_step_t* steps;
    uint8_t           count;
    const char*       name;
} led_pattern_t;

static const led_pattern_t PATTERNS[] = {
    [LED_STATE_ERROR]        = { P_ERROR,        1, "sürekli yanık (tampon yok)" },
    [LED_STATE_UNCONFIGURED] = { P_UNCONFIGURED, 2, "hızlı (ayar eksik)" },
    [LED_STATE_OFFLINE]      = { P_OFFLINE,      2, "uzun tek flaş (bağlantı yok)" },
    [LED_STATE_NO_BROKER]    = { P_NO_BROKER,    4, "çift flaş (broker yok)" },
    [LED_STATE_ONLINE]       = { P_ONLINE,       2, "kısa kırpma (çevrimiçi)" },
};

static led_state_t s_state       = LED_STATE_OFFLINE;
static led_state_t s_forced      = LED_STATE_OFFLINE;
static int64_t     s_force_until = 0;

static inline void led_write(bool on)
{
#if STATUS_LED_ACTIVE_HIGH
    gpio_set_level((gpio_num_t)PIN_STATUS_LED, on ? 1 : 0);
#else
    gpio_set_level((gpio_num_t)PIN_STATUS_LED, on ? 0 : 1);
#endif
}

static led_state_t evaluate(void)
{
    /* 1. Tampon kurulamadıysa cihaz işini yapamaz: rapor ne gönderilebilir
     *    ne saklanabilir. En derindeki arıza budur. */
    sf_stats_t sf;
    store_forward_stats(&sf);
    if (sf.capacity == 0) return LED_STATE_ERROR;

    /* 2. Kimlik ya da ağ ayarı eksik. */
    char imei[24];
    config_store_get_str(NVS_KEY_IMEI, imei, sizeof(imei), "");
    if (!imei[0]) return LED_STATE_UNCONFIGURED;

    net_state_t n = net_service_state();
    if (n == NET_STATE_UNCONFIGURED || n == NET_STATE_DISABLED)
        return LED_STATE_UNCONFIGURED;

    /* 3. Ayar tam ama bağlantı yok — kapsama, anten, şifre. */
    if (n != NET_STATE_ONLINE) return LED_STATE_OFFLINE;

    /* 4. Ağ var ama broker yok — adres/port/kimlik doğrulama yanlış olabilir.
     *    Bu ayrım kurulumcuyu doğru yere bakmaya yönlendiriyor. */
    if (!mqtt_service_is_connected()) return LED_STATE_NO_BROKER;

    return LED_STATE_ONLINE;
}

static void led_task(void* arg)
{
    (void)arg;
    uint8_t  idx        = 0;
    uint32_t step_el    = 0;   /* mevcut adımda geçen süre */
    uint32_t since_eval = LED_EVAL_MS;   /* ilk turda hemen hesapla */

    for (;;) {
        if (since_eval >= LED_EVAL_MS) {
            since_eval = 0;
            led_state_t want = (esp_timer_get_time() / 1000 < s_force_until)
                                   ? s_forced : evaluate();
            if (want != s_state) {
                s_state = want;
                idx = 0;
                step_el = 0;
                ESP_LOGI(TAG, "%s", PATTERNS[s_state].name);
            }
        }

        const led_pattern_t* p = &PATTERNS[s_state];
        led_write(p->steps[idx].on);

        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
        step_el    += LED_TICK_MS;
        since_eval += LED_TICK_MS;

        if (step_el >= p->steps[idx].ms) {
            step_el = 0;
            idx = (uint8_t)((idx + 1) % p->count);
        }
    }
}

esp_err_t status_led_init(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_STATUS_LED;
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d kurulamadı: %s", PIN_STATUS_LED,
                 esp_err_to_name(err));
        return err;
    }
    led_write(false);

    /* En düşük öncelik: LED hiçbir şeyin önüne geçmemeli. Gecikse de
     * insan gözü fark etmez. */
    BaseType_t r = xTaskCreatePinnedToCore(led_task, "status_led", 2560, NULL,
                                           1, NULL, NET_TASK_CORE);
    if (r != pdPASS) { ESP_LOGE(TAG, "görev oluşturulamadı"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init OK (GPIO %d, %s)", PIN_STATUS_LED,
             STATUS_LED_ACTIVE_HIGH ? "aktif-yüksek" : "aktif-düşük");
    return ESP_OK;
}

led_state_t status_led_current(void) { return s_state; }

void status_led_force(led_state_t s, uint32_t ms)
{
    s_forced      = s;
    s_force_until = (ms == 0) ? 0 : (esp_timer_get_time() / 1000 + ms);
}

#else   /* PIN_STATUS_LED < 0 — LED'siz kart */

esp_err_t status_led_init(void)
{
    ESP_LOGI(TAG, "LED yok (PIN_STATUS_LED = -1)");
    return ESP_OK;
}

led_state_t status_led_current(void) { return LED_STATE_OFFLINE; }
void status_led_force(led_state_t s, uint32_t ms) { (void)s; (void)ms; }

#endif  /* PIN_STATUS_LED */

const char* status_led_state_name(led_state_t s)
{
    switch (s) {
        case LED_STATE_ERROR:        return "HATA — tampon yok";
        case LED_STATE_UNCONFIGURED: return "ayarsız";
        case LED_STATE_OFFLINE:      return "çevrimdışı";
        case LED_STATE_NO_BROKER:    return "broker yok";
        case LED_STATE_ONLINE:       return "çevrimiçi";
        default:                     return "?";
    }
}
