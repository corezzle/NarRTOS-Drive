Belgede geçen bütün teknik terimler. Konu başlıklarına göre gruplandı.

## Araç verisi

| Terim | Anlamı |
|---|---|
| **CAN** | Araçlardaki standart veri yolu. İki telli; motor, fren, gösterge paneli hepsi buradan haberleşiyor. |
| **Frame** | CAN hattında geçen tek bir mesaj: bir kimlik numarası ve en çok 8 bayt veri. |
| **TWAI** | Cihazın işlemcisindeki CAN denetleyicisinin adı. Denetleyici var ama elektriksel arayüz yok. |
| **Transceiver** | İşlemcinin sinyalini CAN hattının elektriksel biçimine çeviren ara yonga. Olmadan hat kullanılamaz. |
| **J1939** | Ağır ticari araçlarda kullanılan CAN üst kuralları. |
| **PGN** | J1939'da bir mesaj grubunun numarası. Motor devri, hız, yakıt gibi. |
| **DBC** | Hangi mesajın hangi bitinin hangi sinyal olduğunu tanımlayan dosya. Bu projede 5,7 MB, sunucuda duruyor. |
| **Öncelik bitleri** | CAN kimliğinin baş kısmı; mesajın aciliyetini belirtiyor. Sunucudaki eşleştirme buna duyarlı. |
| **Kontak** | Aracın çalışma durumu. Rapor sıklığını ve güncelleme zamanını belirliyor. |

## Konum

| Terim | Anlamı |
|---|---|
| **GNSS** | Uydu konumlama sistemlerinin genel adı. GPS bunlardan biri; GLONASS, Galileo, BeiDou diğerleri. |
| **NMEA** | Konum alıcılarının kullandığı metin biçimi. Her satır doğrulama koduyla bitiyor. |
| **RMC / GGA** | İki NMEA satır tipi. RMC tarih+saat taşıyor, GGA uydu sayısı. |
| **Sabitleme (fix)** | Alıcının yeterli uyduyu görüp konumu hesaplayabilmiş olması. |
| **Bayat sabitleme** | Geçerli ama eski konum. Hareket eden araçta yanlış; 10 saniyeden eskisi kullanılmıyor. |
| **Ölü hesap** | Uydu yokken konumu bilinen son noktadan yürütme: alınan yol aracın hızından, dönüş jiroskoptan. Tahmindir; hatası yol aldıkça büyür ve rapora hata payıyla birlikte yazılır. |
| **Jiroskop** | Dönüş hızını ölçen sensör. Aracın ne kadar döndüğünü söyler; tek başına konum üretemez. |
| **İvmeölçer** | İvmeyi ölçen sensör. Konum çıkarmak için kullanılmıyor: küçük ölçüm sapması zamanla çok hızlı büyüdüğü için tünel geçişinde işe yaramaz. |
| **MAC adresi** | Her ağ cihazının fabrikadan gelen benzersiz kimliği. Rapordaki WiFi listesinde ağlar bununla ayırt ediliyor. |
| **Sinyal gücü (RSSI)** | Bir ağın ne kadar güçlü duyulduğu. Konum çözümünde kabaca "ne kadar yakın" ipucu. |

## Bağlantı

| Terim | Anlamı |
|---|---|
| **IMEI** | Modemin fabrikadan gelen 15 haneli benzersiz numarası. Filo sistemi cihazları bununla ayırt ediyor. |
| **APN** | Operatörün veri ağına giriş adı. Yanlışsa modem şebekeye kaydolur ama veri geçmez. |
| **AT komutu** | Modemlerin metin tabanlı komut dili. |
| **Kapsama** | Hücresel şebekenin çektiği alan. |
| **PPP** | Seri hat üzerinden internet taşıma yöntemi. Modem böylece cihazın ağ arayüzü oluyor. |

## Mesajlaşma

| Terim | Anlamı |
|---|---|
| **MQTT** | Kanal tabanlı, hafif mesajlaşma protokolü. IoT cihazlarında yaygın. |
| **Kanal (topic)** | Mesajın gönderildiği adres. `narrtos/drive/<imei>/telemetry` gibi. |
| **QoS 1** | "En az bir kez" teslim. Alıcı onay verene kadar gönderen mesajı saklıyor; mesaj iki kez gelebilir. |
| **Onay (PUBACK)** | Sunucunun "mesajı aldım" bildirimi. Bu gelmeden cihaz raporu silmiyor. |
| **Saklanan mesaj (retained)** | Sunucunun kanaldaki son mesajı tutması. Yeni bağlanan hemen son durumu görüyor. |
| **Vasiyet (LWT)** | Bağlantı beklenmedik şekilde koptuğunda sunucunun cihaz adına yayınladığı mesaj. |
| **Broker** | Mesajları gönderenlerden alıp alıcılara dağıtan sunucu. |

## Cihaz ve bellek

| Terim | Anlamı |
|---|---|
| **Firmware** | Cihazın içindeki yazılım. |
| **Tampon** | Gönderilemeyen raporların biriktirildiği alan. Burada kalıcı bellekte, 624 rapor kapasiteli. |
| **Kalıcı bellek (flash)** | Elektrik kesilse de içeriğini koruyan bellek. Yazma kurallarının bir özelliği var: veriyi değiştirmek için önce bölmeyi silmek gerekiyor. |
| **Ayar deposu (NVS)** | IMEI, sunucu adresi gibi ayarların saklandığı bölüm. |
| **A/B bölüm** | İki yazılım bölümü. Güncelleme boşta olana yazılıyor; yarım kalan güncelleme cihazı bozmuyor. |
| **Kablosuz güncelleme (OTA)** | Cihazın yazılımını kablo bağlanmadan güncellemesi. |
| **Geri alma (rollback)** | Yeni yazılım kendini sağlam ilan etmezse cihazın eski sürüme dönmesi. |
| **GPIO** | İşlemcinin dışa açılan numaralı bacakları. |
| **UART** | İki cihazın kablo üzerinden yazışmasını sağlayan seri haberleşme birimi. |
| **Derin uyku** | İşlemcinin neredeyse tamamen kapandığı, tüketimin binlerce kat düştüğü bekleme hâli. Uyanmak yeniden açılış demektir; kalıcı bellek, ayarlar ve saat korunur. |
| **RTC belleği** | Derin uykuda enerjili kalan küçük bellek bölgesi. Uyku sayacı gibi uykular arası taşınması gereken küçük veriler burada tutulur. |
| **Watchdog** | Takılan bir işi fark edip cihazı yeniden başlatan sayaç. Araçta kimse müdahale edemediği için açık. |
| **Duvar saati / monoton saat** | Birincisi gerçek tarih-saat (öğrenmek bağlantı gerektiriyor), ikincisi açılıştan beri geçen süre (hep biliniyor ama tarih taşımıyor). |
