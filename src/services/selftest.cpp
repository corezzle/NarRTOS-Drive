/**
 * @file selftest.cpp
 * @brief Drive donanım self-test checklist'i (bkz. header).
 *
 * Her test bir satır üretir: "[DURUM] ad  detay". Donanımı/derlemesi olmayan
 * çevrebirimler ATLA. Tüm problar salt-okunur ya da komut arayüzündeki mevcut
 * güvenli API'ler (wifi scan, led test) — çalışan servisleri bozmaz.
 */

#include "selftest.h"

#include "../core/os_config.h"
#include "store_forward.h"
#include "gnss_service.h"
#include "telemetry_service.h"
#include "wifi_scan_service.h"
#include "status_led.h"
#include "../drivers/can/twai_driver.h"
#include "../drivers/imu/icm42688p.h"
#include "../drivers/net/net_backend.h"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if CONFIG_SPIRAM
#include <esp_psram.h>
#endif

typedef enum { ST_PASS, ST_FAIL, ST_WARN, ST_SKIP } st_res_t;

static cmd_reply_fn_t s_rep = NULL;
static void*          s_ctx = NULL;
static int            s_n[4];   /* PASS/FAIL/WARN/SKIP sayaçları */

static const char* tag(st_res_t r)
{
    return r == ST_PASS ? "PASS" : r == ST_FAIL ? "FAIL"
         : r == ST_WARN ? "UYARI" : "ATLA";
}

/* Bir satır bas + sayacı artır. */
static void row(st_res_t r, const char* name, const char* fmt, ...)
{
    char d[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d, sizeof(d), fmt, ap);
    va_end(ap);

    char line[256];
    snprintf(line, sizeof(line), "[%-5s] %-18s %s", tag(r), name, d);
    if (s_rep) s_rep(line, s_ctx);
    s_n[r]++;
}

/* Düz satır (başlık/özet). */
static void say(const char* s)
{
    if (s_rep) s_rep(s, s_ctx);
}

/* ── Testler ─────────────────────────────────────────────────────────────── */

static void t_info(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    row(ST_PASS, "Kart bilgisi",
        "%s v%s, ESP32-S3 rev%d %dC, MAC %02X%02X%02X%02X%02X%02X, reset=%d, heap %lu KB",
        NARRTOS_PRODUCT, OS_VERSION_STR, ci.revision, ci.cores,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (int)esp_reset_reason(),
        (unsigned long)(esp_get_free_heap_size() / 1024));
}

static void t_mem(void)
{
    uint32_t fsz = 0;
    esp_flash_get_size(NULL, &fsz);
    size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t freeb = esp_get_free_heap_size();
#if CONFIG_SPIRAM
    size_t psz = esp_psram_get_size();
    row(psz ? ST_PASS : ST_WARN, "Bellek",
        "flash %lu MB, PSRAM %lu MB, bos heap %lu KB, en buyuk blok %lu KB",
        (unsigned long)(fsz / (1024 * 1024)), (unsigned long)(psz / (1024 * 1024)),
        (unsigned long)(freeb / 1024), (unsigned long)(big / 1024));
#else
    row(ST_PASS, "Bellek",
        "flash %lu MB, PSRAM yok/kapali, bos heap %lu KB, en buyuk blok %lu KB",
        (unsigned long)(fsz / (1024 * 1024)),
        (unsigned long)(freeb / 1024), (unsigned long)(big / 1024));
#endif
}

static void t_sf(void)
{
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, SF_PARTITION_LABEL);
    sf_stats_t st;
    store_forward_stats(&st);

    if (!p)
        row(ST_FAIL, "Store&forward",
            "'%s' bolumu YOK (partitions_drive.csv)", SF_PARTITION_LABEL);
    else if (st.capacity == 0)
        row(ST_FAIL, "Store&forward",
            "bolum %lu KB ama tampon baslatilamadi", (unsigned long)(p->size / 1024));
    else
        row(ST_PASS, "Store&forward",
            "'%s' %lu KB, %lu slot, bekleyen %lu, bozuk %lu",
            SF_PARTITION_LABEL, (unsigned long)(p->size / 1024),
            (unsigned long)st.capacity, (unsigned long)st.stored,
            (unsigned long)st.corrupt);
}

static void t_imu(void)
{
#if ENABLE_IMU
    if (imu_running()) {
        row(ST_PASS, "IMU ICM42688P", "calisiyor");
    } else {
        esp_err_t e = imu_start();
        if (e == ESP_OK)       row(ST_PASS, "IMU ICM42688P", "WHO_AM_I dogrulandi");
        else if (e == ESP_ERR_NOT_SUPPORTED)
                               row(ST_SKIP, "IMU ICM42688P", "ENABLE_IMU=0");
        else                   row(ST_FAIL, "IMU ICM42688P",
                                   "baslatilamadi: %s (I2C SDA%d/SCL%d, 0x%02X)",
                                   esp_err_to_name(e), PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_ADDR);
    }
#else
    row(ST_SKIP, "IMU ICM42688P", "ENABLE_IMU=0 (donanim yok)");
#endif
}

