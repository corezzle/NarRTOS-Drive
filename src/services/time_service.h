#pragma once
/**
 * @brief Duvar saati — telemetri damgasının kaynağı.
 *
 * Cihazda RTC YOK. Bu servisin varlık sebebi tek bir alan: telemetri dokümanının
 * `dateTime`'ı. Sunucu tarih aralığı sorgularını bununla yapıyor;
 * yanlış bir damga güzergâhı, hız raporunu ve kontak
 * raporunu aynı anda bozar.
 *
 * ── Saat kaynakları, gelme sırasıyla ──────────────────────────────────────
 *   SNTP    ağ üzerinden, BUGÜN çalışan tek kaynak
 *   GNSS    M5 — asıl saha kaynağı olacak, şebeke gerekmez
 *   modem   M7 — şebeke saati, GNSS fix'i yokken yedek
 *
 * GNSS ve modem geldiğinde `time_service_set_wall_ms()` ile besleyecekler;
 * SNTP'ye özel bir yol yok, kaynak adı sadece teşhis için tutuluyor.
 *
 * ── MONOTON SAAT ayrı tutuluyor ───────────────────────────────────────────
 *
 * `esp_timer_get_time()` boot'tan beri artan monoton saat; duvar saati ise
 * senkronda ileri/geri SIÇRAR. Telemetri kaydı ikisini birden saklıyor:
 *
 *   - kayıt anında duvar saati BİLİNİYORSA doğrudan yazılır (en iyi hâl)
 *   - bilinmiyorsa monoton damga + boot kimliği yazılır; gönderim anında
 *     saat öğrenilmişse damga geriye doğru hesaplanır
 *
 * Kapalı otoparkta kontak açılıp 20 dakika veri toplayan, sonra açık havaya
 * çıkıp şebekeye giren araç tam bu yüzden doğru damgalanabiliyor.
 *
 * ── boot_id neden var ─────────────────────────────────────────────────────
 *
 * Monoton saat her boot'ta sıfırlanır. Yeniden başlatmadan ÖNCE yazılmış,
 * damgasız bir kaydın monoton değeri yeni oturumda anlamsızdır — geriye
 * hesaplamak rastgele bir tarih üretirdi. Her boot'ta üretilen rastgele
 * kimlik bu kayıtları ayırt ediyor; oturumu tutmayan ve damgası olmayan kayıt
 * ARTIK ASLA damgalanamaz, gönderilmez (bkz. telemetry_codec).
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** boot kimliğini üret ve ağ durumuna abone ol. net_service'ten SONRA. */
esp_err_t time_service_init(void);

/** Duvar saati güvenilir mi (bir kez bile senkron oldu mu). */
bool time_service_valid(void);

/** UTC, epoch'tan beri milisaniye. Geçerli değilse 0. */
int64_t time_service_now_ms(void);

/** Boot'tan beri milisaniye — monoton, sıçramaz. */
int64_t time_service_uptime_ms(void);

/** Bu açılışa özgü rastgele kimlik. Asla 0 değil. */
uint32_t time_service_boot_id(void);

/** Teşhis: "yok" | "sntp" | "gnss" | "modem" | "elle" */
const char* time_service_source(void);

/** Saati dışarıdan besle — GNSS (M5) ve modem (M7) buradan girecek.
 *  @param utc_ms epoch UTC milisaniye
 *  @param source teşhis için kaynak adı (kalıcı bir dize olmalı) */
esp_err_t time_service_set_wall_ms(int64_t utc_ms, const char* source);

/* ISO 8601 biçimlendirme burada DEĞİL, telemetry_codec.h'de. Sebebi: damgayı
 * üreten kod host testinde birebir çalıştırılabilmeli, bu dosya ise SNTP ve
 * ESP-IDF ağ yığınına bağlı. */

#ifdef __cplusplus
}
#endif
