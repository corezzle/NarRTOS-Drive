/**
 * @file command_service.cpp
 * @brief Metin komutlarıyla Drive'ı sürme (bkz. header).
 *
 * Wear'daki command_service.cpp'nin yapısı korundu:
 *  - UART0 RX: log çıkan konsol hattının okuma yönü. uart_driver_install
 *    yalnızca RX buffer'ı kurar; log/printf TX yolu (VFS polling) DEĞİŞMEZ.
 *  - Yanıtlar reply-sink üzerinden akar, exec çağrıları mutex ile serileşir.
 *
 * Wear'dan farklar:
 *  - Uygulama/ekran/jest komutları yok (ekran yok).
 *  - Ayar komutları kalıcı: NVS'e yazılıyor (Wear'da parlaklık gibi çoğu
 *    ayar anlıktı).
 *  - `can inject` var: CAN transceiver'ı olmadan boru hattını sürebilmek için.
 */

#include "command_service.h"
#include "../core/config_store.h"
#include "../drivers/can/twai_driver.h"
#include "../drivers/net/net_backend.h"
#include "can_service.h"
#include "gnss_service.h"
#include "mqtt_service.h"
#include "nmea_parser.h"
#include "ota_service.h"
#include "position_service.h"
#include "power_service.h"
#include "wifi_scan_service.h"
#include "net_service.h"
#include "status_led.h"
#include "store_forward.h"
#include "telemetry_codec.h"
#include "telemetry_service.h"
#include "time_service.h"
#include "selftest.h"
#include "../core/os_config.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "CmdSvc";

#define CMD_UART      UART_NUM_0
#define CMD_LINE_MAX  256
#define CFG_STR_MAX   64

/* ── Yanıt sink'i (exec süresince aktif taşıyıcıya işaret eder) ─────────── */
static SemaphoreHandle_t s_exec_mutex = NULL;
static cmd_reply_fn_t    s_reply      = NULL;
static void*             s_reply_ctx  = NULL;

static can_frame_sink_fn_t s_can_sink = NULL;

static void R(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s_reply) s_reply(buf, s_reply_ctx);
}

static void serial_reply(const char* line, void* ctx)
{
    (void)ctx;
    printf("%s\n", line);
}

void command_service_set_can_sink(can_frame_sink_fn_t fn) { s_can_sink = fn; }

/* ── Komut işleyiciler ──────────────────────────────────────────────────── */

static void cmd_help(void)
{
    R("[cmd] komutlar:");
    R("[cmd]   info                        durum JSON'u");
    R("[cmd]   selftest                    donanim checklist'i (test ADI)");
    R("[cmd]   get config                  tüm ayarlar");
    R("[cmd]   set imei <imei>             cihaz kimliği");
    R("[cmd]   set mqtt host <host>        broker adresi");
    R("[cmd]   set mqtt port <port>        broker portu");
    R("[cmd]   set mqtt user|pass <x>      broker kimlik doğrulama");
    R("[cmd]   set wifi ssid|pass <x>      atölye WiFi'si (boşluk olabilir)");
    R("[cmd]   set net backend <x>         wifi|modem|none");
    R("[cmd]   set apn <apn>               modem APN'i");
    R("[cmd]   config clear                fabrika ayarı");
    R("[cmd]   set can bitrate <kbps>      125|250|500|1000");
    R("[cmd]   can inject <0xID,b0,..,b7>  ham CAN frame'i");
    R("[cmd]   can stats|dump [n]|clear    CAN tablosu");
    R("[cmd]   net status                  bağlantı durumu");
    R("[cmd]   net offline <0|1>           kapsama kaybı taklidi");
    R("[cmd]   net reconnect               beklemeyi atla, hemen dene");
    R("[cmd]   net modem                   modem adımı, sinyal, IMEI");
    R("[cmd]   mqtt status                 istemci sayaçları");
    R("[cmd]   sf stats                    tampon doluluğu");
    R("[cmd]   sf push <metin>             tampona ham kayıt yaz (test)");
    R("[cmd]   sf peek                     en eski kaydı ÇÖZÜLMÜŞ göster");
    R("[cmd]   sf clear                    bekleyenleri gönderilmiş say");
    R("[cmd]   time status                 duvar saati ve kaynağı");
    R("[cmd]   set ntp <host>              NTP sunucusu");
    R("[cmd]   telemetry now               hemen bir rapor üret");
    R("[cmd]   telemetry status            periyot, kontak, sayaçlar");
    R("[cmd]   telemetry ignition <0|1|auto>  kontağı elle sür");
    R("[cmd]   set gnss baud <baud>        NMEA alıcısı hızı");
    R("[cmd]   gnss status                 sabitleme, uydu, sayaçlar");
    R("[cmd]   gnss inject $GPRMC,...*HH   NMEA cümlesi enjekte et");
    R("[cmd]   pos status                  konum kaynağı (uydu/tahmin)");
    R("[cmd]   wifi scan                   çevredeki ağları tara (konumlama)");
    R("[cmd]   wifi status                 tarama sayaçları ve son liste");
    R("[cmd]   wifi inject <mac> <rssi>    listeye elle ağ ekle (test)");
    R("[cmd]   set ota url <adres>         manifest adresi (boş = OTA kapalı)");
    R("[cmd]   ota status|check|install    kablosuz güncelleme");
    R("[cmd]   led status                  LED ne gösteriyor");
    R("[cmd]   led test [sn]               LED'i sürekli yak (montajda bulmak için)");
    R("[cmd]   set power sleep <0|1>       kontak kapanınca derin uyku");
    R("[cmd]   power status                uyku kararı, uyanma sebebi, sayaç");
    R("[cmd]   power sleep now             hemen uyu (konsol kapanır!)");
    R("[cmd]   reboot                      yeniden başlat");
}

static void cmd_info(void)
{
    char imei[CFG_STR_MAX], host[CFG_STR_MAX];
    uint16_t port = 0;
    config_store_get_str(NVS_KEY_IMEI, imei, sizeof(imei), "");
    config_store_get_str(NVS_KEY_MQTT_HOST, host, sizeof(host), "");
    config_store_get_u16(NVS_KEY_MQTT_PORT, &port, 0);

    sf_stats_t sf;
    store_forward_stats(&sf);

    R("[cmd] {\"product\":\"%s\",\"version\":\"%s\",\"code\":%d,"
      "\"uptime_s\":%llu,\"heap_free\":%lu,"
      "\"imei\":\"%s\",\"mqtt\":\"%s:%u\","
      "\"net\":\"%s/%s\",\"mqtt_up\":%s,\"buffered\":%lu}",
      NARRTOS_PRODUCT, OS_VERSION_STR, OS_VERSION_CODE,
      (unsigned long long)(esp_timer_get_time() / 1000000ULL),
      (unsigned long)esp_get_free_heap_size(),
      imei, host, (unsigned)port,
      net_service_backend_name(),
      net_service_state_name(net_service_state()),
      mqtt_service_is_connected() ? "true" : "false",
      (unsigned long)sf.stored);
}

