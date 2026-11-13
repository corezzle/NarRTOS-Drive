#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// NarRTOS-Drive — merkezî yapılandırma
//
// Wear'daki os_config.h ile aynı disiplin: pin haritası, görev dağılımı ve
// dış servis adresleri tek yerde. Her tanım #ifndef korumalı; platformio.ini
// build flag'iyle ezilebilir.
// ═══════════════════════════════════════════════════════════════════════════

// ─── Sürüm ───────────────────────────────────────────────────────────────────
// Wear'dan BAĞIMSIZ numaralandırma. Wear 1.4.x'te; Drive sıfırdan başlıyor.
// OTA manifest'inde ayrı kanal kullanılacak, yoksa Wear cihazına Drive
// firmware'i inebilir.
#define OS_VERSION_MAJOR 0
#define OS_VERSION_MINOR 1
#define OS_VERSION_PATCH 0
#define OS_VERSION_STR   "0.1.0"

// Manifest karşılaştırması için tek tam sayı — string compare tuzağına
// düşmemek için (Wear'da da aynı kalıp: "0.10" < "0.2" hatası).
#define OS_VERSION_CODE  ((OS_VERSION_MAJOR) * 10000 + \
                          (OS_VERSION_MINOR) *   100 + \
                          (OS_VERSION_PATCH))

#ifndef NARRTOS_PRODUCT
#define NARRTOS_PRODUCT "drive"
#endif

// ─── Özellik bayrakları ──────────────────────────────────────────────────────
// platformio.ini'den geliyor; burada yalnız varsayılanları koruyoruz ki tek
// dosya derlemeleri ve host testleri de çalışsın.
#ifndef ENABLE_CAN
#define ENABLE_CAN 0
#endif
#ifndef ENABLE_GNSS
#define ENABLE_GNSS 0
#endif
#ifndef ENABLE_WIFI
#define ENABLE_WIFI 0
#endif
#ifndef ENABLE_MODEM
#define ENABLE_MODEM 0
#endif
#ifndef ENABLE_IMU
#define ENABLE_IMU 0
#endif
#ifndef ENABLE_MQTT
#define ENABLE_MQTT 0
#endif

// ─── Pin haritası ────────────────────────────────────────────────────────────
//
// ⚠ HEPSİ GEÇİCİ. Elde CAN transceiver'ı ve modem yok; bu değerler
// ESP32-S3 devkit üzerinde çakışmayan, GPIO matrisiyle her yere yönlenebilen
// pinlerden seçildi. Gerçek kart gelince tek yerden düzeltilecek.
//
// Kaçınılanlar: GPIO 0 (boot), 19/20 (USB D-/D+), 26-32 (flash/PSRAM),
// 33-37 (octal PSRAM'li kartlarda ayrılmış), 46 (strapping, giriş-only).

// TWAI (CAN 2.0B) — dışarıdan transceiver şart (SN65HVD230 / TJA1050).
// ESP32-S3'te dahili denetleyici var ama sürücü katmanı yok; çıplak bağlarsan
// hattı da yakarsın, cihazı da.
#ifndef PIN_CAN_TX
#define PIN_CAN_TX      4
#endif
#ifndef PIN_CAN_RX
#define PIN_CAN_RX      5
#endif
#ifndef PIN_CAN_STBY            // transceiver standby; yoksa -1 bırak
#define PIN_CAN_STBY   -1
#endif

// Hücresel modem (SIM7600 / EC200 sınıfı) — UART + güç kontrolü.
// IMEI buradan okunacak: sunucu cihazları IMEI ile anahtarlıyor.
#ifndef PIN_MODEM_TX
#define PIN_MODEM_TX   17
#endif
#ifndef PIN_MODEM_RX
#define PIN_MODEM_RX   18
#endif
#ifndef PIN_MODEM_PWRKEY
#define PIN_MODEM_PWRKEY 15
#endif

