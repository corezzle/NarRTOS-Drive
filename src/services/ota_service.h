#pragma once
/**
 * @brief OTA — kablosuz firmware güncelleme.
 *
 * Mekanizma Wear ile aynı (HTTP manifest → esp_https_ota → A/B partisyon),
 * POLİTİKA tamamen farklı. Fark tek bir gerçekten çıkıyor: **bu cihaza
 * fiziksel olarak ulaşılamıyor.** Wear'da bozuk bir güncelleme kullanıcıyı
 * rahatsız eder ve kullanıcı cihazı USB'ye takar. Drive aracın altında,
 * ekransız, belki başka şehirde.
 *
 * ── 1. GERİ ALMA (rollback) AÇIK ──────────────────────────────────────────
 *
 * Wear'da kapalıydı. Drive'da açık: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.
 * Yeni firmware kendini "geçerli" işaretlemezse, bir sonraki açılışta
 * bootloader eski sürüme döner. Açılışta çöken bir güncelleme cihazı sahada
 * tuğlaya çevirmek yerine kendiliğinden geri alınıyor.
 *
 * ── 2. "GEÇERLİ" ÖLÇÜTÜ ───────────────────────────────────────────────────
 *
 * İki koşuldan biri yeter, hangisi önce olursa:
 *
 *   a) telemetri yayını PUBACK aldı — güçlü kanıt, boru hattı uçtan uca
 *      çalışıyor demek
 *   b) firmware OTA_VALIDATE_UPTIME_MS boyunca çökmeden çalıştı
 *
 * Yalnız (a) olsaydı: broker bakımdayken güncellenen cihaz kendini asla
 * geçerlemez, ilk elektrik kesintisinde eski sürüme döner ve güncelleme
 * döngüsüne girer. Broker arızası firmware hatası değildir.
 *
 * Yalnız (b) olsaydı: açılan ama işe yaramayan bir firmware geçerli sayılırdı.
 * (a) bunu erkenden yakalıyor.
 *
 * Geri almanın gerçekten koruduğu şey açılış döngüleri ve erken çökmeler —
 * ikisini de (b) yakalıyor; (a) ise iyi durumda doğrulamayı hızlandırıyor.
 *
 * ── 3. KURULUM PENCERESİ: ARAÇ PARK HÂLİNDEYKEN ───────────────────────────
 *
 * Wear'ın karşılığı "gece penceresi"ydi (kullanıcı uyurken). Drive'ın elinde
 * daha iyi bir sinyal var: KONTAK. Yol ortasında yeniden başlatmak canlı
 * takibi ~30 saniye kör bırakır ve dispatcher haritada aracı kaybeder.
 *
 * Kontak kapalıyken kurulum yapılıyor. `forced` manifest bayrağı bu pencereyi
 * atlıyor — kritik bir güvenlik yaması park saatini bekleyemez.
 *
 * Kurulum sırasında kontak açılırsa tehlike yok: A/B düzeni sayesinde yarım
 * yazılan pasif bölüm hiç devreye alınmaz, cihaz eski firmware'le çalışmaya
 * devam eder.
 *
 * ── 4. Tampon güncellemeden ETKİLENMEZ ────────────────────────────────────
 *
 * `buffer` bölümü (0x590000) app0/app1'in dışında; OTA pasif app yuvasına
 * yazıyor. Bekleyen raporlar güncellemeden sağ çıkıyor — Session 4'te
 * yeniden başlatmayla donanımda doğrulandı.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,      /**< yeni sürüm var, kurulum penceresi bekleniyor */
    OTA_DOWNLOADING,
    OTA_INSTALLED,      /**< yazıldı, yeniden başlatma bekleniyor */
    OTA_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    char        latest_version[24];
    int32_t     latest_code;
    bool        forced;
    uint8_t     progress_pct;
    char        error[64];
    uint32_t    checks;
    uint32_t    failures;
    /** Bu açılış geri alma adayı mı (henüz geçerlenmedi). */
    bool        pending_verify;
    bool        validated;
} ota_status_t;

/** Servisi başlat ve geri alma doğrulamasını yürüt.
 *  mqtt_service ve telemetry_service'ten SONRA çağrılmalı. */
esp_err_t ota_service_init(void);

/** Manifest'i şimdi kontrol et (elle tetikleme). Ağ gerekir. */
esp_err_t ota_service_check_now(void);

/** Bekleyen güncellemeyi pencereyi beklemeden kur (elle tetikleme). */
esp_err_t ota_service_install_now(void);

void ota_service_status(ota_status_t* out);
const char* ota_service_state_name(ota_state_t s);

#ifdef __cplusplus
}
#endif