static void cmd_get_config(void)
{
    char buf[CFG_STR_MAX];
    uint16_t port = 0;

    /* Ayarlanmamış olanı boş dizeyle değil "<ayarlanmadı>" ile göster —
     * kurulumcu "boş mu bıraktım, yoksa boş dize mi yazdım" diye
     * düşünmesin. */
    config_store_get_str(NVS_KEY_IMEI, buf, sizeof(buf), "");
    R("[cmd] imei      = %s", buf[0] ? buf : "<ayarlanmadı>");
    config_store_get_str(NVS_KEY_MQTT_HOST, buf, sizeof(buf), "");
    R("[cmd] mqtt host = %s", buf[0] ? buf : "<ayarlanmadı>");
    config_store_get_u16(NVS_KEY_MQTT_PORT, &port, 0);
    if (port) R("[cmd] mqtt port = %u", (unsigned)port);
    else      R("[cmd] mqtt port = <ayarlanmadı>");
    config_store_get_str(NVS_KEY_MQTT_USER, buf, sizeof(buf), "");
    R("[cmd] mqtt user = %s", buf[0] ? buf : "<ayarlanmadı>");
    /* Şifreler DEĞER olarak basılmıyor — konsol çıktısı loglara, ekran
     * görüntülerine ve destek taleplerine düşüyor. Ayarlı mı, o kadar. */
    config_store_get_str(NVS_KEY_MQTT_PASS, buf, sizeof(buf), "");
    R("[cmd] mqtt pass = %s", buf[0] ? "<ayarlı>" : "<ayarlanmadı>");
    config_store_get_str(NVS_KEY_APN, buf, sizeof(buf), "");
    R("[cmd] apn       = %s", buf[0] ? buf : "<ayarlanmadı>");
    config_store_get_str(NVS_KEY_WIFI_SSID, buf, sizeof(buf), "");
    R("[cmd] wifi ssid = %s", buf[0] ? buf : "<ayarlanmadı>");
    config_store_get_str(NVS_KEY_WIFI_PASS, buf, sizeof(buf), "");
    R("[cmd] wifi pass = %s", buf[0] ? "<ayarlı>" : "<ayarlanmadı>");
    config_store_get_str(NVS_KEY_NET_BACKEND, buf, sizeof(buf), "");
    R("[cmd] net back. = %s (etkin: %s)", buf[0] ? buf : "<oto>",
      net_service_backend_name());
    config_store_get_str(NVS_KEY_NTP, buf, sizeof(buf), NTP_SERVER_DEFAULT);
    R("[cmd] ntp       = %s", buf);
    {
        uint16_t ps = 0;
        config_store_get_u16(NVS_KEY_POWER_SLEEP, &ps, 0);
        R("[cmd] pwr sleep = %s", ps ? "1 (kontak kapanınca uyur)" : "0 (hep uyanık)");
    }
}

/* IMEI 15 hane; 14 (kontrol hanesiz) de kabul ediliyor çünkü bazı modemler
 * öyle raporluyor. Uzunluk ve rakam kontrolü yapıyoruz — yanlış yazılmış bir
 * IMEI sahada sessizce "cihaz görünmüyor" olarak geri döner, en ucuz yer
 * burada yakalamak. Luhn kontrolü yapmıyoruz: bazı test SIM'leri geçmiyor. */
