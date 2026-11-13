/**
 * @file store_forward.cpp
 * @brief Flash üstünde sabit slotlu kayıt halkası (bkz. header).
 *
 * Tasarım notları:
 *
 *  - **Yazmadan önce HER ZAMAN silinir.** "Slot zaten boştu, silmeye gerek yok"
 *    kısayolu bir hata kaynağı: yükü yazılmış ama başlığı yazılamamış bir slot
 *    (elektrik kesintisi) başlığa bakıldığında boş görünür, ama yük alanında
 *    0xFF olmayan baytlar durur. Oraya doğrudan yazmak NOR flash'ta 0->1
 *    geçişi ister; sonuç sessizce bozuk kayıttır. Maliyeti sıfır sayılır:
 *    halka sürekli döndüğü için pratikte her yazma zaten kullanılmış bir slotu
 *    geri alıyor, gereksiz silme yalnız bakir cihazın ilk turunda oluyor.
 *
 *  - **Boot taraması yalnız BAŞLIKLARI okur** (624 x 20 bayt). Tüm bölümü
 *    okuyup CRC doğrulamak 2,4 MB okuma demekti; CRC zaten peek'te, yükü
 *    okurken bedavaya kontrol ediliyor. Bozuk çıkan kayıt orada gönderilmiş
 *    işaretlenip kuyruğu tıkamadan devre dışı kalıyor.
 *
 *  - **RAM'de slot dizini tutuluyor** (durum + sıra no, ~3 KB). Alternatifi her
 *    peek'te başlıkları yeniden taramaktı; 3 KB'ı vermek daha iyi bir takas,
 *    hem de gönderim yolundaki flash okumasını sıfıra indiriyor.
 *
 *  - CRC-32 burada elle yazıldı (ESP-IDF'in esp_rom_crc'si yerine): host testi
 *    ile hedefin bit bit aynı sonucu üretmesi gerekiyor.
 */

#include "store_forward.h"
#include "../core/os_config.h"
#include "../drivers/flash/sf_flash.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static const char* TAG = "sf";

/* "SFL1" — biçim değişirse artırılır, eski tamponu yanlış okumaktansa
 * tamamının atılması yeğdir. */
#define SF_MAGIC 0x314C4653u
#define SF_VER   1

#define SF_SENT_PENDING 0xFFFFFFFFu
#define SF_SENT_DONE    0x00000000u

/* Başlık alan ofsetleri — struct padding'ine güvenmiyoruz, host ve hedef
 * arasında bayt bayt aynı olmalı. */
#define OFF_MAGIC 0
#define OFF_SEQ   4
#define OFF_LEN   8
#define OFF_VER   10
#define OFF_PAD   11
#define OFF_CRC   12
#define OFF_SENT  16

enum { SLOT_EMPTY = 0, SLOT_PENDING = 1, SLOT_SENT = 2 };

static uint8_t          s_state[SF_MAX_SLOTS];
static uint32_t         s_seq[SF_MAX_SLOTS];
static uint32_t         s_slots      = 0;   /* gerçek slot sayısı */
static uint32_t         s_next_write = 0;
static uint32_t         s_next_seq   = 1;   /* 0 kullanılmıyor: "yok" demek */
static uint32_t         s_stored     = 0;
static uint32_t         s_pushed     = 0;
static uint32_t         s_sent       = 0;
static uint32_t         s_dropped    = 0;
static uint32_t         s_corrupt    = 0;
static bool             s_ready      = false;
static SemaphoreHandle_t s_mutex     = NULL;

/* ── CRC-32 (IEEE 802.3, yansımalı, poly 0xEDB88320) ────────────────────── */
static uint32_t crc32_of(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return ~c;
}

/* ── Küçük yardımcılar ──────────────────────────────────────────────────── */
static inline size_t slot_off(uint32_t i) { return (size_t)i * SF_SLOT_SIZE; }

static inline uint32_t rd_u32(const uint8_t* h, int off)
{
    uint32_t v;
    memcpy(&v, h + off, 4);
    return v;
}
static inline uint16_t rd_u16(const uint8_t* h, int off)
{
    uint16_t v;
    memcpy(&v, h + off, 2);
    return v;
}

/* Yükü hizalı kısım + kalan (<=3 bayt) olarak yazar. Kalan 0xFF ile
 * doldurulur; gerçek uzunluk başlıktaki `len` alanında. Ara tampon
 * kullanmıyoruz: 4 KB'lık bir yığın tamponu komut görevinin 4 KB'lık
 * yığınına sığmaz. */
static esp_err_t write_payload(size_t off, const uint8_t* p, size_t len)
{
    size_t whole = len & ~(size_t)3;
    if (whole) {
        esp_err_t e = sf_flash_write(off, p, whole);
        if (e != ESP_OK) return e;
    }
    size_t rem = len - whole;
    if (rem) {
        uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(tail, p + whole, rem);
        return sf_flash_write(off + whole, tail, 4);
    }
    return ESP_OK;
}

