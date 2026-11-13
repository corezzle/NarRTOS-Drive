#pragma once
/**
 * @brief AT komut istemcisi ve yanıt ayrıştırıcıları — saf, host'ta test edilir.
 *
 * Modem donanımı yok. Bu modül taşıyıcıyı bir arayüzün arkasına aldığı için
 * modemsiz de baştan sona sınanabiliyor: host testi senaryolu yanıtlar
 * besliyor, gerçek hedefte aynı arayüzü UART dolduruyor. store_forward'ın
 * sahte flash'ıyla aynı fikir.
 *
 * Modülün taşıdığı iş, modem sürücüsünün en çok hata çıkan kısmı:
 *
 *   - yanıt sonunu doğru tanımak (OK / ERROR / +CME ERROR / +CMS ERROR)
 *   - bilgi satırlarını sonuç kodundan ayırmak
 *   - yankıyı (echo) ve boş satırları elemek
 *   - sayıları ve tarihi doğru çözmek
 *
 * ── Neden kendi AT katmanımız ─────────────────────────────────────────────
 *
 * `esp_modem` bileşeni bu ESP-IDF kurulumunda yok (kayıt defterinden gelen
 * yönetilen bileşen). Donanım da olmadığı için doğrulanamayacak bir bağımlılık
 * eklemek yerine ihtiyacımız kadarını yazıyoruz: IMEI, sinyal, şebeke kaydı,
 * saat ve çevirme. Hepsi birkaç komut.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT_LINE_MAX 128
#define AT_RESP_MAX 256

/** Taşıyıcı — gerçek hedefte UART, testte senaryo. */
typedef struct {
    /** Satırı CR ile gönder. false = yazılamadı. */
    bool (*write_line)(void* ctx, const char* line);
    /** Bir satır oku (satır sonu olmadan). Zaman aşımında false. */
    bool (*read_line)(void* ctx, char* out, size_t cap, uint32_t timeout_ms);
    void* ctx;
} at_io_t;

typedef enum {
    AT_OK = 0,
    AT_ERR_ERROR,     /**< modem "ERROR" ya da "+CME ERROR" dedi */
    AT_ERR_TIMEOUT,   /**< sonuç kodu hiç gelmedi */
    AT_ERR_IO,        /**< yazılamadı */
    AT_ERR_ARG,
} at_result_t;

/**
 * @brief Komutu gönder, sonuç kodu gelene kadar oku.
 *
 * Bilgi satırları (`+CSQ: ...` gibi) `resp`e satır satır birikir; sonuç kodu
 * (`OK`/`ERROR`) resp'e YAZILMAZ — çağıran onu dönüş değerinden öğrenir.
 * Yankı satırı (modem komutu geri yazarsa) ve boş satırlar atılır.
 *
 * @param resp NULL olabilir (yanıtla ilgilenmiyorsak)
 * @param err_code +CME/+CMS ERROR sayısı; NULL olabilir
 */
at_result_t at_command(const at_io_t* io, const char* cmd, uint32_t timeout_ms,
                       char* resp, size_t resp_cap, int* err_code);

/* ── Yanıt ayrıştırıcıları ──────────────────────────────────────────────── */

/** `+CSQ: 17,99` → rssi indeksi 0-31, 99 = bilinmiyor (false döner). */
bool at_parse_csq(const char* resp, int* rssi_index);

/** CSQ indeksini dBm'e çevirir: 0 → -113, 31 → -51. */
int at_csq_to_dbm(int rssi_index);

/** IMEI — AT+CGSN çıplak sayı döndürür, bazı modemler `+CGSN: <imei>` der.
 *  İkisi de kabul; 14-15 hane rakam doğrulanır. */
bool at_parse_imei(const char* resp, char* out, size_t cap);

/** `+CREG: 0,1` → stat. Kayıtlı sayılanlar: 1 (yerel), 5 (dolaşımda). */
bool at_parse_creg(const char* resp, int* stat);
bool at_creg_registered(int stat);

/**
 * @brief `+CCLK: "26/08/14,13:22:33+12"` → epoch ms (UTC).
 *
 * Son alan çeyrek SAAT cinsinden saat dilimi ve YEREL saate göre verilir;
 * UTC'ye çevirmek için çıkarılması gerekir. Bu ayrıntı atlanırsa damga
 * saatlerce kayar — ve kayma dilime göre değiştiği için sahada ancak bazı
 * araçlarda görünür, teşhisi zor bir hata olur.
 */
bool at_parse_cclk(const char* resp, int64_t* utc_ms);

#ifdef __cplusplus
}
#endif