static bool imei_gecerli(const char* s)
{
    size_t n = strlen(s);
    if (n != 14 && n != 15) return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

/* Satırın kalanını döndürür, baştaki boşlukları atlayarak; boşsa NULL.
 * SSID, WiFi şifresi ve APN'de boşluk olabiliyor — token bazlı ayrıştırma
 * "Misafir Agi"yi ilk kelimede keserdi. exec_locked satır sonundaki
 * boşlukları zaten kırpmış oluyor. */
static char* rest_of_line(char** save)
{
    if (!save || !*save) return NULL;
    char* p = *save;
    while (*p == ' ' || *p == '\t') p++;
    *save = p + strlen(p);          /* tüketildi */
    return *p ? p : NULL;
}

/** Dizi ayarı yaz; val NULL ise anahtarı siler. `gizli` ise değer basılmaz. */
static void set_str_reply(const char* key, const char* etiket, char* val,
                          bool gizli)
{
    if (config_store_set_str(key, val ? val : "") != ESP_OK) {
        R("[cmd] ERR: yazılamadı");
        return;
    }
    if (!val || !val[0]) R("[cmd] OK: %s <silindi>", etiket);
    else if (gizli)      R("[cmd] OK: %s <ayarlandı, %u karakter>", etiket,
                           (unsigned)strlen(val));
    else                 R("[cmd] OK: %s=%s", etiket, val);
}

static void cmd_set(char* param, char** save)
{
    if (!param) {
        R("[cmd] ERR: set <imei|mqtt|wifi|net|apn|ntp|can> ..."); return;
    }

    if (strcasecmp(param, "imei") == 0) {
        char* val = strtok_r(NULL, " \t", save);
        if (!val) { R("[cmd] ERR: set imei <15 hane>"); return; }
        if (!imei_gecerli(val)) {
            R("[cmd] ERR: IMEI 14-15 hane rakam olmalı ('%s')", val);
            return;
        }
        if (config_store_set_str(NVS_KEY_IMEI, val) == ESP_OK) {
            /* MQTT konuları IMEI'den kuruluyor — istemciyi yeniden kur. */
            mqtt_service_reconfigure();
            R("[cmd] OK: imei=%s", val);
        } else R("[cmd] ERR: yazılamadı");
        return;
    }

    if (strcasecmp(param, "mqtt") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        if (!what) { R("[cmd] ERR: set mqtt <host|port|user|pass> <değer>"); return; }

        if (strcasecmp(what, "host") == 0) {
            set_str_reply(NVS_KEY_MQTT_HOST, "mqtt host", rest_of_line(save), false);
        } else if (strcasecmp(what, "port") == 0) {
            char* v = strtok_r(NULL, " \t", save);
            if (!v) { R("[cmd] ERR: set mqtt port <1-65535>"); return; }
            long p = strtol(v, NULL, 10);
            if (p < 1 || p > 65535) { R("[cmd] ERR: port 1-65535"); return; }
            if (config_store_set_u16(NVS_KEY_MQTT_PORT, (uint16_t)p) == ESP_OK)
                R("[cmd] OK: mqtt port=%ld", p);
            else R("[cmd] ERR: yazılamadı");
        } else if (strcasecmp(what, "user") == 0) {
            set_str_reply(NVS_KEY_MQTT_USER, "mqtt user", rest_of_line(save), false);
        } else if (strcasecmp(what, "pass") == 0) {
            set_str_reply(NVS_KEY_MQTT_PASS, "mqtt pass", rest_of_line(save), true);
        } else {
            R("[cmd] ERR: set mqtt <host|port|user|pass> <değer>");
            return;
        }
        /* Adres/kimlik değişti: çalışan istemciyi yeni ayarlarla yeniden kur.
         * Yoksa değişiklik reboot'a kadar etkisiz kalıyordu. */
        mqtt_service_reconfigure();
        return;
    }

    if (strcasecmp(param, "power") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        char* v    = strtok_r(NULL, " \t", save);
        if (!what || strcasecmp(what, "sleep") != 0 || !v ||
            (strcmp(v, "0") != 0 && strcmp(v, "1") != 0)) {
            R("[cmd] ERR: set power sleep <0|1>"); return;
        }
        if (config_store_set_u16(NVS_KEY_POWER_SLEEP, (uint16_t)(v[0] - '0')) == ESP_OK) {
            R("[cmd] OK: power sleep=%s", v);
            if (v[0] == '1') {
                R("[cmd] (kontak %lu sn kapalı kalınca uyur; uykuda USB "
                  "konsolu KAPANIR)", (unsigned long)(POWER_SLEEP_DELAY_MS / 1000));
                R("[cmd] (uyanma: kontak sinyali ya da %lu dk'lık zamanlayıcı)",
                  (unsigned long)(POWER_WAKE_INTERVAL_MS / 60000));
            }
        } else R("[cmd] ERR: yazılamadı");
        return;
    }

    if (strcasecmp(param, "wifi") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        if (!what) { R("[cmd] ERR: set wifi <ssid|pass> <değer>"); return; }

        if (strcasecmp(what, "ssid") == 0) {
            set_str_reply(NVS_KEY_WIFI_SSID, "wifi ssid", rest_of_line(save), false);
            /* SSID değişti: beklemeyi sıfırla, kurulumcu 5 dakika beklemesin. */
            net_service_reconnect_now();
        } else if (strcasecmp(what, "pass") == 0) {
            set_str_reply(NVS_KEY_WIFI_PASS, "wifi pass", rest_of_line(save), true);
            net_service_reconnect_now();
        } else {
            R("[cmd] ERR: set wifi <ssid|pass> <değer>");
        }
        return;
    }

    if (strcasecmp(param, "net") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        char* v    = strtok_r(NULL, " \t", save);
        if (!what || strcasecmp(what, "backend") != 0 || !v) {
            R("[cmd] ERR: set net backend <wifi|modem|none>"); return;
        }
        if (strcasecmp(v, "wifi") && strcasecmp(v, "modem") &&
            strcasecmp(v, "none")) {
            R("[cmd] ERR: arka uç wifi|modem|none olmalı"); return;
        }
        if (config_store_set_str(NVS_KEY_NET_BACKEND, v) == ESP_OK)
            R("[cmd] OK: net backend=%s (reboot gerekli)", v);
        else R("[cmd] ERR: yazılamadı");
        return;
    }

    if (strcasecmp(param, "can") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        char* v    = strtok_r(NULL, " \t", save);
        if (!what || strcasecmp(what, "bitrate") != 0 || !v) {
            R("[cmd] ERR: set can bitrate <125|250|500|1000>"); return;
        }
        long k = strtol(v, NULL, 10);
        if (k != 125 && k != 250 && k != 500 && k != 1000) {
            R("[cmd] ERR: desteklenen hızlar 125/250/500/1000"); return;
        }
        if (config_store_set_u16(NVS_KEY_CAN_KBPS, (uint16_t)k) == ESP_OK)
            R("[cmd] OK: can bitrate=%ld (reboot gerekli)", k);
        else R("[cmd] ERR: yazılamadı");
        return;
    }

    if (strcasecmp(param, "ota") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        if (!what || strcasecmp(what, "url") != 0) {
            R("[cmd] ERR: set ota url <adres>"); return;
        }
        set_str_reply(NVS_KEY_OTA_URL, "ota url", rest_of_line(save), false);
        return;
    }

    if (strcasecmp(param, "gnss") == 0) {
        char* what = strtok_r(NULL, " \t", save);
        char* v    = strtok_r(NULL, " \t", save);
        if (!what || strcasecmp(what, "baud") != 0 || !v) {
            R("[cmd] ERR: set gnss baud <4800|9600|38400|115200>"); return;
        }
        long b = strtol(v, NULL, 10);
        if (b != 4800 && b != 9600 && b != 19200 && b != 38400 &&
            b != 57600 && b != 115200) {
            R("[cmd] ERR: desteklenen hızlar 4800/9600/19200/38400/57600/115200");
            return;
        }
        if (config_store_set_u32(NVS_KEY_GNSS_BAUD, (uint32_t)b) == ESP_OK)
            R("[cmd] OK: gnss baud=%ld (reboot gerekli)", b);
        else R("[cmd] ERR: yazılamadı");
        return;
    }

    if (strcasecmp(param, "apn") == 0) {
        set_str_reply(NVS_KEY_APN, "apn", rest_of_line(save), false);
        return;
    }

    if (strcasecmp(param, "ntp") == 0) {
        /* Kapalı bir şebekede public havuz erişilemez olur ve cihaz saatsiz
         * kalır; saatsiz kalınca da rapor gönderilemez (damga üretilemiyor). */
        set_str_reply(NVS_KEY_NTP, "ntp", rest_of_line(save), false);
        R("[cmd] (reboot gerekli — SNTP bir kez kuruluyor)");
        return;
    }

    R("[cmd] ERR: bilinmeyen parametre '%s'", param);
}


static void cmd_can_stats(void)
{
    can_stats_t st;
    can_service_stats(&st);
    int bus = can_twai_bus_state();
    const char* durum = (bus < 0) ? "sürücü yok (ENABLE_CAN=0)"
                      : (bus == 1) ? "çalışıyor"
                      : (bus == 2) ? "bus-off"
                      : (bus == 3) ? "kurtarılıyor" : "durdu";
    R("[cmd] hat      : %s", durum);
    R("[cmd] alınan   : %lu frame", (unsigned long)st.rx_total);
    R("[cmd] kimlik   : %u / %u", (unsigned)st.distinct, (unsigned)st.capacity);
    R("[cmd] atılan   : %lu (tablo dolu)", (unsigned long)st.dropped_full);
}

/* Tabloyu sunucuya gidecek tel formatında döker — rapor içeriğini
 * göndermeden önce gözle doğrulayabilmek için. */
static void cmd_can_dump(const char* arg)
{
    size_t max = arg ? (size_t)strtoul(arg, NULL, 10) : 16;
    if (max == 0 || max > CAN_FRAMES_PER_REPORT) max = CAN_FRAMES_PER_REPORT;

    static can_frame_t buf[CAN_FRAMES_PER_REPORT];
    size_t n = can_service_snapshot(buf, max);
    if (n == 0) { R("[cmd] tablo boş"); return; }

    for (size_t i = 0; i < n; i++) {
        char line[64];
        int p = snprintf(line, sizeof(line), "0x%08lX",
                         (unsigned long)buf[i].id);
        for (uint8_t b = 0; b < buf[i].dlc && p < (int)sizeof(line) - 4; b++)
            p += snprintf(line + p, sizeof(line) - p, ",%02X", buf[i].data[b]);
        R("[cmd] %s  (x%lu)", line, (unsigned long)buf[i].count);
    }
    R("[cmd] %u frame", (unsigned)n);
}

/* ── can inject ─────────────────────────────────────────────────────────────
 * Biçim: 0xID,b0,b1,...  (kimlik + 0-8 bayt, hepsi hex, virgülle)
 * Araç takip sunucusunun beklediği tel formatının aynısı — cihaz frame'i böyle
 * iletecek, çözümü sunucu yapacak (J1939 DBC ile). Burada yalnız ayrıştırıp
 * doğruluyoruz; transceiver gelmeden formatı sınamanın tek yolu bu. */
static void cmd_can_inject(char* arg)
{
    if (!arg) { R("[cmd] ERR: can inject 0xID,b0,..,b7"); return; }

    char* save = NULL;
    char* tok  = strtok_r(arg, ",", &save);
    if (!tok) { R("[cmd] ERR: kimlik yok"); return; }

    char* end = NULL;
    unsigned long id = strtoul(tok, &end, 16);
    if (end == tok || *end != '\0') {
        R("[cmd] ERR: kimlik hex olmalı ('%s')", tok);
        return;
    }
    /* 29-bit genişletilmiş kimlik sınırı; J1939 tamamen extended kullanır. */
    if (id > 0x1FFFFFFFUL) { R("[cmd] ERR: kimlik 29 biti aşıyor"); return; }
    bool ext = (id > 0x7FF);

    uint8_t data[8];
    uint8_t dlc = 0;
    while ((tok = strtok_r(NULL, ",", &save)) != NULL) {
        if (dlc >= 8) { R("[cmd] ERR: en çok 8 bayt"); return; }
        unsigned long b = strtoul(tok, &end, 16);
        if (end == tok || *end != '\0' || b > 0xFF) {
            R("[cmd] ERR: bayt hex 00-FF olmalı ('%s')", tok);
            return;
        }
        data[dlc++] = (uint8_t)b;
    }

    if (s_can_sink) {
        s_can_sink((uint32_t)id, ext, data, dlc);
        R("[cmd] OK: frame kuyruğa alındı (id=0x%08lX %s dlc=%u)",
          id, ext ? "ext" : "std", (unsigned)dlc);
    } else {
        /* M1: tüketici yok. Doğrulamayı yine de yapıp ne anladığımızı
         * geri basıyoruz — biçim hatası burada görülsün. */
        char hex[3 * 8 + 1] = {0};
        for (uint8_t i = 0; i < dlc; i++)
            snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", data[i]);
        R("[cmd] OK: çözüldü id=0x%08lX %s dlc=%u data=[%s] "
          "(tüketici yok — can_service M2'de)",
          id, ext ? "ext" : "std", (unsigned)dlc, hex);
    }
}

/* ── net / mqtt / store&forward ─────────────────────────────────────────── */

static void cmd_net_status(void)
{
    R("[cmd] arka uç  : %s", net_service_backend_name());
    R("[cmd] durum    : %s", net_service_state_name(net_service_state()));
    if (net_service_forced_offline())
        R("[cmd] ⚠ zorlanmış çevrimdışı AÇIK (`net offline 0` ile kapat)");
    uint32_t in = net_service_retry_in_ms();
    if (in) R("[cmd] sonraki deneme: %lu ms sonra", (unsigned long)in);
}

static void cmd_net_offline(const char* arg)
{
    if (!arg) { R("[cmd] ERR: net offline <0|1>"); return; }
    bool on = (arg[0] == '1');
    net_service_force_offline(on);
    R("[cmd] OK: zorlanmış çevrimdışı %s", on ? "AÇIK" : "kapalı");
}

static void cmd_mqtt_status(void)
{
    mqtt_stats_t st;
    mqtt_service_stats(&st);
    if (!st.compiled) { R("[cmd] mqtt derleme dışı (ENABLE_MQTT=0)"); return; }
    R("[cmd] bağlı        : %s", st.connected ? "evet" : "hayır");
    R("[cmd] gönderilen   : %lu (PUBACK alındı)", (unsigned long)st.published);
    R("[cmd] onay bekledi : %lu (kayıt korundu)", (unsigned long)st.ack_timeouts);
    R("[cmd] uzak komut   : %lu", (unsigned long)st.cmds_received);
    R("[cmd] bağlanma     : %lu kez", (unsigned long)st.reconnects);
}

static void cmd_sf_stats(void)
{
    sf_stats_t st;
    store_forward_stats(&st);
    if (st.capacity == 0) { R("[cmd] tampon başlatılamadı"); return; }

    R("[cmd] bekleyen  : %lu / %lu slot", (unsigned long)st.stored,
      (unsigned long)st.capacity);
    R("[cmd] yazılan   : %lu", (unsigned long)st.pushed);
    R("[cmd] gönderilen: %lu", (unsigned long)st.sent);
    R("[cmd] ezilen    : %lu (halka dolu, EN ESKİ atıldı)",
      (unsigned long)st.dropped_full);
    R("[cmd] bozuk     : %lu", (unsigned long)st.corrupt);
    R("[cmd] sıra no   : %lu", (unsigned long)st.next_seq);
    /* Kalan kapsama-dışı süresi: kurulumcunun asıl merak ettiği sayı. */
    uint32_t bos = st.capacity - st.stored;
    R("[cmd] boş slot  : %lu  (~%lu dk kapsama dışı, %d sn periyotla)",
      (unsigned long)bos,
      (unsigned long)(bos * (TELEMETRY_PERIOD_MS / 1000) / 60),
      TELEMETRY_PERIOD_MS / 1000);
}

/* Tampona elle kayıt yazar. `can inject`'in store&forward karşılığı:
 * telemetri servisi (M4) yazılmadan boru hattını sürebilmek için. */
static void cmd_sf_push(char* arg)
{
    if (!arg) { R("[cmd] ERR: sf push <metin>"); return; }
    esp_err_t err = store_forward_push(arg, strlen(arg));
    if (err == ESP_OK) R("[cmd] OK: %u bayt tampona yazıldı", (unsigned)strlen(arg));
    else R("[cmd] ERR: yazılamadı (%s)", esp_err_to_name(err));
}

static void cmd_sf_peek(void)
{
    /* Kayıt 4 KB'a kadar olabilir; komut görevinin yığını 4 KB. Kalıcı statik
     * tampon ayırmak yerine bu seyrek teşhis komutunda geçici alıyoruz. */
    uint8_t* buf = (uint8_t*)malloc(SF_PAYLOAD_MAX);
    if (!buf) { R("[cmd] ERR: bellek yok"); return; }

    size_t len = 0;
    uint32_t seq = 0;
    esp_err_t err = store_forward_peek(buf, SF_PAYLOAD_MAX, &len, &seq);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) R("[cmd] tampon boş");
        else R("[cmd] ERR: okunamadı (%s)", esp_err_to_name(err));
        free(buf);
        return;
    }

    R("[cmd] seq %lu, ham kayıt %u bayt", (unsigned long)seq, (unsigned)len);

    /* Kayıt İKİLİ; broker'a giden JSON gönderim anında üretiliyor. Burada da
     * aynı üreticiyi çağırıyoruz ki konsolda görülen ile gönderilen birebir
     * aynı olsun — "ekranda doğruydu ama sunucuya başka gitti" sınıfı
     * hataların önü kapansın. */
    char* json = (char*)malloc(3456);
    if (!json) { R("[cmd] ERR: bellek yok"); free(buf); return; }

    int r = telemetry_render_for_mqtt(buf, len, json, 3456, NULL);
    if (r == MQTT_RENDER_RETRY) {
        R("[cmd] kayıt BEKLETİLİYOR: duvar saati henüz yok, damga üretilemiyor");
        R("[cmd] (`time status` — saat gelince kendiliğinden gönderilecek)");
    } else if (r < 0) {
        R("[cmd] kayıt ÇÖZÜLEMİYOR (bozuk ya da önceki açılıştan damgasız)");
        R("[cmd] gönderim sırası geldiğinde düşürülecek");
    } else {
        /* Yanıt tamponu 256 bayt; uzun JSON'u parçalara bölerek basıyoruz. */
        const size_t chunk = 180;
        for (size_t i = 0; i < (size_t)r; i += chunk) {
            size_t n = ((size_t)r - i < chunk) ? ((size_t)r - i) : chunk;
            R("[cmd] %.*s", (int)n, json + i);
        }
        R("[cmd] (%d bayt JSON)", r);
    }
    R("[cmd] (gönderilmiş SAYILMADI — pop yalnız broker onayıyla)");

    free(json);
    free(buf);
}

