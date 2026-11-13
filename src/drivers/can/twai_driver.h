#pragma once
/**
 * @brief TWAI (CAN 2.0B) sürücüsü — ESP32-S3'ün dahili denetleyicisi.
 *
 * ⚠ Dışarıdan bir hat sürücüsü (transceiver) ŞART: SN65HVD230, TJA1050 vb.
 * ESP32-S3'te denetleyici var ama fiziksel katman yok. Çıplak bağlarsan
 * ne hat çalışır ne de pin sağ kalır.
 *
 * Sürücü yalnız ALIR (dinleme modu değil, normal mod ama TX kuyruğu 0).
 * Araç veri yoluna yazmak bilinçli olarak kapalı: takip cihazının aracın
 * hattına frame basması hem gereksiz hem riskli.
 *
 * ENABLE_CAN=0 iken bu dosya boş çevrilir. Donanım yokken `can_service`
 * `can inject` ile beslenir — servis donanımdan bağımsız.
 */

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TWAI'yi kur ve alıcı görevini başlat. Alınan her frame
 *  can_service_ingest()'e verilir.
 *  @param kbps hat hızı (250 / 500 / 125 / 1000)
 *  @return ESP_ERR_NOT_SUPPORTED — ENABLE_CAN=0 ile derlendiyse */
esp_err_t can_twai_start(uint32_t kbps);

/** Sürücüyü durdur ve kaldır. */
esp_err_t can_twai_stop(void);

/** Sürücü çalışıyor mu (bus-off durumunda da true döner; durum için
 *  can_twai_bus_state kullanın). */
bool can_twai_running(void);

/** 0=stopped 1=running 2=bus-off 3=recovering, -1=sürücü yok */
int can_twai_bus_state(void);

#ifdef __cplusplus
}
#endif