static void t_can(void)
{
#if ENABLE_CAN
    int bus = can_twai_bus_state();
    if (bus == 1)      row(ST_PASS, "CAN / TWAI", "hat calisiyor (GPIO TX%d/RX%d)", PIN_CAN_TX, PIN_CAN_RX);
    else if (bus < 0)  row(ST_FAIL, "CAN / TWAI", "surucu yok (ENABLE_CAN acik ama baslamadi)");
    else               row(ST_WARN, "CAN / TWAI", "durum=%d (bus-off/durdu) - transceiver/hat?", bus);
#else
    row(ST_SKIP, "CAN / TWAI", "ENABLE_CAN=0 (transceiver yok)");
#endif
}

static void t_gnss(void)
{
#if ENABLE_GNSS
    gnss_stats_t g;
    gnss_service_stats(&g);
    if (g.rx_sentences > 0)
        row(g.has_fix ? ST_PASS : ST_WARN, "GNSS",
            "%lu cumle, uydu %u, sabitleme %s",
            (unsigned long)g.rx_sentences, (unsigned)g.sats, g.has_fix ? "VAR" : "yok");
    else
        row(ST_WARN, "GNSS", "hic NMEA cumlesi yok (GPIO RX%d) - anten/baud/baglanti?", PIN_GNSS_RX);
#else
    row(ST_SKIP, "GNSS", "ENABLE_GNSS=0 (modul yok)");
#endif
}

static void t_modem(void)
{
#if ENABLE_MODEM
    const char* imei = modem_backend_imei();
    int csq = modem_backend_csq();
    if (imei && imei[0])
        row(ST_PASS, "Modem (hucresel)", "IMEI %s, sinyal %d/31, adim '%s'",
            imei, csq, modem_backend_step());
    else
        row(ST_WARN, "Modem (hucresel)", "AT'ye cevap yok/IMEI okunmadi - adim '%s', son hata '%s'",
            modem_backend_step(), modem_backend_last_error());
#else
    row(ST_SKIP, "Modem (hucresel)", "ENABLE_MODEM=0 (donanim yok)");
#endif
}

static void t_wifi(void)
{
#if ENABLE_WIFI
    esp_err_t e = wifi_scan_service_scan_now();
    if (e == ESP_ERR_NOT_SUPPORTED) {
        row(ST_SKIP, "Wi-Fi tarama", "derleme disi");
        return;
    }
    if (e != ESP_OK) {
        row(ST_WARN, "Wi-Fi tarama", "tarama hatasi: %s (baglanma suruyor olabilir)", esp_err_to_name(e));
        return;
    }
    wifi_scan_ap_t aps[WIFI_SCAN_MAX_APS];
    uint32_t age = 0;
    size_t n = wifi_scan_service_get(aps, WIFI_SCAN_MAX_APS, &age);
    int best = -127;
    for (size_t i = 0; i < n; i++) if (aps[i].rssi > best) best = aps[i].rssi;
    if (n == 0)        row(ST_WARN, "Wi-Fi tarama", "hic ag bulunamadi - anten?");
    else if (best < -85) row(ST_WARN, "Wi-Fi tarama", "%u ag, en iyi %d dBm - zayif (anten?)", (unsigned)n, best);
    else               row(ST_PASS, "Wi-Fi tarama", "%u ag, en iyi %d dBm", (unsigned)n, best);
#else
    row(ST_SKIP, "Wi-Fi tarama", "ENABLE_WIFI=0");
#endif
}

static void t_ignition(void)
{
    /* Pini yeniden yapılandırmıyoruz; telemetry servisinin okuduğu durumu
     * raporluyoruz (çalışan servisi bozmamak için). */
    telemetry_stats_t ts;
    telemetry_service_stats(&ts);
    row(ST_PASS, "Kontak (ignition)", "GPIO%d = %s%s",
        PIN_IGNITION, ts.ignition ? "ACIK" : "kapali",
        ts.ignition_sim >= 0 ? " (elle suruluyor)" : " (pin)");
}

static void t_led(void)
{
    if (PIN_STATUS_LED < 0) {
        row(ST_SKIP, "Durum LED", "PIN_STATUS_LED=-1 (LED yok)");
        return;
    }
    led_state_t s = status_led_current();
    status_led_force(LED_STATE_ERROR, 1500);   /* `led test` ile ayni güvenli yol */
    row(ST_PASS, "Durum LED", "GPIO%d, su an '%s', 1.5 sn test yakildi (WS2812 ise desen farkli)",
        PIN_STATUS_LED, status_led_state_name(s));
}

/* ── Orkestratör ─────────────────────────────────────────────────────────── */

void selftest_run(cmd_reply_fn_t reply, void* ctx)
{
    s_rep = reply;
    s_ctx = ctx;
    s_n[0] = s_n[1] = s_n[2] = s_n[3] = 0;

    say("=== NarRTOS-Drive donanim self-test ===");
    t_info();
    t_mem();
    t_sf();
    t_imu();
    t_can();
    t_gnss();
    t_modem();
    t_wifi();
    t_ignition();
    t_led();

    char sum[96];
    snprintf(sum, sizeof(sum), "=== SONUC: %d PASS, %d FAIL, %d UYARI, %d ATLA ===",
             s_n[ST_PASS], s_n[ST_FAIL], s_n[ST_WARN], s_n[ST_SKIP]);
    say(sum);

    s_rep = NULL;
    s_ctx = NULL;
}