/* ── GNSS ───────────────────────────────────────────────────────────────── */

static void cmd_gnss_status(void)
{
    gnss_stats_t st;
    gnss_service_stats(&st);

    R("[cmd] sabitleme : %s", st.has_fix ? "VAR" : "yok");
    R("[cmd] uydu      : %u", (unsigned)st.sats);
    R("[cmd] cümle     : %lu (desteklenmeyen %lu)",
      (unsigned long)st.rx_sentences, (unsigned long)st.unsupported);
    R("[cmd] sağlama   : %lu hatalı", (unsigned long)st.csum_errors);
    R("[cmd] biçim     : %lu hatalı", (unsigned long)st.format_errors);
    R("[cmd] konum     : %lu güncelleme", (unsigned long)st.fix_updates);
    R("[cmd] saat      : %lu kez GNSS'ten kuruldu", (unsigned long)st.time_sets);

    gnss_fix_t f;
    if (gnss_service_get_fix(&f)) {
        /* Ham 1e-7 değerler; kayan nokta biçimlendirmeye girmemek için
         * derece ve ondalık ayrı basılıyor. */
        R("[cmd] enlem     : %ld.%07ld", (long)(f.lat_e7 / 10000000),
          (long)(f.lat_e7 < 0 ? -f.lat_e7 : f.lat_e7) % 10000000);
        R("[cmd] boylam    : %ld.%07ld", (long)(f.lon_e7 / 10000000),
          (long)(f.lon_e7 < 0 ? -f.lon_e7 : f.lon_e7) % 10000000);
        R("[cmd] hız       : %u.%u km/sa", f.speed_kmh_x10 / 10,
          f.speed_kmh_x10 % 10);
        R("[cmd] rota      : %u.%u derece", f.course_deg_x10 / 10,
          f.course_deg_x10 % 10);
        R("[cmd] yaş       : %lu ms (sınır %d)", (unsigned long)f.age_ms,
          GNSS_FIX_MAX_AGE_MS);
    } else {
        R("[cmd] (geçerli/taze sabitleme yok — rapora 0,0 gidiyor)");
    }
}