// GNSS — UART NMEA. Araç takibinde WiFi BSSID konumu yetmez (Wear'daki
// yöntem ±19 m ama sabit cihaz varsayıyor; hareket eden araçta kullanılamaz).
#ifndef PIN_GNSS_RX
#define PIN_GNSS_RX     8
#endif
#ifndef PIN_GNSS_TX
#define PIN_GNSS_TX     9
#endif

// Kontak (ignition) — sunucu tarafında hazır alarm/rapor motoru var, cihaz
// bunu güvenilir üretmeli. Dijital giriş; araca göre CAN'den de çıkarılabilir.
#ifndef PIN_IGNITION
#define PIN_IGNITION   10
#endif

// Durum LED'i — ekransız cihazın tek yerel geri bildirimi. -1 = LED yok,
// servis hiç başlamaz.
//
// ⚠ 48 bazı ESP32-S3 devkit'lerinde adreslenebilir RGB LED'e (WS2812) bağlı.
// Onlar düz GPIO ile sürülemez, kendi protokolünü ister. Gerçek kart gelince
// ya düz bir LED'e alınacak ya da sürücü WS2812'ye çevrilecek; desen mantığı
// (status_led.cpp) ikisinde de aynı kalır.
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED 48
#endif

// LED'in yanma yönü. Ortak anot bağlantıda 0 yapılır.
#ifndef STATUS_LED_ACTIVE_HIGH
#define STATUS_LED_ACTIVE_HIGH 1
#endif

// ─── Telemetri ───────────────────────────────────────────────────────────────
// Rapor periyodu. Wear'da 60 sn'ydi ve WiFi taraması yüzünden 5 turda bir
// atlanıyordu; burada tarama yok, her tur rapor gider.
#ifndef TELEMETRY_PERIOD_MS
#define TELEMETRY_PERIOD_MS (60 * 1000)
#endif

// Kontak kapalıyken periyot uzar — araç park hâlindeyken veri harcamamak için.
#ifndef TELEMETRY_PERIOD_PARKED_MS
#define TELEMETRY_PERIOD_PARKED_MS (10 * 60 * 1000)
#endif

// Bir raporda taşınacak azami ham CAN frame sayısı. Frame'ler
// "0xID,b0,..,b7" dizesi olarak gider; çözümü SUNUCU yapar (J1939 DBC ile).
// Cihazda DBC yok — 5,7 MB'lık dosya zaten sığmaz.
#ifndef CAN_FRAMES_PER_REPORT
#define CAN_FRAMES_PER_REPORT 64
#endif

// CAN kimliği tablosunun boyutu. Kimlik BAŞINA en son frame saklanır —
// FIFO DEĞİL. Gerekçe: J1939'da PGN'ler 10 ms–1 sn aralıklarla tekrar eder,
// 250 kbit/s hat saniyede ~2000 frame taşır. FIFO saniyeler içinde taşar ve
// rapora rastgele bir dilim gider. Sunucunun istediği her sinyalin GÜNCEL
// değeri; bir araç tipik olarak 30-80 farklı kimlik yayınlar.
#ifndef CAN_ID_TABLE_SIZE
#define CAN_ID_TABLE_SIZE 96
#endif

// Varsayılan hat hızı. J1939 250 kbit/s'tir; bazı araçlar 500 kullanır.
// `set can bitrate` ile NVS'e yazılır; bu yalnızca fabrika varsayılanı.
#ifndef CAN_BITRATE_DEFAULT_KBPS
#define CAN_BITRATE_DEFAULT_KBPS 250
#endif

// ─── Store & forward ─────────────────────────────────────────────────────────
// Kapsama dışında raporlar flash'a yazılır, bağlantı gelince gönderilir.
// Wear'da böyle bir şey yoktu (WiFi yoksa rapor atlanıyordu); araçta kapsama
// kaybı istisna değil, normal.

