/**
 * @file can_service.cpp
 * @brief Kimlik başına en son frame tablosu (bkz. header).
 *
 * Tasarım notları:
 *  - Doğrusal arama. Tablo 96 girdilik ve tipik araç 30-80 kimlik yayınlar;
 *    hash tablosu kurmanın karmaşıklığı bu boyutta kendini amorti etmiyor.
 *    Ölçüm gerekirse `rx_total` ile CPU yükü izlenebilir.
 *  - Mutex ile korunuyor: TWAI görevi (çekirdek 1, öncelik 6) ile komut
 *    görevi (çekirdek 0, öncelik 3) aynı anda yazabilir.
 *  - Tablo dolduğunda YENİ kimlik atılır, mevcutlar korunur. Alternatifi
 *    en eskiyi atmaktı ama o, nadir ama önemli bir PGN'i (ör. arıza kodu)
 *    sürekli yayın yapan bir PGN uğruna düşürürdü. Atılan sayılıyor;
 *    `can stats` ile görülür, tablo küçük kalırsa büyütülür.
 */

#include "can_service.h"
#include "command_service.h"
#include "../core/os_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static const char* TAG = "can";

static can_frame_t       s_tbl[CAN_ID_TABLE_SIZE];
static uint16_t          s_used   = 0;
static uint32_t          s_rx     = 0;
static uint32_t          s_drop   = 0;
static SemaphoreHandle_t s_mutex  = NULL;

/* command_service'in `can inject` komutundan gelen frame'ler buraya düşer —
 * gerçek hattan gelenlerle aynı kapı. Donanımsız test bu yüzden anlamlı:
 * enjekte edilen frame tabloda gerçek frame gibi durur. */
static void inject_sink(uint32_t id, bool ext, const uint8_t* data, uint8_t dlc)
{
    can_service_ingest(id, ext, data, dlc);
}

esp_err_t can_service_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    memset(s_tbl, 0, sizeof(s_tbl));
    s_used = 0; s_rx = 0; s_drop = 0;

    command_service_set_can_sink(inject_sink);
    ESP_LOGI(TAG, "init OK (tablo %d kimlik, kimlik başına son frame)",
             CAN_ID_TABLE_SIZE);
    return ESP_OK;
}

void can_service_ingest(uint32_t id, bool ext, const uint8_t* data, uint8_t dlc)
{
    if (dlc > 8) dlc = 8;
    if (!s_mutex) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_rx++;

    can_frame_t* slot = NULL;
    for (uint16_t i = 0; i < s_used; i++) {
        if (s_tbl[i].id == id && s_tbl[i].ext == ext) { slot = &s_tbl[i]; break; }
    }

    if (!slot) {
        if (s_used >= CAN_ID_TABLE_SIZE) {
            /* Tablo dolu — yeni kimliği at, mevcutları koru. Bkz. dosya başı. */
            s_drop++;
            xSemaphoreGive(s_mutex);
            return;
        }
        slot = &s_tbl[s_used++];
        slot->id    = id;
        slot->ext   = ext;
        slot->count = 0;
    }

    if (dlc) memcpy(slot->data, data, dlc);
    slot->dlc     = dlc;
    slot->last_us = esp_timer_get_time();
    slot->count++;

    xSemaphoreGive(s_mutex);
}

size_t can_service_snapshot(can_frame_t* out, size_t max)
{
    if (!out || !max || !s_mutex) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* En taze kimlikten başlayarak seç. Rapor `max` ile sınırlıysa en güncel
     * veriler gitsin — eski bir PGN'i taze olanın önüne koymanın anlamı yok.
     * Seçim sırasında kopyalıyoruz; tablo küçük, sıralama maliyeti önemsiz. */
    size_t n = (s_used < max) ? s_used : max;
    bool taken[CAN_ID_TABLE_SIZE] = {false};

    for (size_t k = 0; k < n; k++) {
        int best = -1;
        for (uint16_t i = 0; i < s_used; i++) {
            if (taken[i]) continue;
            if (best < 0 || s_tbl[i].last_us > s_tbl[best].last_us) best = i;
        }
        if (best < 0) { n = k; break; }
        taken[best] = true;
        out[k] = s_tbl[best];
    }

    xSemaphoreGive(s_mutex);
    return n;
}

void can_service_stats(can_stats_t* out)
{
    if (!out) return;
    if (!s_mutex) { memset(out, 0, sizeof(*out)); return; }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->rx_total     = s_rx;
    out->dropped_full = s_drop;
    out->distinct     = s_used;
    out->capacity     = CAN_ID_TABLE_SIZE;
    xSemaphoreGive(s_mutex);
}

void can_service_clear(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_tbl, 0, sizeof(s_tbl));
    s_used = 0; s_rx = 0; s_drop = 0;
    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "tablo boşaltıldı");
}