/* NMEA cümlesini elle besler. `can inject`'in GNSS karşılığı: modül yokken
 * konum ve saat yolunu uçtan uca sürmenin tek yolu. */
static void cmd_gnss_inject(char* arg)
{
    if (!arg) { R("[cmd] ERR: gnss inject $GPRMC,..."); return; }

    /* Sağlama toplamını kullanıcı yazmak zorunda; yanlış cümlenin sessizce
     * kabul edilmemesi testin amacı. */
    if (!nmea_checksum_ok(arg)) {
        R("[cmd] ERR: sağlama toplamı tutmuyor ya da '*HH' eksik");
        R("[cmd] (cümle olduğu gibi atıldı — gerçek hattaki davranışın aynısı)");
        return;
    }

    gnss_stats_t before, after;
    gnss_service_stats(&before);
    gnss_service_ingest(arg);
    gnss_service_stats(&after);

    if (after.fix_updates > before.fix_updates)
        R("[cmd] OK: konum güncellendi");
    else if (after.unsupported > before.unsupported)
        R("[cmd] OK: geçerli cümle ama ilgilenmiyoruz (GSV/VTG/GSA…)");
    else if (after.format_errors > before.format_errors)
        R("[cmd] ERR: biçim hatası (alan sayısı?)");
    else
        R("[cmd] OK: işlendi (konum güncellenmedi — sabitleme yok olabilir)");

    if (after.time_sets > before.time_sets)
        R("[cmd] + saat GNSS'ten kuruldu");
}

/* ── Konum (uydu/tahmin birleştirme) ────────────────────────────────────── */