// partitions_drive.csv'deki bölüm etiketi. Tek bağlayıcı nokta.
#ifndef SF_PARTITION_LABEL
#define SF_PARTITION_LABEL "buffer"
#endif

// Bir kayıt = bir slot = bir flash SEKTÖRÜ. Sektör hizası bilinçli: bir kaydı
// silmek komşusuna asla dokunmaz, dolayısıyla silme sırasında elektrik gitse
// bile yalnız o kayıt kaybolur.
#ifndef SF_SLOT_SIZE
#define SF_SLOT_SIZE 4096
#endif

// RAM'deki slot dizininin üst sınırı. `buffer` 0x270000 bayt = 624 slot;
// 640 sınırı bölüm biraz büyütülürse kod değişmeden yetsin diye. Maliyeti
// 640 bayt durum + 2560 bayt sıra numarası ≈ 3,1 KB statik RAM.
// Bölüm bundan büyükse fazlası kullanılmaz ve boot'ta uyarı basılır.
#ifndef SF_MAX_SLOTS
#define SF_MAX_SLOTS 640
#endif

// Bir telemetri kaydının taşıyabileceği azami CAN frame sayısı ile birlikte
// kayıt boyutu: 44 bayt başlık + 13 bayt/frame. 64 frame → 876 bayt, tek
// store&forward slotuna (4076 bayt yük) rahat sığıyor.

// ─── Zaman ───────────────────────────────────────────────────────────────────
// Cihazda RTC YOK. Saat kaynakları, gelme sırasıyla: SNTP (ağ üzerinden,
// bugün), GNSS (M5), modem şebeke saati (M7).
//
// Bu neden kritik: telemetri dokümanının `dateTime` alanı zorunlu ve sunucu
// tarih aralığı sorgularını bununla yapıyor.
// Yanlış damga güzergâhı, hız raporunu ve kontak raporunu birden bozar.
#ifndef NTP_SERVER_DEFAULT
#define NTP_SERVER_DEFAULT "pool.ntp.org"
#endif

// Saatin "geçerli" sayılması için asgari eşik. Sistem saati senkron öncesi
// 1970'ten başlar; 2020'den küçük her değer senkronsuz demek.
#ifndef TIME_VALID_MIN_EPOCH_S
#define TIME_VALID_MIN_EPOCH_S 1577836800L   /* 2020-01-01T00:00:00Z */
#endif

// ─── GNSS ────────────────────────────────────────────────────────────────────
// NMEA alıcıları neredeyse hepsi 9600'de başlar; bazıları 38400/115200.
// `set gnss baud` ile NVS'e yazılır, bu yalnız fabrika varsayılanı.
#ifndef GNSS_BAUD_DEFAULT
#define GNSS_BAUD_DEFAULT 9600
#endif
#ifndef GNSS_UART_NUM
#define GNSS_UART_NUM 1        /* UART0 konsol/komut hattı, dokunulmuyor */
#endif

// Sabitleme bundan eskiyse GEÇERSİZ sayılır ve rapora 0,0 gider.
// Bayat konum, konum yokluğundan kötüdür: hareket eden araçta eski bir nokta
// "şu anki konum" diye gönderilir ve sunucunun bunu ayıklama şansı yoktur
// (`latitude: {$ne: 0}` filtresi onu kabul eder). 10 sn, 1 Hz yayın yapan
// bir alıcıda birkaç kaçan cümleyi tolere eder ama tünelde hızla düşer.
#ifndef GNSS_FIX_MAX_AGE_MS
#define GNSS_FIX_MAX_AGE_MS (10 * 1000)
#endif

// Saat zaten geçerliyken GNSS'ten yeniden kurma eşiği. Küçük farklarda
// saati sürekli oynatmak damgaları zıplatır; büyük fark gerçek bir sapmadır.
#ifndef GNSS_TIME_RESYNC_MS
#define GNSS_TIME_RESYNC_MS (60 * 1000)
#endif

