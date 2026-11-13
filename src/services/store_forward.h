#pragma once
/**
 * @brief Kapsama dışı tamponu — raporları flash'a yaz, bağlantı gelince gönder.
 *
 * Araçta kapsama kaybı istisna değil, NORMAL. Tünel, kapalı otopark, kırsal
 * yol, hücre değişimi. Wear'da böyle bir şey yoktu: WiFi yoksa rapor sessizce
 * atlanıyordu, çünkü giyilebilir cihazın raporu kaçırması bir eksik ölçüm.
 * Araç takibinde kaçan rapor eksik bir GÜZERGÂH — sunucu tarafında "araç
 * kayboldu" olarak görünür ve hız/kontak/bölge alarmları yanlış çalışır.
 *
 * ── Yapı: ham flash üzerinde sabit slotlu HALKA ────────────────────────────
 *
 * "buffer" bölümü SF_SLOT_SIZE'lık slotlara bölünür; her slot bir kayıt tutar
 * ve tam bir flash sektörüdür. Slot = sektör olması bilinçli: bir kaydı silmek
 * komşusuna asla dokunmaz.
 *
 *   ofset  0  u32  magic   SF_MAGIC
 *          4  u32  seq     monoton sıra numarası — gönderim sırası bu
 *          8  u16  len     yük uzunluğu
 *         10  u8   ver
 *         11  u8   pad
 *         12  u32  crc32   yükün CRC'si
 *         16  u32  sent    0xFFFFFFFF = bekliyor, 0x00000000 = gönderildi
 *         20  ...  yük
 *
 * **Başlık YÜKTEN SONRA yazılır.** Commit atomikliği bundan geliyor: yazma
 * ortasında elektrik giderse magic hiç oluşmaz, slot boş sayılır. Yarım kayıt
 * gönderilmiş gibi davranamaz.
 *
 * **`sent` ayrı bir 4 baytlık kelime.** NOR flash'ta yazma yalnız 1'i 0 yapar;
 * 0xFFFFFFFF üstüne 0x00000000 yazmak silme gerektirmez. Böylece "gönderildi"
 * işareti slotu silmeden konabiliyor. Slot ancak yeniden kullanılırken siliniyor
 * — kayıt başına iki silme yerine bir silme, flash ömrünün iki katı.
 *
 * ── Halka dolduğunda EN ESKİ kayıt ezilir ──────────────────────────────────
 *
 * can_service'te tersini yapmıştık (yeni kimlik atılır, mevcutlar korunur).
 * Çelişki değil, karşılaştırılan şeyler farklı: orada yarışan kayıtlar farklı
 * SİNYALLERdi ve hepsi gerekliydi. Burada aynı sinyalin farklı ZAMANLARı
 * yarışıyor. Dispatcher haritaya bakarken aracın şu anki yerini görmek ister;
 * 14 saat önceki noktayı kaybetmek, güncel noktayı kaybetmekten iyidir. Yeni
 * yazmayı reddetmek cihazı canlı veri üretemez hâle getirirdi.
 *
 * ── peek/pop AYRI olmak zorunda ────────────────────────────────────────────
 *
 * Kayıt broker onaylamadan silinmez. Akış: peek -> QoS1 publish -> PUBACK ->
 * pop. Tek adımlık bir "al ve sil" olsaydı, publish'ten sonra bağlantı
 * kopunca kayıt hem gitmemiş hem silinmiş olurdu.
 *
 * Servis AĞDAN BAĞIMSIZ: ne MQTT'yi ne net_service'i bilir. Yalnız bayt
 * saklar. Bu yüzden host'ta baştan sona test edilebiliyor.
 */

/* SF_SLOT_SIZE / SF_MAX_SLOTS için — başlık kendi kendine yetsin. */
#include "../core/os_config.h"

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Slot başlığı — yük bu kadar bayt sonra başlar. */
#define SF_HEADER_SIZE 20

/** Bir kaydın taşıyabileceği azami yük. */
#define SF_PAYLOAD_MAX (SF_SLOT_SIZE - SF_HEADER_SIZE)

typedef struct {
    uint32_t capacity;     /**< toplam slot */
    uint32_t stored;       /**< gönderilmeyi bekleyen kayıt */
    uint32_t pushed;       /**< boot'tan beri yazılan */
    uint32_t sent;         /**< boot'tan beri gönderildi işaretlenen */
    uint32_t dropped_full; /**< halka dolu olduğu için ezilen EN ESKİ kayıt */
    uint32_t corrupt;      /**< boot taramasında atılan bozuk slot */
    uint32_t next_seq;     /**< bir sonraki kayda verilecek sıra numarası */
} sf_stats_t;

/** Bölümü tara, halka durumunu kur. sf_flash_init'i kendi çağırır. */
esp_err_t store_forward_init(void);

/** Kaydı tampona yaz.
 *  @return ESP_OK; len 0 ya da SF_PAYLOAD_MAX'ı aşarsa ESP_ERR_INVALID_SIZE. */
esp_err_t store_forward_push(const void* payload, size_t len);

/** Bekleyen en ESKİ kaydı oku — silmez.
 *  @param out_seq pop için gereken sıra numarası
 *  @return bekleyen kayıt yoksa ESP_ERR_NOT_FOUND */
esp_err_t store_forward_peek(void* out, size_t max, size_t* out_len,
                             uint32_t* out_seq);

/** Kaydı "gönderildi" işaretle. Broker onayından SONRA çağrılmalı.
 *  @return o sıra numarasıyla bekleyen kayıt yoksa ESP_ERR_NOT_FOUND */
esp_err_t store_forward_pop(uint32_t seq);

void store_forward_stats(sf_stats_t* out);

/** Bekleyen tüm kayıtları gönderilmiş say. Bölümü SİLMEZ — 624 sektör silmek
 *  ~20 sn bloklar ve gereksiz aşınma. Slotlar yeniden kullanılırken silinecek. */
esp_err_t store_forward_clear(void);

#ifdef __cplusplus
}
#endif