static void cmd_pos_status(void)
{
    position_stats_t st;
    position_service_stats(&st);

    R("[cmd] kaynak      : %s", position_source_name(st.source));
    if (st.source != POS_NONE)
        R("[cmd] belirsizlik : %u m (sınır %u)", (unsigned)st.uncertainty_m,
          (unsigned)DR_MAX_UNCERTAINTY_M);
    R("[cmd] uydu        : %lu tur konum alındı", (unsigned long)st.gnss_fixes);
    R("[cmd] tahmin      : %lu adım, %lu kez düşüldü, %lu kez sınır aşıldı",
      (unsigned long)st.dr_steps, (unsigned long)st.dr_sessions,
      (unsigned long)st.dr_expired);
    if (st.dr_distance_m > 0)
        R("[cmd] tahmin yolu : %lu m (son tohumlamadan beri)",
          (unsigned long)st.dr_distance_m);
    R("[cmd] araç hızı   : %s",
      st.have_can_speed ? "CAN'den geliyor"
                        : "YOK. Hız verisi olmadan tahmin başlamaz");
    if (st.have_gyro) {
        int16_t b = st.gyro_bias_dps_x10;
        R("[cmd] jiroskop    : var, sapma %s%d.%d derece/sn (duruşta ölçülür)",
          (b < 0) ? "-" : "", (b < 0 ? -b : b) / 10, (b < 0 ? -b : b) % 10);
    } else {
        R("[cmd] jiroskop    : yok. Yön sabit varsayılır, belirsizlik daha "
          "hızlı büyür");
    }

    wifi_scan_stats_t ws;
    wifi_scan_service_stats(&ws);
    if (ws.age_ms != UINT32_MAX)
        R("[cmd] wifi        : %u ağ, %lu sn önce (rapora girme sınırı %u sn)",
          (unsigned)ws.last_count, (unsigned long)(ws.age_ms / 1000),
          (unsigned)(WIFI_SCAN_MAX_AGE_MS / 1000));
    else
        R("[cmd] wifi        : liste yok%s",
          ws.hw ? "" : " (tarama derleme dışı, ENABLE_WIFI=0)");
}

/* ── Güç yönetimi ───────────────────────────────────────────────────────── */

static void cmd_power_status(void)
{
    power_stats_t st;
    power_service_stats(&st);
    R("[cmd] uyku ayarı  : %s", st.enabled ? "açık" : "kapalı (set power sleep 1)");
    R("[cmd] karar       : %s", st.why);
    R("[cmd] uyanma      : %s%s", st.wake_cause,
      st.timer_wake ? " (kısa pencere: iş bitince tekrar uyur)" : "");
    R("[cmd] uyku sayısı : %lu", (unsigned long)st.sleep_cycles);
    if (st.ign_off_ms >= 0)
        R("[cmd] kontak      : %lld sn'dir kapalı (uyku beklemesi %lu sn)",
          (long long)(st.ign_off_ms / 1000),
          (unsigned long)(POWER_SLEEP_DELAY_MS / 1000));
    else
        R("[cmd] kontak      : açık");
}

/* ── WiFi tarama (konumlama) ────────────────────────────────────────────── */

static void wifi_print_list(void)
{
    wifi_scan_ap_t aps[WIFI_SCAN_MAX_APS];
    uint32_t age = 0;
    size_t n = wifi_scan_service_get(aps, WIFI_SCAN_MAX_APS, &age);
    if (n == 0) { R("[cmd] (liste boş)"); return; }
    for (size_t i = 0; i < n; i++)
        R("[cmd]   %02X:%02X:%02X:%02X:%02X:%02X  %d dBm",
          aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
          aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5],
          (int)aps[i].rssi);
}

static void cmd_wifi_scan(void)
{
    R("[cmd] taranıyor (1-2 sn sürer)...");
    esp_err_t e = wifi_scan_service_scan_now();
    if (e == ESP_ERR_NOT_SUPPORTED) {
        R("[cmd] ERR: tarama derleme dışı (ENABLE_WIFI=0) — `wifi inject` kullanılabilir");
        return;
    }
    if (e != ESP_OK) {
        R("[cmd] ERR: %s (bağlanma sürüyor olabilir, tekrar deneyin)",
          esp_err_to_name(e));
        return;
    }
    R("[cmd] OK:");
    wifi_print_list();
}

static void cmd_wifi_status(void)
{
    wifi_scan_stats_t ws;
    wifi_scan_service_stats(&ws);
    R("[cmd] tarama    : %s", ws.hw ? "derlemede var" : "derleme dışı (ENABLE_WIFI=0)");
    R("[cmd] politika  : konum uydu değilken %u sn'de bir",
      (unsigned)(WIFI_SCAN_INTERVAL_MS / 1000));
    R("[cmd] sayaç     : %lu tarama, %lu başarısız, %lu inject",
      (unsigned long)ws.scans_ok, (unsigned long)ws.scans_failed,
      (unsigned long)ws.injects);
    if (ws.age_ms != UINT32_MAX) {
        R("[cmd] son liste : %u ağ, %lu sn önce", (unsigned)ws.last_count,
          (unsigned long)(ws.age_ms / 1000));
        wifi_print_list();
    } else {
        R("[cmd] son liste : yok");
    }
}

/* "AA:BB:CC:DD:EE:FF -62" biçiminde. MAC ':' ya da '-' ayraçlı. */
static void cmd_wifi_inject(char* mac_s, char* rssi_s)
{
    if (!mac_s || !rssi_s) {
        R("[cmd] ERR: wifi inject <AA:BB:CC:DD:EE:FF> <rssi>"); return;
    }

    uint8_t mac[6];
    const char* q = mac_s;
    for (int i = 0; i < 6; i++) {
        char* end = NULL;
        long v = strtol(q, &end, 16);
        if (end == q || v < 0 || v > 255 ||
            (i < 5 && *end != ':' && *end != '-') ||
            (i == 5 && *end != '\0')) {
            R("[cmd] ERR: MAC çözülemedi ('%s')", mac_s); return;
        }
        mac[i] = (uint8_t)v;
        q = end + 1;
    }

    long rssi = strtol(rssi_s, NULL, 10);
    if (rssi < -127 || rssi > 0) {
        R("[cmd] ERR: rssi -127..0 dBm arası olmalı"); return;
    }

    wifi_scan_service_inject(mac, (int8_t)rssi);
    wifi_scan_stats_t ws;
    wifi_scan_service_stats(&ws);
    R("[cmd] OK: listede %u ağ (sonraki rapora girer)", (unsigned)ws.last_count);
}

/* ── zaman / telemetri ──────────────────────────────────────────────────── */

static void cmd_time_status(void)
{
    int64_t now = time_service_now_ms();
    R("[cmd] kaynak   : %s", time_service_source());
    if (now > 0) {
        char iso[32];
        telemetry_format_iso8601(now, iso, sizeof(iso));
        R("[cmd] utc      : %s", iso);
    } else {
        R("[cmd] utc      : YOK — damga üretilemiyor, raporlar tamponda bekler");
    }
    /* uptime 64 bit; nano printf'te %lld güvenilir değil, saniyeye indiriyoruz. */
    R("[cmd] uptime   : %lu sn",
      (unsigned long)(time_service_uptime_ms() / 1000));
    R("[cmd] boot_id  : %08lX", (unsigned long)time_service_boot_id());
}

static void cmd_telemetry_status(void)
{
    telemetry_stats_t st;
    telemetry_service_stats(&st);
    R("[cmd] kontak     : %s%s", st.ignition ? "AÇIK" : "kapalı",
      (st.ignition_sim >= 0) ? " (elle sürülüyor)" : " (pin)");
    R("[cmd] periyot    : %d sn", st.ignition ? (TELEMETRY_PERIOD_MS / 1000)
                                              : (TELEMETRY_PERIOD_PARKED_MS / 1000));
    R("[cmd] sonraki    : %lu sn sonra", (unsigned long)(st.next_in_ms / 1000));
    R("[cmd] üretilen   : %lu rapor", (unsigned long)st.captured);
    R("[cmd] yazılamayan: %lu", (unsigned long)st.push_failed);
    R("[cmd] son rapor  : %lu CAN frame", (unsigned long)st.last_frames);
}