// ─── Ağ ──────────────────────────────────────────────────────────────────────
// Bağlantı yeniden deneme aralığı ÜSTEL artar. Sabit aralık araçta yanlış:
// tünelde ya da kırsalda saatlerce kapsama olmayabilir, saniyede bir modem
// kaydı denemek boşa akım ve boşa hücre trafiği.
#ifndef NET_RETRY_MIN_MS
#define NET_RETRY_MIN_MS (5 * 1000)
#endif
#ifndef NET_RETRY_MAX_MS
#define NET_RETRY_MAX_MS (5 * 60 * 1000)
#endif

// Tek bir bağlanma denemesinin süre sınırı.
#ifndef NET_CONNECT_TIMEOUT_MS
#define NET_CONNECT_TIMEOUT_MS (20 * 1000)
#endif

// ─── MQTT ────────────────────────────────────────────────────────────────────
// ⚠ Sözleşme HENÜZ BİLİNMİYOR — konu şeması ve yük biçimi mqtt_contract.h'de,
// tek dosyada toplandı. Sunucu ingestion servisi bu makinede yok; broker adresi,
// konu şeması ve kimlik doğrulama öğrenilince yalnız o dosya değişecek.
#ifndef MQTT_PORT_DEFAULT
#define MQTT_PORT_DEFAULT 1883
#endif

// Broker'a bağlanınca tamponu boşaltırken aynı anda kaç kayıt havada olsun.
// 1: gönderim sırası korunur ve pop mantığı tek kayıtlık kalır. Güzergâh
// kronolojik olmak zorunda; 800 kayıtlık birikmiş tamponu boşaltmak tek
// kayıtla ~40 sn sürer, bu kabul edilebilir.
#ifndef MQTT_INFLIGHT_MAX
#define MQTT_INFLIGHT_MAX 1
#endif

// PUBACK beklenirken sabır sınırı. Aşılırsa kayıt gönderilmiş SAYILMAZ,
// yeniden denenir — kaybetmek yerine tekrarlamak yeğdir.
#ifndef MQTT_ACK_TIMEOUT_MS
#define MQTT_ACK_TIMEOUT_MS (15 * 1000)
#endif

// ─── Jiroskop (ICM-42688-P) ──────────────────────────────────────────────────
// Ölü hesabın yön kaynağı: uydu kesildiğinde aracın ne kadar döndüğünü bu
// sensör söylüyor. Mesafe bilgisi CAN'den geliyor, ivmeölçerden DEĞİL
// (gerekçe dead_reckoning.h'de).
//
// ⚠ Sensör elde YOK. Pinler boştaki GPIO'lardan seçildi, gerçek kart gelince
// düzeltilecek.
#ifndef PIN_IMU_SDA
#define PIN_IMU_SDA 11
#endif
#ifndef PIN_IMU_SCL
#define PIN_IMU_SCL 12
#endif
#ifndef IMU_I2C_NUM
#define IMU_I2C_NUM 0
#endif
// AP_AD0 bacağı düşükken 0x68, yüksekken 0x69.
#ifndef IMU_I2C_ADDR
#define IMU_I2C_ADDR 0x68
#endif
// Sensör karta ters monte edilirse dönüş yönü çevrilir.
#ifndef IMU_YAW_INVERT
#define IMU_YAW_INVERT 0
#endif