/* ── init: halka durumunu başlıklardan kur ──────────────────────────────── */
esp_err_t store_forward_init(void)
{
    if (s_ready) return ESP_OK;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_err_t err = sf_flash_init();
    if (err != ESP_OK) return err;

    size_t sz = sf_flash_size();
    s_slots = (uint32_t)(sz / SF_SLOT_SIZE);
    if (s_slots == 0) {
        ESP_LOGE(TAG, "bölüm bir slota bile yetmiyor (%u bayt)", (unsigned)sz);
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_slots > SF_MAX_SLOTS) {
        /* Bölüm büyütülmüş ama SF_MAX_SLOTS güncellenmemiş. Sessizce yarısını
         * kullanmak yerine bunu söylüyoruz; fazlası kullanılmıyor. */
        ESP_LOGW(TAG, "bölüm %lu slot alıyor, SF_MAX_SLOTS=%d — fazlası boş kalıyor",
                 (unsigned long)s_slots, SF_MAX_SLOTS);
        s_slots = SF_MAX_SLOTS;
    }

    memset(s_state, SLOT_EMPTY, sizeof(s_state));
    memset(s_seq, 0, sizeof(s_seq));
    s_stored = s_pushed = s_sent = s_dropped = s_corrupt = 0;
    s_next_seq = 1;

    uint32_t max_seq = 0;
    int32_t  max_i   = -1;

    for (uint32_t i = 0; i < s_slots; i++) {
        uint8_t h[SF_HEADER_SIZE];
        if (sf_flash_read(slot_off(i), h, sizeof(h)) != ESP_OK) continue;

        if (rd_u32(h, OFF_MAGIC) != SF_MAGIC) continue;  /* boş ya da çöp */

        uint16_t len = rd_u16(h, OFF_LEN);
        if (h[OFF_VER] != SF_VER || len == 0 || len > SF_PAYLOAD_MAX) {
            /* Magic tuttu ama başlık tutarsız — yarım yazılmış olabilir.
             * Slot kullanılabilir, yalnız içindekine güvenilmez. */
            s_corrupt++;
            continue;
        }

        uint32_t seq = rd_u32(h, OFF_SEQ);
        s_seq[i]   = seq;
        s_state[i] = (rd_u32(h, OFF_SENT) == SF_SENT_PENDING) ? SLOT_PENDING
                                                             : SLOT_SENT;
        if (s_state[i] == SLOT_PENDING) s_stored++;

        /* Halka her zaman artan seq ile sırayla yazılır; en büyük seq'in bir
         * sonrası yazılacak slottur. Sarma (wrap) bu yolla kendiliğinden
         * çözülüyor, ayrı bir "baş/kuyruk" kaydı tutmaya gerek yok. */
        if (max_i < 0 || seq > max_seq) { max_seq = seq; max_i = (int32_t)i; }
    }

    if (max_i >= 0) {
        s_next_write = ((uint32_t)max_i + 1) % s_slots;
        s_next_seq   = max_seq + 1;
    } else {
        s_next_write = 0;
        s_next_seq   = 1;
    }

    s_ready = true;
    ESP_LOGI(TAG, "init OK (%lu slot x %d bayt, bekleyen %lu, bozuk %lu, seq %lu)",
             (unsigned long)s_slots, SF_SLOT_SIZE, (unsigned long)s_stored,
             (unsigned long)s_corrupt, (unsigned long)s_next_seq);
    return ESP_OK;
}

/* ── push ───────────────────────────────────────────────────────────────── */
esp_err_t store_forward_push(const void* payload, size_t len)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!payload || len == 0 || len > SF_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint32_t i   = s_next_write;
    size_t   off = slot_off(i);

    if (s_state[i] == SLOT_PENDING) {
        /* Halka dolu: en eski kayıt eziliyor. Bkz. header — güncel konum,
         * saatler önceki konumdan değerli. */
        s_dropped++;
        if (s_stored) s_stored--;
        ESP_LOGW(TAG, "halka dolu, en eski kayıt (seq %lu) eziliyor",
                 (unsigned long)s_seq[i]);
    }

    /* Her zaman sil — bkz. dosya başı. */
    esp_err_t err = sf_flash_erase(off, SF_SLOT_SIZE);
    if (err != ESP_OK) {
        s_state[i] = SLOT_EMPTY;   /* içeriği artık bilinmiyor */
        s_seq[i]   = 0;
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "slot %lu silinemedi: %d", (unsigned long)i, (int)err);
        return err;
    }
    s_state[i] = SLOT_EMPTY;
    s_seq[i]   = 0;

    err = write_payload(off + SF_HEADER_SIZE, (const uint8_t*)payload, len);
    if (err != ESP_OK) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "yük yazılamadı: %d", (int)err);
        return err;
    }

    /* Başlık EN SON — commit noktası. Buraya kadar elektrik giderse magic
     * oluşmaz, slot boş sayılır ve yarım kayıt gönderilmez. */
    uint32_t seq = s_next_seq;
    uint8_t  h[SF_HEADER_SIZE];
    memset(h, 0xFF, sizeof(h));
    uint32_t magic = SF_MAGIC;
    uint32_t crc   = crc32_of((const uint8_t*)payload, len);
    uint16_t l16   = (uint16_t)len;
    memcpy(h + OFF_MAGIC, &magic, 4);
    memcpy(h + OFF_SEQ, &seq, 4);
    memcpy(h + OFF_LEN, &l16, 2);
    h[OFF_VER] = SF_VER;
    memcpy(h + OFF_CRC, &crc, 4);
    /* OFF_SENT 0xFFFFFFFF kalıyor = bekliyor. */

    err = sf_flash_write(off, h, sizeof(h));
    if (err != ESP_OK) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "başlık yazılamadı: %d", (int)err);
        return err;
    }

    s_state[i]   = SLOT_PENDING;
    s_seq[i]     = seq;
    s_stored++;
    s_pushed++;
    s_next_seq   = seq + 1;
    s_next_write = (i + 1) % s_slots;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/* ── peek ───────────────────────────────────────────────────────────────── */
