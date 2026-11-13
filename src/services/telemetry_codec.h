#pragma once
/**
 * @brief Telemetri kaydı ve telemetri JSON kodlayıcısı.
 *
 * Bu modül SAF: donanım, ağ, FreeRTOS ya da ESP-IDF bilmiyor. Sebebi host'ta
 * baştan sona test edilebilmesi — gönderilen dokümanın doğruluğu bu projedeki
 * en zor geri alınabilir şey. Yanlış biçim sahadaki her araçtan yanlış veri
 * toplar ve fark edilmesi haftalar sürer.
 *
 * ── Tampona İKİLİ kayıt yazılıyor, JSON gönderim anında üretiliyor ─────────
 *
 * store&forward'a hazır JSON yazmak daha basit olurdu ama iki şeyi kaybederdi:
 *
 *  1. **Zaman damgası.** Kayıt yazıldığı anda saat bilinmiyor olabilir (RTC
 *     yok, SNTP ağ ister). Kapalı otoparkta kontak açılıp 20 dakika veri
 *     toplayan araç tipik hâl. JSON'u o an üretseydik damga ya yanlış olur ya
 *     da kayıt düşerdi. İkili kayıt monoton saati saklıyor, damga gönderim
 *     anında — saat öğrenildikten SONRA — geriye doğru hesaplanıyor.
 *  2. **Kapasite.** 64 frame'lik kayıt ikili 877 bayt (WiFi listesiyle en
 *     çok 933), JSON ~2,9 KB. Aynı 2,4 MB tampon ~10 saat yerine ~30 saat
 *     kapsama kaybı tutuyor.
 *
 * Bedeli kayıt biçiminin sürümlenmesi: OTA sonrası tamponda eski sürüm
 * kayıtlar kalabilir. `ver` alanı bunun için; tanınmayan sürüm düşürülür.
 *
 * ── Alan eşlemesi (sunucu sözleşmesi, tahmin DEĞİL) ────────────────────────
 *
 *
 *   imei, dateTime, latitude, longitude, speed, ignitionState, direction,
 *   gsmSignalQuality, satelliteCount, can[], tpms[], fuel
 *
 * `vehicleStatus`, `idleDuration`, `ignitionDuration` SUNUCUDA hesaplanıyor —
 * cihaz göndermiyor.
 *
 * Sentinel değerler sunucu kodundan okundu:
 *   - `tire: latestData.tpms || [-1,-1,-1,-1]`, `fuel || -1` → **-1 = bilinmiyor**
 *   - güzergâh sorguları `latitude: {$ne: 0}` filtreliyor → **0,0 = fix yok**
 * GNSS M5'te geldiği için şimdilik 0,0 gönderiliyor ve sunucu bu noktaları
 * güzergâhtan doğal olarak eliyor; kontak ve CAN verisi yine de işleniyor.
 *
 * `tpms` ayrı bir cihaz tipinden geliyor (DeviceType.TPMS), bizim
 * ürettiğimiz bir alan değil — hiç göndermiyoruz, sunucu varsayılanı koyuyor.
 *
 * `wifi` alanı (v2) telemetri dokümanında YOK; biçimi wearos-backend'in
 * geolocation.service.js'inin beklediğiyle birebir: [{"mac","rssi"}, ...].
 * positionSource/positionAccuracy ile birlikte sunucu mutabakatı bekliyor.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_REC_MAGIC 0x4D54u   /* "TM" */
/** Yazıcının sürümü. v2 = v1 + kuyrukta WiFi bölümü (aşağıda). Kodlayıcı
 *  v1'i de okur: OTA sonrası tamponda eski sürüm kayıt kalabiliyor. */
#define TELEMETRY_REC_VER     2
#define TELEMETRY_REC_VER_MIN 1

/* flags bitleri */
#define TELEMETRY_F_IGNITION 0x01   /**< kontak açık */
#define TELEMETRY_F_FIX      0x02   /**< konum geçerli (yoksa 0,0 gider) */
/** Konum ÖLÇÜLMEDİ, TAHMİN EDİLDİ (ölü hesap). Uydu kesildiğinde aracın
 *  kendi hız verisi ve jiroskopla yürütülen konum bu bayrakla işaretleniyor.
 *  Sunucunun ikisini ayırt edebilmesi şart; yoksa tahmini nokta ölçülmüş gibi
 *  kaydedilir ve güzergâhın doğruluğu sessizce bozulur. */
#define TELEMETRY_F_DR       0x04

