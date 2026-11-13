**Kısaca:** cihaz metin komutlarıyla sürülüyor. Aynı komutlar iki yerden gönderilebiliyor: USB kablosuyla bilgisayardan, ya da bağlantı varken uzaktan.

![Seri konsolda pos status ve power status çıktıları](GORSEL: konsol-teshis.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Sahada teşhis: pos status tünelde tahmine geçildiğini, power status uyku kararını gösteriyor.</em></div>

## Ayar komutları

| Komut | Ne yapar |
|---|---|
| `set imei <numara>` | Cihaz kimliği |
| `set mqtt host\|port\|user\|pass` | Sunucu ayarları |
| `set wifi ssid\|pass` | Atölye WiFi'si (boşluklu isim yazılabilir) |
| `set net backend` | Modem mi WiFi mi kullanılacak |
| `set apn <ad>` | Operatörün veri ağı adı |
| `set power sleep <0\|1>` | Kontak kapanınca derin uyku (sahada 1 olmalı) |
| `set ota url <adres>` | Güncelleme adresi (boş = güncelleme kapalı) |
| `set can bitrate <hız>` | Araç hattı hızı |
| `set gnss baud <hız>` | Uydu alıcısı hızı |
| `get config` | Bütün ayarları göster |
| `config clear` | Fabrika ayarlarına dön |

## Durum komutları

| Komut | Ne gösterir |
|---|---|
| `info` | Sürüm, çalışma süresi, bellek, ağ, tampon durumu |
| `net status` | Bağlantı durumu |
| `net modem` | Modem hangi adımda, sinyal gücü, IMEI |
| `mqtt status` | Sunucu bağlantısı ve sayaçlar |
| `time status` | Saat ve nereden öğrenildiği |
| `gnss status` | Sabitleme, uydu sayısı, sayaçlar |
| `pos status` | Konum kaynağı (uydu/tahmin), hata yarıçapı, jiroskop |
| `wifi scan` · `wifi status` | Çevredeki ağları tara / tarama sayaçları |
| `sf stats` | Tampon doluluğu ve kalan süre |
| `can stats` · `can dump` | Araç verisi tablosu |
| `telemetry status` | Rapor sıklığı, kontak durumu |
| `ota status` | Sürüm ve güncelleme durumu |
| `power status` | Uyku kararı, uyanma sebebi, uyku sayısı |
| `led status` | LED ne gösteriyor |
| `selftest` | Donanım checklist'i — tüm çevrebirimleri sırayla sınar (bkz. Test ve doğrulama) |

## Donanımsız test komutları

CAN ara yongası, uydu alıcısı ve modem elimizde yok. Bu komutlar **sürücü katmanını tamamen atlayıp** doğrudan servise veri veriyor. Yani gerçek veriyle elle beslenen veri, sistemin içinde ayırt edilemiyor.

```
can inject 0x18FEE500,01,02,03,04,05,06,07,08   # araç verisi
gnss inject $GPRMC,...*XX                       # konum
telemetry ignition 1        # kontağı aç, anında rapor üretilir
telemetry now               # hemen bir rapor üret
sf peek                     # gidecek raporu göster
net offline 1               # şebeke kaybını taklit et
sf stats                    # tampon doluyor mu?
net offline 0               # şebeke döndü, tampon boşalır
```