// ─── WiFi tarama (kapalı alan konumlama, M9) ─────────────────────────────────
// Konum uydu değilken çevredeki AP'lerin MAC+RSSI listesi rapora ekleniyor;
// konumu SUNUCU çözer (cihaz çözmez, CAN'deki kalıbın aynısı). Uydu varken
// taranmıyor: katma değeri yok, radyo ve güç israfı.
#ifndef WIFI_SCAN_MAX_APS
#define WIFI_SCAN_MAX_APS 8            /* rapora giren en güçlü N ağ */
#endif
#ifndef WIFI_SCAN_INTERVAL_MS
#define WIFI_SCAN_INTERVAL_MS 30000    /* iki tarama arası asgari süre */
#endif
#ifndef WIFI_SCAN_MAX_AGE_MS
#define WIFI_SCAN_MAX_AGE_MS 120000    /* bundan eski liste rapora girmez */
#endif
#ifndef WIFI_SCAN_POLL_MS
#define WIFI_SCAN_POLL_MS 5000         /* politika denetim tiki */
#endif

// ─── Modem (4G) ──────────────────────────────────────────────────────────────
#ifndef MODEM_UART_NUM
#define MODEM_UART_NUM 2       /* UART0 konsol, UART1 GNSS */
#endif
#ifndef MODEM_BAUD_DEFAULT
#define MODEM_BAUD_DEFAULT 115200
#endif

// PWRKEY darbe süresi. SIM7600 ~500 ms, EC200 ~1 sn ister; 1,2 sn ikisini de
// kapsıyor. Kısa kalırsa modem hiç açılmaz ve sebebi görünmez.
#ifndef MODEM_PWRKEY_PULSE_MS
#define MODEM_PWRKEY_PULSE_MS 1200
#endif

// Modem açılıp AT'ye cevap verene kadar geçen süre — soğuk açılışta uzun.
#ifndef MODEM_BOOT_TIMEOUT_MS
#define MODEM_BOOT_TIMEOUT_MS (20 * 1000)
#endif

// Şebekeye kayıt bekleme. Kapalı otoparkta hiç olmayabilir; net_service'in
// üstel beklemesi devreye girsin diye burada sonsuz beklemiyoruz.
#ifndef MODEM_REG_TIMEOUT_MS
#define MODEM_REG_TIMEOUT_MS (60 * 1000)
#endif

#ifndef MODEM_AT_TIMEOUT_MS
#define MODEM_AT_TIMEOUT_MS 3000
#endif

// ─── OTA ─────────────────────────────────────────────────────────────────────
// ⚠ Uç nokta Wear'dan AYRI olmalı: aynı manifest'i paylaşsalar Wear cihazına
// Drive firmware'i iner (Session 1 kararı). Sunucu tarafı henüz yok; adres
// NVS'ten ayarlanabiliyor ki firmware değiştirmeden bağlanabilelim.
#ifndef OTA_MANIFEST_URL_DEFAULT
#define OTA_MANIFEST_URL_DEFAULT ""    /* boş = OTA kapalı */
#endif

// Manifest kontrol aralığı. Araç takip cihazı için sık kontrol gereksiz;
// hücresel veri de bedava değil.
#ifndef OTA_CHECK_PERIOD_MS
#define OTA_CHECK_PERIOD_MS (6 * 60 * 60 * 1000)
#endif

// Geri alma doğrulaması: yeni firmware bu kadar süre çökmeden çalışırsa
// "geçerli" işaretlenir (yayın onayı daha erken gelirse o kullanılır).
// 5 dakika, açılış döngülerini ve erken çökmeleri yakalamaya yeter.
#ifndef OTA_VALIDATE_UPTIME_MS
#define OTA_VALIDATE_UPTIME_MS (5 * 60 * 1000)
#endif

#ifndef OTA_HTTP_TIMEOUT_MS
#define OTA_HTTP_TIMEOUT_MS (30 * 1000)
#endif