static void cmd_telemetry_ignition(const char* arg)
{
    if (!arg) { R("[cmd] ERR: telemetry ignition <0|1|auto>"); return; }
    if (strcasecmp(arg, "auto") == 0) {
        telemetry_service_set_ignition_sim(-1);
        R("[cmd] OK: kontak gerçek pinden okunuyor (GPIO %d)", PIN_IGNITION);
        return;
    }
    if (arg[0] != '0' && arg[0] != '1') {
        R("[cmd] ERR: telemetry ignition <0|1|auto>"); return;
    }
    telemetry_service_set_ignition_sim((int8_t)(arg[0] - '0'));
    R("[cmd] OK: kontak elle %s (rapor hemen üretilir)",
      arg[0] == '1' ? "AÇIK" : "kapalı");
}

/* ── Parser çekirdeği (taşıyıcıdan bağımsız) ────────────────────────────── */
static void exec_locked(char* line)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n' ||
                     line[n-1] == ' '  || line[n-1] == '\t')) line[--n] = '\0';
    if (n == 0) return;

    char* save = NULL;
    char* tok  = strtok_r(line, " \t", &save);
    if (!tok) return;

    if (strcasecmp(tok, "help") == 0) { cmd_help(); return; }
    if (strcasecmp(tok, "info") == 0) { cmd_info(); return; }
    if (strcasecmp(tok, "selftest") == 0 || strcasecmp(tok, "test") == 0) {
        selftest_run(s_reply, s_reply_ctx);
        return;
    }

    if (strcasecmp(tok, "get") == 0) {
        char* what = strtok_r(NULL, " \t", &save);
        if (what && strcasecmp(what, "config") == 0) cmd_get_config();
        else R("[cmd] ERR: get config");
        return;
    }

    if (strcasecmp(tok, "set") == 0) {
        cmd_set(strtok_r(NULL, " \t", &save), &save);
        return;
    }

    if (strcasecmp(tok, "config") == 0) {
        char* what = strtok_r(NULL, " \t", &save);
        if (what && strcasecmp(what, "clear") == 0) {
            config_store_erase_all();
            R("[cmd] OK: ayarlar silindi, reboot gerekli");
        } else R("[cmd] ERR: config clear");
        return;
    }

    if (strcasecmp(tok, "can") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: can inject|stats|dump|clear"); return; }
        if      (strcasecmp(sub, "inject") == 0) cmd_can_inject(strtok_r(NULL, " \t", &save));
        else if (strcasecmp(sub, "stats")  == 0) cmd_can_stats();
        else if (strcasecmp(sub, "dump")   == 0) cmd_can_dump(strtok_r(NULL, " \t", &save));
        else if (strcasecmp(sub, "clear")  == 0) { can_service_clear(); R("[cmd] OK: tablo boşaltıldı"); }
        else R("[cmd] ERR: can inject|stats|dump|clear");
        return;
    }

    if (strcasecmp(tok, "net") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: net status|offline|reconnect"); return; }
        if      (strcasecmp(sub, "status")  == 0) cmd_net_status();
        else if (strcasecmp(sub, "offline") == 0) cmd_net_offline(strtok_r(NULL, " \t", &save));
        else if (strcasecmp(sub, "reconnect") == 0) {
            net_service_reconnect_now();
            R("[cmd] OK: bekleme sıfırlandı");
        }
        else if (strcasecmp(sub, "modem") == 0) {
            /* "modem bağlanmıyor" tek başına işe yaramaz — hangi adımda
             * takıldığı ekransız cihazda tek teşhis yolu. */
            R("[cmd] adım      : %s", modem_backend_step());
            int csq = modem_backend_csq();
            if (csq >= 0) R("[cmd] sinyal    : %d/31 (%d dBm)", csq,
                            -113 + 2 * csq);
            else          R("[cmd] sinyal    : bilinmiyor");
            const char* im = modem_backend_imei();
            R("[cmd] imei      : %s", im[0] ? im : "<modemden okunmadı>");
            const char* e = modem_backend_last_error();
            if (e[0]) R("[cmd] son hata  : %s", e);
        }
        else R("[cmd] ERR: net status|offline|reconnect|modem");
        return;
    }

    if (strcasecmp(tok, "mqtt") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (sub && strcasecmp(sub, "status") == 0) cmd_mqtt_status();
        else R("[cmd] ERR: mqtt status");
        return;
    }

    if (strcasecmp(tok, "sf") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: sf stats|push|peek|clear"); return; }
        if      (strcasecmp(sub, "stats") == 0) cmd_sf_stats();
        else if (strcasecmp(sub, "push")  == 0) cmd_sf_push(rest_of_line(&save));
        else if (strcasecmp(sub, "peek")  == 0) cmd_sf_peek();
        else if (strcasecmp(sub, "clear") == 0) {
            store_forward_clear();
            R("[cmd] OK: bekleyenler gönderilmiş sayıldı");
        }
        else R("[cmd] ERR: sf stats|push|peek|clear");
        return;
    }

    if (strcasecmp(tok, "ota") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: ota status|check|install"); return; }
        if (strcasecmp(sub, "status") == 0) {
            ota_status_t o;
            ota_service_status(&o);
            R("[cmd] durum     : %s", ota_service_state_name(o.state));
            R("[cmd] bu sürüm  : %s (kod %d)", OS_VERSION_STR, OS_VERSION_CODE);
            if (o.latest_code)
                R("[cmd] sunucuda  : %s (kod %ld)%s", o.latest_version,
                  (long)o.latest_code, o.forced ? " [ZORUNLU]" : "");
            if (o.state == OTA_DOWNLOADING)
                R("[cmd] ilerleme  : %%%u", (unsigned)o.progress_pct);
            /* Geri alma durumu ekransız cihazda kritik: bu açılış aday mı? */
            if (o.pending_verify)
                R("[cmd] ⚠ DOĞRULAMA BEKLİYOR — geçerlenmezse sonraki "
                  "açılışta eski sürüme dönülür");
            else
                R("[cmd] geri alma: yok (firmware geçerli)");
            R("[cmd] kontrol   : %lu (başarısız %lu)",
              (unsigned long)o.checks, (unsigned long)o.failures);
            if (o.error[0]) R("[cmd] son hata  : %s", o.error);
        } else if (strcasecmp(sub, "check") == 0) {
            esp_err_t e = ota_service_check_now();
            if (e == ESP_OK) R("[cmd] OK: kontrol sıraya alındı");
            else R("[cmd] ERR: ağ yok");
        } else if (strcasecmp(sub, "install") == 0) {
            /* Elle kurulum park penceresini ATLAR — kurulumcu ne yaptığını
             * biliyor demektir. */
            esp_err_t e = ota_service_install_now();
            if (e == ESP_OK) R("[cmd] OK: kurulum başlıyor (pencere atlanıyor)");
            else if (e == ESP_ERR_NOT_FOUND) R("[cmd] ERR: bekleyen güncelleme yok");
            else R("[cmd] ERR: ağ yok");
        } else R("[cmd] ERR: ota status|check|install");
        return;
    }

    if (strcasecmp(tok, "gnss") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: gnss status|inject"); return; }
        if      (strcasecmp(sub, "status") == 0) cmd_gnss_status();
        else if (strcasecmp(sub, "inject") == 0) cmd_gnss_inject(rest_of_line(&save));
        else R("[cmd] ERR: gnss status|inject");
        return;
    }

    if (strcasecmp(tok, "pos") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (sub && strcasecmp(sub, "status") == 0) cmd_pos_status();
        else R("[cmd] ERR: pos status");
        return;
    }

    if (strcasecmp(tok, "power") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (sub && strcasecmp(sub, "status") == 0) { cmd_power_status(); return; }
        if (sub && strcasecmp(sub, "sleep") == 0) {
            char* n = strtok_r(NULL, " \t", &save);
            if (n && strcasecmp(n, "now") == 0) {
                R("[cmd] uyuyor — konsol kapanacak; kontak ya da zamanlayıcı "
                  "uyandırır");
                power_service_sleep_now();   /* geri dönmez */
                return;
            }
        }
        R("[cmd] ERR: power status|sleep now");
        return;
    }

    if (strcasecmp(tok, "wifi") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: wifi scan|status|inject|clear"); return; }
        if      (strcasecmp(sub, "scan") == 0)   cmd_wifi_scan();
        else if (strcasecmp(sub, "status") == 0) cmd_wifi_status();
        else if (strcasecmp(sub, "inject") == 0)
            cmd_wifi_inject(strtok_r(NULL, " \t", &save),
                            strtok_r(NULL, " \t", &save));
        else if (strcasecmp(sub, "clear") == 0) {
            wifi_scan_service_clear(); R("[cmd] OK: liste boşaltıldı");
        }
        else R("[cmd] ERR: wifi scan|status|inject|clear");
        return;
    }

    if (strcasecmp(tok, "led") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: led status|test"); return; }
        if (strcasecmp(sub, "status") == 0) {
            led_state_t s = status_led_current();
            R("[cmd] gösterilen: %s", status_led_state_name(s));
            if (PIN_STATUS_LED < 0)
                R("[cmd] ⚠ LED yok (PIN_STATUS_LED = -1) — yalnız mantık çalışıyor");
            else
                R("[cmd] pin       : GPIO %d", PIN_STATUS_LED);
        } else if (strcasecmp(sub, "test") == 0) {
            /* Kurulumcunun LED'i fiziksel olarak bulması için: sürekli yanık
             * desen kırpmalar arasında aranmaktan çok daha görünür. */
            char* v = strtok_r(NULL, " \t", &save);
            long sn = v ? strtol(v, NULL, 10) : 5;
            if (sn < 1 || sn > 60) sn = 5;
            status_led_force(LED_STATE_ERROR, (uint32_t)sn * 1000);
            R("[cmd] OK: LED %ld sn sürekli yanacak, sonra otomatiğe dönecek", sn);
        } else R("[cmd] ERR: led status|test");
        return;
    }

    if (strcasecmp(tok, "time") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (sub && strcasecmp(sub, "status") == 0) cmd_time_status();
        else R("[cmd] ERR: time status");
        return;
    }

    if (strcasecmp(tok, "telemetry") == 0) {
        char* sub = strtok_r(NULL, " \t", &save);
        if (!sub) { R("[cmd] ERR: telemetry now|status|ignition"); return; }
        if (strcasecmp(sub, "now") == 0) {
            esp_err_t e = telemetry_service_capture_now();
            if (e == ESP_OK) R("[cmd] OK: rapor üretildi ve tampona yazıldı");
            else R("[cmd] ERR: üretilemedi (%s)", esp_err_to_name(e));
        }
        else if (strcasecmp(sub, "status")   == 0) cmd_telemetry_status();
        else if (strcasecmp(sub, "ignition") == 0)
            cmd_telemetry_ignition(strtok_r(NULL, " \t", &save));
        else R("[cmd] ERR: telemetry now|status|ignition");
        return;
    }

    if (strcasecmp(tok, "reboot") == 0) {
        R("[cmd] OK: yeniden başlatılıyor...");
        vTaskDelay(pdMS_TO_TICKS(200));   /* yanıt kanaldan çıksın */
        esp_restart();
    }

    R("[cmd] ERR: bilinmeyen komut '%s' (help)", tok);
}