/** Kayıt başlığı — 44 bayt, paketlenmiş, little-endian.
 *  Alan sırası değişirse TELEMETRY_REC_VER artırılmalı. */
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  ver;
    uint8_t  flags;
    uint32_t boot_id;          /**< yakalama oturumunun kimliği */
    int64_t  uptime_ms;        /**< yakalama anındaki monoton saat */
    int64_t  wall_ms;          /**< yakalama anında biliniyorsa UTC ms, yoksa 0 */
    int32_t  lat_e7;           /**< 1e-7 derece */
    int32_t  lon_e7;
    uint16_t speed_kmh_x10;
    uint16_t heading_deg_x10;
    int16_t  gsm_csq;          /**< -1 = bilinmiyor */
    int16_t  fuel_pct_x10;     /**< -1 = bilinmiyor */
    uint8_t  sats;
    uint8_t  frame_count;
    /** Konumun tahmini hata yarıçapı, metre. Uydu konumunda küçük bir sabit,
     *  ölü hesapta yol aldıkça büyüyor. Kayıt düzeni değişmesin diye eski
     *  `reserved` alanı kullanıldı; eski kayıtlarda 0 okunur ve bu "bilinmiyor"
     *  olarak yorumlanır. */
    uint16_t pos_uncertainty_m;
} telemetry_hdr_t;

/** Ham CAN frame'i — 13 bayt. Cihaz ÇÖZMEZ, sunucu DBC ile çözer. */
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} telemetry_frame_t;

/** Çevredeki WiFi ağı, 7 bayt (v2 kuyruğu). Konumu SUNUCU çözer
 *  (Ichnaea/beaconDB ya da Google Geolocation, wearos-backend'de hazır
 *  servis var); cihaz yalnız MAC + sinyal gücü iletir. Kuyruk düzeni:
 *  frame'lerden sonra 1 bayt sayaç + sayaç kadar bu yapı. */
typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    int8_t  rssi;              /**< dBm, negatif */
} telemetry_wifi_t;

#define TELEMETRY_HDR_SIZE   44
#define TELEMETRY_FRAME_SIZE 13
#define TELEMETRY_WIFI_SIZE  7
#define TELEMETRY_WIFI_MAX   8

/** Yakıt "bilinmiyor" değeri. Sunucunun sentinel'i düz -1 (`fuel || -1`), ama
 *  alan 10 ile ÖLÇEKLİ tutuluyor — ham -1 yazmak JSON'a -0.1 olarak çıkar ve
 *  sunucu bunu geçerli bir yakıt seviyesi sanar. Doğru değer -10 → "-1.0",
 *  ki JSON'da -1 ile aynı sayıdır. */
#define TELEMETRY_FUEL_UNKNOWN (-10)

/** Sinyal gücü tam sayı olarak gidiyor, ölçekli değil — -1 doğrudan yazılır. */
#define TELEMETRY_CSQ_UNKNOWN  (-1)

/** Sürüm 1 kayıt boyutu: WiFi kuyruğu yok. Geriye uyum denetimi için. */
static inline size_t telemetry_rec_size_v1(unsigned frame_count)
{
    return TELEMETRY_HDR_SIZE + (size_t)frame_count * TELEMETRY_FRAME_SIZE;
}

/** Sürüm 2 kayıt boyutu: frame'ler + 1 bayt WiFi sayacı + WiFi girdileri. */
static inline size_t telemetry_rec_size(unsigned frame_count,
                                        unsigned wifi_count)
{
    return telemetry_rec_size_v1(frame_count)
         + 1 + (size_t)wifi_count * TELEMETRY_WIFI_SIZE;
}

/* ── Kodlayıcı sonuç kodları ────────────────────────────────────────────── */
#define TELEMETRY_ERR_BAD_RECORD (-1)  /**< magic/ver/boyut tutmuyor → düşür */
#define TELEMETRY_ERR_NO_TIME    (-2)  /**< damga ŞU AN çözülemiyor → BEKLET */
#define TELEMETRY_ERR_STALE      (-3)  /**< damga ASLA çözülemez → düşür */
#define TELEMETRY_ERR_BUF_SMALL  (-4)

/**
 * @brief Kaydı telemetri dokümanı JSON'una çevirir.
 *
 * Damga çözümü (sırayla):
 *   1. kayıtta duvar saati varsa doğrudan kullanılır
 *   2. yoksa ve kayıt BU açılışa aitse: now_wall - (now_uptime - kayıt_uptime)
 *   3. yoksa ve önceki açılıştansa → TELEMETRY_ERR_STALE (bir daha asla
 *      çözülemez, beklemenin anlamı yok)
 *   4. bu açılışa ait ama saat hâlâ yoksa → TELEMETRY_ERR_NO_TIME (saat
 *      gelince çözülecek, kayıt tamponda kalmalı)
 *
 * @param now_wall_ms   şu anki UTC ms; 0 = saat hâlâ bilinmiyor
 * @param now_uptime_ms şu anki monoton ms
 * @param boot_id       şu anki açılışın kimliği
 * @return yazılan bayt (>0) ya da TELEMETRY_ERR_*
 */
int telemetry_encode_json(const void* rec, size_t rec_len, const char* imei,
                          int64_t now_wall_ms, int64_t now_uptime_ms,
                          uint32_t boot_id, char* out, size_t cap);

/** ISO 8601 UTC: "2026-08-13T09:41:07.123Z". cap en az 25 olmalı. */
void telemetry_format_iso8601(int64_t utc_ms, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