/* Mutex çağıran tarafından alınmış olmalı. */
static int32_t oldest_pending_locked(void)
{
    int32_t best = -1;
    for (uint32_t i = 0; i < s_slots; i++) {
        if (s_state[i] != SLOT_PENDING) continue;
        if (best < 0 || s_seq[i] < s_seq[best]) best = (int32_t)i;
    }
    return best;
}

/* Slotu gönderilmiş işaretle. Mutex alınmış olmalı. */
static esp_err_t mark_sent_locked(uint32_t i)
{
    uint32_t done = SF_SENT_DONE;
    esp_err_t err = sf_flash_write(slot_off(i) + OFF_SENT, &done, 4);
    if (err != ESP_OK) {
        /* Flash yazamadıysak durumu RAM'de de değiştirmiyoruz: yeniden
         * denenebilir. Yoksa reboot sonrası kayıt tekrar gönderilirdi. */
        ESP_LOGE(TAG, "sent işareti yazılamadı (slot %lu): %d",
                 (unsigned long)i, (int)err);
        return err;
    }
    s_state[i] = SLOT_SENT;
    if (s_stored) s_stored--;
    s_sent++;
    return ESP_OK;
}

esp_err_t store_forward_peek(void* out, size_t max, size_t* out_len,
                             uint32_t* out_seq)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!out || max == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Bozuk kayıt kuyruğu tıkamamalı: bulunca gönderilmiş işaretleyip bir
     * sonrakine geçiyoruz. Döngü slot sayısıyla sınırlı. */
    for (uint32_t guard = 0; guard < s_slots; guard++) {
        int32_t i = oldest_pending_locked();
        if (i < 0) {
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NOT_FOUND;
        }

        uint8_t h[SF_HEADER_SIZE];
        esp_err_t err = sf_flash_read(slot_off((uint32_t)i), h, sizeof(h));
        if (err != ESP_OK) { xSemaphoreGive(s_mutex); return err; }

        uint16_t len = rd_u16(h, OFF_LEN);
        if (rd_u32(h, OFF_MAGIC) != SF_MAGIC || len == 0 ||
            len > SF_PAYLOAD_MAX) {
            s_corrupt++;
            mark_sent_locked((uint32_t)i);
            continue;
        }
        if (len > max) {
            /* Çağıranın tamponu küçük — bu bir programlama hatası, kaydı
             * atmıyoruz. */
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_SIZE;
        }

        err = sf_flash_read(slot_off((uint32_t)i) + SF_HEADER_SIZE, out, len);
        if (err != ESP_OK) { xSemaphoreGive(s_mutex); return err; }

        if (crc32_of((const uint8_t*)out, len) != rd_u32(h, OFF_CRC)) {
            /* Yük bozulmuş. Göndermenin anlamı yok, sunucu çözemez. */
            ESP_LOGW(TAG, "slot %ld crc tutmadı, atlanıyor", (long)i);
            s_corrupt++;
            mark_sent_locked((uint32_t)i);
            continue;
        }

        if (out_len) *out_len = len;
        if (out_seq) *out_seq = rd_u32(h, OFF_SEQ);
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_mutex);
    return ESP_ERR_NOT_FOUND;
}

/* ── pop ────────────────────────────────────────────────────────────────── */
esp_err_t store_forward_pop(uint32_t seq)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < s_slots; i++) {
        if (s_state[i] == SLOT_PENDING && s_seq[i] == seq) {
            err = mark_sent_locked(i);
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return err;
}

/* ── stats / clear ──────────────────────────────────────────────────────── */
void store_forward_stats(sf_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_ready) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->capacity     = s_slots;
    out->stored       = s_stored;
    out->pushed       = s_pushed;
    out->sent         = s_sent;
    out->dropped_full = s_dropped;
    out->corrupt      = s_corrupt;
    out->next_seq     = s_next_seq;
    xSemaphoreGive(s_mutex);
}

esp_err_t store_forward_clear(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    for (uint32_t i = 0; i < s_slots; i++) {
        if (s_state[i] != SLOT_PENDING) continue;
        esp_err_t e = mark_sent_locked(i);
        if (e != ESP_OK) err = e;
    }
    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "bekleyen kayıtlar gönderilmiş sayıldı");
    return err;
}