void command_service_exec_ex(char* line, cmd_reply_fn_t reply, void* ctx)
{
    if (!line) return;
    if (!s_exec_mutex) s_exec_mutex = xSemaphoreCreateMutex();
    if (s_exec_mutex) xSemaphoreTake(s_exec_mutex, portMAX_DELAY);

    s_reply     = reply;
    s_reply_ctx = ctx;
    exec_locked(line);
    s_reply     = NULL;
    s_reply_ctx = NULL;

    if (s_exec_mutex) xSemaphoreGive(s_exec_mutex);
}

void command_service_exec(char* line)
{
    command_service_exec_ex(line, serial_reply, NULL);
}

/* ── UART okuyucu görevi ────────────────────────────────────────────────── */
static void command_task(void* arg)
{
    (void)arg;
    static char line[CMD_LINE_MAX];
    size_t pos = 0;

    for (;;) {
        uint8_t ch;
        int r = uart_read_bytes(CMD_UART, &ch, 1, portMAX_DELAY);
        if (r <= 0) continue;

        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                line[pos] = '\0';
                command_service_exec(line);
                pos = 0;
            }
        } else if (pos < CMD_LINE_MAX - 1) {
            line[pos++] = (char)ch;
        } else {
            pos = 0;   /* taşan satırı at */
            printf("[cmd] ERR: satır çok uzun\n");
        }
    }
}

esp_err_t command_service_init(void)
{
    if (!s_exec_mutex) s_exec_mutex = xSemaphoreCreateMutex();

    /* RX için sürücü kur — TX buffer 0: log/printf yolu değişmez.
     * Konsol UART'ı boot'ta zaten yapılandırılmış (115200). */
    esp_err_t err = uart_driver_install(CMD_UART, 512, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install başarısız: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t r = xTaskCreatePinnedToCore(
        command_task, "cmd_svc", 4096, NULL, 3, NULL, NET_TASK_CORE);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "görev oluşturulamadı");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "init OK (USB seri, satır tabanlı; 'help' yazın)");
    return ESP_OK;
}
