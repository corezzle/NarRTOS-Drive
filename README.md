# NarRTOS-Drive

> 📖 **Başlamadan önce:** kurulum, derleme, karta yükleme, yapılandırma, rapor içeriği, konum, OTA ve sorun giderme adımlarının tamamı
> **[NarRTOS Drive Wiki](https://www.corezzle.com/document-content/narrtos-drive-9)** sayfasında adım adım anlatılıyor.
> Projeyi ilk kez kullanacaksanız oradan ilerleyin; bu README yalnız kısa bir özettir.
>
> 📖 **Before you start:** installation, build, flashing, configuration and troubleshooting are covered step by step in the
> **[NarRTOS Drive Wiki](https://www.corezzle.com/document-content/narrtos-drive-9)** (Turkish, with English pages under `docs/panel-content/en/`).


Ekransız araç takip cihazı için gömülü işletim sistemi (ESP32-S3).

NarRTOS ürün ailesinin ikinci ürünü. Birincisi
[NarRTOS-Wear](https://github.com/corezzle/NarRTOS-Wear)
(NarRTOS-Wear): dokunmatik ekranlı giyilebilir cihaz. Drive aynı çekirdek
disiplinini paylaşır ama farklı bir üründür: ekran, LVGL ve uygulama katmanı
yoktur; yerine CAN, GNSS ve hücresel bağlantı vardır.

## Belgeler

Wiki'nin kaynak dosyaları depoda: `docs/panel-content/` (TR), `docs/panel-content/en/` (EN),
tek dosya sürüm `docs/NarRTOS-Drive-Wiki.html` ve `.pdf`.

## Ne yapar

Araca takılır, periyodik olarak konum ve araç verisi raporlar:

- **CAN (TWAI)**: araç veri yolundan ham frame yakalar
- **GNSS**: UART NMEA ile konum
- **Kapalı alanda konum**: uydu kesilince ölü hesap (CAN hızı + jiroskop);
  çevredeki WiFi ağ listesi rapora eklenir, konumu sunucu çözer
- **Hücresel**: 4G modem üzerinden MQTT
- **USB konfigürasyon**: seri porttan metin komutlarıyla saha ayarı
- **Store & forward**: kapsama dışında raporları saklar, bağlantı gelince gönderir
- **Güç yönetimi**: kontak kapanınca tamponu boşaltıp derin uyku;
  kontakla ya da park raporu zamanlayıcısıyla uyanır (varsayılan kapalı)
- **OTA**: A/B partisyon ile kablosuz güncelleme

### CAN'i cihaz çözmez

Ham frame'ler `"0xID,b0,b1,...,b7"` biçiminde sunucuya iletilir; çözümü filo
backend'i yapar (J1939 DBC ile). Cihazda DBC yoktur: 5,7 MB'lık dosya zaten
sığmaz ve sinyal tanımları sunucuda güncellenebilir olmalıdır.

## Derleme

```bash
pio run -e esp32s3_drive
pio run -e esp32s3_drive -t upload
pio device monitor
```

## Yapılandırma

Cihazda ekran ve buton yok; tüm ayar seri porttan yapılır. `help` yazarak
komut listesini görebilirsiniz.

```
set imei 351234567890123        cihaz kimliği (sunucu IMEI ile anahtarlıyor)
set mqtt host broker.ornek.com
set mqtt port 1883
set apn internet
set power sleep 1                sahada önerilir: park hâlinde derin uyku
set wifi ssid Atolye Agi         atölye bağlantısı (modem gelene kadar)
set wifi pass ...
get config
```

MQTT bağlıyken aynı komutlar `<önek>/<imei>/cmd` konusundan da verilebilir;
yanıtlar `<önek>/<imei>/cmd/reply` konusuna döner.

### Donanımsız sürüş

Elde CAN transceiver'ı, modem ve GNSS yok. Boru hattı uçtan uca komutlarla
sürülüyor:

```
can inject 0x18FEE500,01,02,...  ham frame enjekte et
gnss inject $GPRMC,...*HH        NMEA cümlesi enjekte et (konum + saat)
wifi inject AA:BB:CC:DD:EE:FF -60  WiFi listesine ağ ekle (kapalı alan konumu)
pos status                       konum kaynağı: uydu mu tahmin mi
telemetry ignition 1             kontağı aç, anında rapor üretilir
telemetry now                    hemen bir rapor üret
sf peek                          kaydı ÇÖZÜLMÜŞ göster (giden JSON'un aynısı)
net offline 1                    kapsama kaybı taklidi
sf stats                         tampon doluluğu ve kalan süre
net offline 0                    kapsama geri geldi, tampon boşalır
```

## Telemetri

Rapor tampona **ikili kayıt** olarak yazılır, telemetri JSON'u gönderim anında
üretilir. Sebebi zaman damgası: cihazda RTC yok ve kayıt yazılırken saat
bilinmiyor olabilir (kapalı otoparkta çalışan araç). İkili kayıt monoton saati
saklar, damga saat öğrenildikten sonra geriye doğru hesaplanır. Yan kazanç:
aynı tampon ~10 saat yerine ~30 saat kapsama kaybı tutar.

Doküman alanları `src/services/telemetry_codec.h`'de belgeli ve filo
backend'inden okundu; `vehicleStatus`, `idleDuration` ve `ignitionDuration`
sunucuda hesaplanır, cihaz göndermez.

**Saat yoksa rapor gönderilmez, tamponda bekler.** Yanlış damga güzergâhı,
hız raporunu ve kontak raporunu aynı anda bozar; beklemek daha ucuz.
`time status` ile bakılır.

## Konum ve saat

GNSS (NMEA 0183) `latitude`, `longitude`, `speed`, `direction` ve
`satelliteCount` alanlarını dolduruyor. Aynı zamanda **ikinci saat kaynağı**:
şebeke olmayan bir araç GNSS sabitlemesiyle de doğru damga üretebiliyor.

İki kural:

**Sağlama toplamı tutmayan cümle tamamen atılır.** UART'ta tek bozuk bayt
koordinatı kilometrelerce kaydırabilir ve sunucunun bunu ayıklama şansı yok.

**Bayat sabitleme, sabitleme yokluğundan kötüdür.** 10 saniyeden eski konum
geçersiz sayılıp rapora 0,0 gider. Hareket eden araçta eski bir noktayı
"şu anki konum" diye göndermek, haritada duran ama aslında yol alan bir araç
üretir; sunucu `latitude: {$ne: 0}` filtresiyle onu memnuniyetle kabul eder.

Uydu kesildiğinde (tünel, kapalı alan) konum bırakılmaz: ölü hesapla
tahmin edilerek sürdürülür ve rapora `positionSource:"dr"` + hata yarıçapı
(`positionAccuracy`) ile işaretlenir. Tahmin 500 m belirsizliği ya da 10
dakikayı aşarsa konum "yok" olur. Ayrıntı [wiki'nin Konum bölümünde](https://www.corezzle.com/document-content/narrtos-drive-9).

## OTA: geri alma korumalı

Mekanizma Wear ile aynı (HTTP manifest → `esp_https_ota` → A/B partisyon),
politika farklı. Fark tek bir gerçekten çıkıyor: **bu cihaza fiziksel olarak
ulaşılamıyor.**

**Geri alma açık** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Yeni firmware
kendini "geçerli" işaretlemezse bootloader bir sonraki açılışta eski sürüme
döner. Geçerleme ölçütü, hangisi önce olursa:

- telemetri yayını PUBACK aldı (boru hattı uçtan uca çalışıyor), veya
- firmware 5 dakika çökmeden çalıştı (açılış döngüsü yok)

Yalnız birincisi olsaydı broker bakımdayken güncellenen cihaz güncelleme
döngüsüne girerdi; yalnız ikincisi olsaydı açılan ama işe yaramayan firmware
geçerli sayılırdı.

**Kurulum penceresi: araç park hâlindeyken.** Yol ortasında yeniden başlatmak
canlı takibi ~30 saniye kör bırakır. Kontak sinyali bu pencereyi veriyor;
manifest'teki `forced` bayrağı pencereyi atlıyor (kritik yama).

**Bekleyen raporlar güncellemeden etkilenmez:** `buffer` bölümü app0/app1'in
dışında.

`set ota url <adres>` ile manifest adresi verilir; boş bırakılırsa OTA kapalı.
⚠ Adres Wear'ınkinden **ayrı** olmalı, yoksa Wear cihazına Drive firmware'i
iner.

## Durum LED'i

Ekran, buton ve hoparlör yok. Kablo takılıyken seri konsol, bağlantı varken
MQTT komut konusu var; ama kurulumcu aracın altında, cihaz henüz ağa
girmemişken ikisinden de yararlanamaz. Tek yerel gösterge bu LED.

Kural: **ne kadar çok ışık, o kadar kötü.**

| Desen | Anlamı |
|---|---|
| sürekli yanık | cihaz işini yapamıyor (tampon kurulamadı) |
| hızlı yanıp sönme | ayar eksik (IMEI ya da ağ) |
| uzun tek flaş / 2 sn | yaşıyor ama bağlantı yok |
| çift kısa flaş / 2 sn | ağ var, broker'a bağlanamıyor |
| çok kısa kırpma / 3 sn | her şey yolunda |
| hiç yanmıyor | besleme yok ya da cihaz ölü |

Sağlıklı durumda bile LED düzenli kırptığı için "hiç yanmıyor" tek başına
arıza demek. `led status` ile durum, `led test` ile LED'i montajda bulmak
için sürekli yakma. `PIN_STATUS_LED -1` yapılırsa servis hiç başlamaz.

## Testler

Donanım gerekmeden, gerçek `src/` dosyaları derlenerek:

```bash
tools/host-test/run.sh
```

`can_service`'in kimlik tablosunu, `store_forward`'ın flash halkasını,
`telemetry_codec`'in ürettiği telemetri dokümanını ve `nmea_parser`'ı sınar. Flash sahtesi NOR
semantiğini zorlar (silinmemiş bölgeye yazma ihlal sayılır) ve elektrik
kesintisi taklit edilebilir; codec testi dokümanı altın çıktıyla karşılaştırır.

## Durum

| Aşama | Kapsam | Durum |
|---|---|---|
| M0 | Proje iskeleti, partisyon tablosu, `os_config.h` | ✅ |
| M1 | NVS ayar deposu, USB komut arayüzü | ✅ |
| M2 | TWAI sürücüsü + `can_service` | ✅ |
| M3 | `net_service` + MQTT + store & forward | ✅ |
| M4 | Telemetri yükü (sunucu formatı) + duvar saati | ✅ |
| M5 | GNSS (NMEA) + durum LED'i | ✅ |
| M6 | OTA (geri alma korumalı) | ✅ |
| M7 | Modem arka ucu (AT + PPP) | ✅ kod / ⏳ donanım |
| M8 | Kapalı alanda konum: ölü hesap (CAN hızı + jiroskop) | ✅ kod / ⏳ donanım |
| M9 | WiFi taraması (kayıt v2, rapora `wifi` alanı) | ✅ kod / ⏳ masa testi |
| M10 | Güç yönetimi: kontak kapanınca derin uyku | ✅ kod / ⏳ masa testi |

**Drive'ın kendi donanımı henüz yok.** `os_config.h`'daki pin haritası
geçicidir; gerçek kart geldiğinde tek dosyadan düzeltilecektir.

M1-M4 ödünç bir ESP32-S3 kartında uçtan uca doğrulandı: USB konfigürasyonu,
NVS kalıcılığı, WiFi, SNTP, MQTT (QoS1 + PUBACK), store & forward ve zaman
damgası fix-up'ı. Cihazın yayınladığı doküman araç takip sunucusunin kendi DBC
çözücüsünden geçirilip 30 sinyale açıldı.

**CAN hattı test EDİLMEDİ:** transceiver yok. `can inject` TWAI sürücüsünü
atlıyor, yani "hattan frame yakalama" doğrulanmamış durumda.

**MQTT sözleşmesi henüz bilinmiyor:** broker adresi, konu şeması ve kimlik
doğrulama tahmin. Tahminlerin tamamı `src/services/mqtt_contract.h`'de; gerçek
sözleşme öğrenilince yalnız o dosya değişecek.

## Üçüncü taraf bileşenler

- [ESP-IDF](https://github.com/espressif/esp-idf) (Apache-2.0)

## Lisans

Apache License 2.0. Ayrıntı için `LICENSE` ve `NOTICE` dosyalarına bakın.

---

Corezzle Electronics
