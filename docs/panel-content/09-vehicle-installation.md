Bu bölüm cihazı araca takan kişi için: kabloları bağladıktan sonra çalıştığını nasıl anlayacağı.

## 1. Kablolama

| Kablo | Nereye |
|---|---|
| 12 volt (+) | Kontakla kesilmeyen sürekli besleme hattı |
| Şase (−) | Araç gövdesi |
| Kontak sinyali | Kontakla gerilim alan hat → gerilim düşürücü → GPIO 10 |
| CAN H / CAN L | Aracın veri yolu → ara yonga → GPIO 4 ve 5 |
| Uydu anteni | Gökyüzünü gören yer (ön cam altı uygun) |
| Hücresel anten | Metal yüzeylerden uzak |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Cihaz aracın hattına veri YAZMIYOR</div><div style="line-height:1.6;color:inherit">Yalnız dinliyor. Yazma yeteneği yazılımda <strong>hiç kurulmuyor</strong>, yanlışlıkla bile mümkün olmasın diye. Takip cihazının aracın veri yoluna müdahale etmesi gereksiz ve riskli.</div></div>

## 2. İlk ayar (USB kablosuyla)

```
pio device monitor            # cihazın konsoluna bağlan

set apn internet              # operatörünüzün veri ağı adı
set mqtt host broker.ornek.com
set mqtt port 1883
set ota url https://ota.ornek.com/drive/manifest
get config                    # kontrol
```

IMEI yazmaya gerek yok: modem bağlıysa cihaz numarayı kendisi okuyup kaydediyor.

## 3. Bağlantıyı kontrol et

```
net status                    # "çevrimiçi" olmalı
net modem                     # bağlanmadıysa nerede takıldı?
time status                   # saat kurulmuş olmalı
mqtt status                   # "bağlı: evet" olmalı
```

`net modem` çıktısı sorunun yerini söylüyor: *SIM YOK*, *şebeke aranıyor*, *ayar reddedildi*. Her biri ayrı bir adım. "Modem bağlanmıyor" tek başına işe yaramaz; hangi adımda kaldığı lazım.

## 4. Veri akışını kontrol et

```
can stats                     # araçtan mesaj geliyor mu?
gnss status                   # uydu sabitlemesi var mı?
telemetry status              # kontak durumu doğru mu?
sf peek                       # gidecek raporu gözle gör
```

Kontağı açıp kapatın ve `telemetry status` çıktısındaki kontak satırının değiştiğini görün. Değişmiyorsa kontak kablosu ya da gerilim düşürücü hatalı.

## 5. Kabloyu çek, LED'e bak

USB çıktıktan sonra elinizdeki tek gösterge durum LED'i. Beklenen: **3 saniyede bir çok kısa kırpma**, yani her şey yolunda.

| Görüyorsanız | Yapılacak |
|---|---|
| Hızlı yanıp sönme | Ayar eksik. `get config` ile IMEI ve SIM ayarını kontrol edin |
| Uzun tek flaş | Şebeke yok. Anteni ve kapsamayı kontrol edin |
| Çift kısa flaş | Şebeke var, sunucuya bağlanamıyor. Adres, port ya da şifre hatalı |
| Sürekli yanık | Cihazın kendi belleğinde sorun var. Cihazı geri gönderin |
| Hiç yanmıyor | Elektrik gelmiyor, cihaz arızalı ya da derin uykuda (uyku açıksa park hâlinde normaldir) |

## 6. Kablosuz kontrol (isteğe bağlı)

Cihaz ağa girdikten sonra kablo takmadan da sorgulanabiliyor:

```
mosquitto_pub -h <sunucu> -t 'narrtos/drive/<imei>/cmd' -m 'net status'
mosquitto_sub -h <sunucu> -t 'narrtos/drive/<imei>/cmd/reply'
```

Araç sahada, cihaz aracın altında ve kimse ona ulaşamıyorken tek müdahale yolu bu.