// ─── Güç yönetimi (M10) ──────────────────────────────────────────────────────
// Kontak kapanınca bekleyen işler bitirilip derin uykuya geçiliyor; uyanma
// kontak sinyaliyle (anında) ya da zamanlayıcıyla (park raporu için).
// Varsayılan KAPALI: geliştirme sırasında cihazın masada uyuyakalması USB
// konsolunu öldürür. Sahada `set power sleep 1` ile açılır.
#ifndef POWER_SLEEP_DELAY_MS
#define POWER_SLEEP_DELAY_MS (5u * 60u * 1000u)   /* kontak kapandıktan uykuya */
#endif
#ifndef POWER_WAKE_INTERVAL_MS
#define POWER_WAKE_INTERVAL_MS TELEMETRY_PERIOD_PARKED_MS /* park raporuyla hizalı */
#endif
#ifndef POWER_TIMER_AWAKE_MS
#define POWER_TIMER_AWAKE_MS (2u * 60u * 1000u)   /* zamanlayıcı uyanışında pencere */
#endif
#ifndef POWER_MAX_AWAKE_MS
#define POWER_MAX_AWAKE_MS (15u * 60u * 1000u)    /* tampon boşalmasa da tavan */
#endif
#ifndef POWER_POLL_MS
#define POWER_POLL_MS 5000
#endif

// ─── Görev dağılımı ──────────────────────────────────────────────────────────
// platformio.ini'den geliyor; burada yalnız varsayılanları koruyoruz.
#ifndef CAN_TASK_CORE
#define CAN_TASK_CORE 1
#endif
#ifndef CAN_TASK_PRIORITY
#define CAN_TASK_PRIORITY 6
#endif
#ifndef NET_TASK_CORE
#define NET_TASK_CORE 0
#endif
#ifndef NET_TASK_PRIORITY
#define NET_TASK_PRIORITY 4
#endif
#ifndef TELEMETRY_TASK_CORE
#define TELEMETRY_TASK_CORE 0
#endif
#ifndef TELEMETRY_TASK_PRIORITY
#define TELEMETRY_TASK_PRIORITY 3
#endif

// ─── NVS anahtarları ─────────────────────────────────────────────────────────
// USB komut arayüzünden yazılan kalıcı ayarlar. Modem gelene kadar IMEI de
// elle giriliyor (`set imei ...`) — sunucu IMEI ile anahtarladığı için zorunlu.
#define NVS_NAMESPACE     "drive"
#define NVS_KEY_IMEI      "imei"
#define NVS_KEY_MQTT_HOST "mqtt_host"
#define NVS_KEY_MQTT_PORT "mqtt_port"
#define NVS_KEY_POWER_SLEEP "pwr_sleep"  /* 1 = kontak kapanınca uyu */
// Broker kimlik doğrulaması BİLİNMİYOR. Anahtarlar şimdiden var: değer boşsa
// anonim bağlanılıyor, sözleşme öğrenilince yalnız `set mqtt user/pass`
// yazılacak — firmware değişmeyecek.
#define NVS_KEY_MQTT_USER "mqtt_user"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_APN       "apn"
#define NVS_KEY_CAN_KBPS  "can_kbps"
#define NVS_KEY_GNSS_BAUD "gnss_baud"

// WiFi arka ucu — geçici/atölye bağlantısı. Sahada 4G modem kullanılacak
// (sunucu IMEI ile anahtarlıyor, IMEI modemden gelir), ama modem donanımı
// gelene kadar ağ yolunu sürebilmenin tek yolu WiFi.
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

// Hangi ağ arka ucu kullanılacak: "wifi" | "modem" | "none".
// Derleme bayrağıyla değil NVS ile seçiliyor: aynı firmware hem atölyede
// (WiFi) hem araçta (modem) çalışsın, ikinci bir yapı üretmeye gerek kalmasın.
#define NVS_KEY_NET_BACKEND "net_be"

// NTP sunucusu. Varsayılan public havuz; kapalı bir şebekede (APN'in dışarı
// çıkmadığı kurumsal SIM) havuz erişilemez olur ve cihaz saatsiz kalır —
// saatsiz kalınca da rapor gönderilemez. Bu yüzden sahada değiştirilebilir.
#define NVS_KEY_NTP       "ntp"
#define NVS_KEY_OTA_URL   "ota_url"
