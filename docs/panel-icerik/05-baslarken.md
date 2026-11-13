**Kısaca:** bu bölüm, Hızlı başlangıç'taki adımların açıklamalı hâli. Hiç gömülü cihaz kurmamış biri buradan adım adım ilerleyebilir: neyi neden kurduğun, her adımda ne görmen gerektiği ve bir şey ters giderse ne yapacağın yazıyor.

![Seri konsolda ilk kurulum komutları](GORSEL: konsol-kurulum.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>İlk kurulum seri konsoldan: kimlik, sunucu ve atölye ağı ayarlanıp deneme raporu üretiliyor.</em></div>

Önce tek bir şeyi kafana yerleştir: cihazın **ekranı yok**. Onunla konuşmanın tek yolu, USB kablosuyla bağlanıp klavyeden metin komutları yazmak. Telefon kurar gibi dokunarak değil, kısa komutlar yazarak ilerleyeceksin ve cihaz her komuta yazıyla cevap verecek.

## Adım 1: Bilgisayarı hazırla

İki şey kurulacak, ikisi de bir kez:

1. **PlatformIO**: gömülü yazılımları derleyip karta yükleyen araç. Terminale şunu yaz:

```
pip install platformio
```

Bilgisayarında Python yoksa önce [python.org](https://www.python.org)'dan kur; PlatformIO gereken her şeyi kendisi indirir.

1. **USB-seri sürücüsü**: bazı kartlar bilgisayara özel bir sürücüyle tanıtılır (karttaki köprü yongasına göre, ör. CH34x). Kartı taktığında bilgisayar görmezse bu sürücüyü kurup kabloyu çıkarıp tak.

## Adım 2: Kodu indir, derle, karta yükle

```
# 1. Kodu indir
git clone https://github.com/corezzle/NarRTOS-Drive
cd NarRTOS-Drive

# 2. Kart USB'de takılıyken derle ve yükle
pio run -e esp32s3_drive -t upload
```

**Ne göreceksin:** ilk derleme birkaç dakika sürer (derleyiciler iniyor), ekrandan yüzlerce satır akar. Sonunda yeşil **SUCCESS** ve yükleme yüzdeleri görünür. Sonraki derlemeler saniyeler sürer.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">"Kart bulunamadı" derse</div><div style="line-height:1.6;color:inherit">Üç şüpheli var, sırayla dene: kablo (bazı USB kablolar yalnız şarj içindir, veri taşımaz), sürücü (Adım 1'deki USB-seri sürücüsü) ve port (kartı farklı bir USB girişine tak). Kart görünüyor ama yükleme başlamıyorsa karttaki BOOT tuşuna basılı tutup tekrar dene.</div></div>

## Adım 3: Cihazla ilk konuşma

```
pio device monitor
```

Bu komut cihazın konsoluna bağlanır. Kart her açılışta neler yaptığını satır satır yazar; en sonda şu satırı görürsün:

```
seri porttan 'help' yazarak komut listesini görebilirsiniz
```

Şimdi `help` yazıp Enter'a bas: bütün komutların listesi gelir. `info` yaz: sürüm, çalışma süresi ve bellek bilgisi gelir. **Bu ikisi çalışıyorsa cihazla konuşabiliyorsun demektir.**

## Adım 4: İlk ayarlar

Cihaz fabrikadan "boş" gelir: kim olduğunu, hangi sunucuya rapor göndereceğini ve hangi ağı kullanacağını bilmez. Bunları tek tek söylüyorsun. Aşağıdakileri kendi bilgilerinle yaz; her komuttan sonra cihaz `OK` ile cevap verir:

```
set imei 351234567890123        # cihazın kimlik numarası (modem takılıysa gerekmez)
set apn internet                # SIM operatörünün veri ağı adı
set mqtt host broker.ornek.com  # raporların gideceği sunucu
set mqtt port 1883
set wifi ssid Atolye Agi        # atölyede test için WiFi adı
set wifi pass sifre123          # WiFi şifresi
get config                      # yazdıklarını kontrol et
```

| Ayar | Ne işe yarıyor | Nereden bulunur |
|---|---|---|
| `imei` | Cihazın filo sistemindeki kimliği | Modem takılıysa cihaz kendisi okur, yazmana gerek kalmaz |
| `apn` | SIM kartın veri ağına giriş adı | SIM operatöründen (çoğu operatörde "internet") |
| `mqtt host / port` | Raporların gideceği sunucu | Sunucu ekibinden |
| `wifi ssid / pass` | Atölyede, modem yokken test bağlantısı | Atölyenin WiFi bilgileri |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Şifreler ekrana basılmıyor</div><div style="line-height:1.6;color:inherit"><code>get config</code> çıktısında şifreler yerine <code><ayarlı></code> yazar. Sebebi: konsol çıktısı loglara, ekran görüntülerine ve destek yazışmalarına düşüyor.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Ayarsız cihaz boşa çalışmıyor</div><div style="line-height:1.6;color:inherit">SIM ya da WiFi bilgisi girilmemişse cihaz ağ donanımını <strong>hiç açmıyor</strong>; kurulum masasında bekleyen cihaz boşuna elektrik harcamasın diye. <code>net status</code> bu durumu "ayarsız" diye bildirir.</div></div>

## Adım 5: Çalıştığını gör

Sırayla şu üç komutu yaz ve yanlarındaki sonucu bekle:

| Komut | Görmen gereken | Görmüyorsan |
|---|---|---|
| `net status` | çevrimiçi | WiFi/SIM ayarlarını `get config` ile kontrol et |
| `telemetry now` | OK: rapor üretildi | Çıktıdaki hata satırını oku, ne eksikse onu söyler |
| `sf peek` | Gidecek raporun içeriği (JSON) | "saat henüz yok" görmen normaldir: saat öğrenilince kendiliğinden gönderilir |

Bu üçü tamamsa zincir uçtan uca çalışıyor: veri toplanıyor, rapor üretiliyor, gönderilmeye hazır. Cihaz bundan sonra kendi kendine çalışır; kontak açıkken dakikada bir, park hâlinde 10 dakikada bir rapor üretir.

## Cihaz açılışta ne yapıyor?

Merak edersen: cihaz elektrik verildiğinde parçalarını belirli bir sırayla başlatır ve her adımı konsola yazar. Sıra keyfi değil: ayarlar en önce (her şey onları okuyor), komut arayüzü hemen ardından (yanlış ayarlı cihaza müdahalenin tek yolu), sonra veri toplayıcılar, tampon, ağ, saat, konum, rapor üretici, güncelleme ve durum LED'i.

## Geliştirici notları

Aynı kaynak kod iki kart için derlenebiliyor:

| Ortam adı | Kart | Notlar |
|---|---|---|
| `esp32s3_drive` | ESP32-S3 + 8 MB bellek | Asıl hedef. Ekran olmadığı için ek bellek yongası (PSRAM) yok |
| `esp32s3_drive_lcd128` | Corezzle Touch-LCD-1.28 | Geliştirme/test kartı: 16 MB bellek. Ekranına dokunulmuyor, siyah kalıyor |

Cihazın CAN, uydu ve modem donanımı henüz elimizde yok; ilgili kod parçaları özellik anahtarlarıyla derleme dışında tutulabiliyor. Önemli olan şu: **servisler donanımdan bağımsız yazıldı**, donanım olmasa bile komutlarla veri beslenip test edilebiliyorlar.

| Anahtar | Varsayılan | Kapalıyken |
|---|---|---|
| `ENABLE_CAN` | kapalı | Araç verisi `can inject` komutuyla elle beslenir |
| `ENABLE_GNSS` | kapalı | Konum `gnss inject` komutuyla elle beslenir |
| `ENABLE_WIFI` | açık | geçerli değil |
| `ENABLE_MQTT` | açık | Raporlar tamponda birikir, gönderilmez |
| `ENABLE_MODEM` | kapalı | Modem seçilemez |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Yazılmamış bir parçayı açmak derlemeyi durduruyor</div><div style="line-height:1.6;color:inherit">Bir anahtarı yanlışlıkla açan kişi sessizce çalışmayan bir cihaz almasın diye, ilgili dosyada derlemeyi durduran bir kontrol var.</div></div>
