**Kısaca:** cihaz tek başına işe yaramaz. Karşısında iki servis olmalı: raporları alıp veritabanına yazan servis ve yazılım güncellemesi bildiren servis. Bu bölüm sunucu ekibinin ne yapması gerektiğini anlatıyor.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Aşağıdakiler cihazın şu anki varsayımı</div><div style="line-height:1.6;color:inherit">Sunucu ekibiyle mutabık kalınmış hâli değil. Adresler, kanal adları ve kimlik doğrulama netleşince cihazda <strong>tek bir dosya</strong> değişecek. Netleşmesi gereken maddeler bölümün sonunda listeli.</div></div>

## Mesaj kanalları

Cihaz MQTT adlı bir mesajlaşma protokolü kullanıyor. Mesajlar "kanal" (topic) adı verilen adreslere gönderiliyor. Kanal adı `narrtos/drive` ile başlıyor, ardından cihazın IMEI numarası geliyor:

| Kanal | Yön | İçerik |
|---|---|---|
| `narrtos/drive/<imei>/telemetry` | cihaz → sunucu | Rapor |
| `narrtos/drive/<imei>/status` | cihaz → sunucu | `online` / `offline` |
| `narrtos/drive/<imei>/cmd` | sunucu → cihaz | Komut |
| `narrtos/drive/<imei>/cmd/reply` | cihaz → sunucu | Komut yanıtı |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">"Cihaz sustu" ile "araç durdu" nasıl ayırt ediliyor?</div><div style="line-height:1.6;color:inherit">Cihaz bağlanırken sunucuya bir vasiyet bırakıyor: "bağlantım beklenmedik şekilde koparsa benim adıma <code>offline</code> yaz." Mesajlaşma sunucusu kopmayı fark ettiğinde bunu kendisi yayınlıyor.<br><br>Böylece filo ekranı "araç park etti, rapor göndermiyor" ile "cihaza erişilemiyor" arasındaki farkı ek bir zaman aşımı mantığı kurmadan görebiliyor.<br><br>Güç yönetimi açık cihaz park hâlinde <strong>bilerek</strong> uyur: uyumadan önce <code>offline</code> yayınlar, park raporu periyodunda kısaca uyanıp raporunu gönderir ve tekrar uyur. Yani park hâlindeki cihazın çoğunlukla offline görünmesi arıza değildir.</div></div>

## Rapor alan servis ne yapmalı

1. `narrtos/drive/+/telemetry` kanalına **onaylı teslim** (QoS 1) ile abone ol. Cihaz onay gelmeden raporu silmiyor; onay vermeyen bir alıcı cihazın belleğini doldurur.

2. Gelen raporu doğrula: içindeki IMEI kanal adındaki IMEI ile aynı olmalı.

3. Ham araç verisini tanım dosyasıyla çöz. Cihaz işlenmemiş veri gönderiyor.

4. Zaman damgasını metin biçiminden tarih tipine çevir, çünkü veritabanı sorguları tarih karşılaştırması yapıyor.

5. Raporu ilgili araç koleksiyonuna yaz.

6. Türetilmiş bilgileri hesapla: araç durumu, rölanti süresi, çalışma süresi. Cihaz bunları göndermiyor.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Rapordaki <code>wifi</code> listesi nasıl çözülür?</div><div style="line-height:1.6;color:inherit">Kapalı alanda üretilen raporlarda <code>wifi</code> alanı olabilir: çevredeki ağların MAC + sinyal listesi. Ailenin saat ürününün sunucu yazılımında bu listeyi konuma çeviren hazır bir servis var (geolocation.service.js): Ichnaea/beaconDB uyumlu, <code>GEOLOCATE_URL</code> ve <code>GEOLOCATE_KEY</code> ayarlarıyla Google Geolocation API'ye geçiyor, sonuçları önbellekliyor. Çözülen konum tahmini konumun <em>doğrulaması</em> olarak kullanılmalı, ölçülmüş konum olarak değil.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Aynı rapor iki kez gelebilir</div><div style="line-height:1.6;color:inherit">Onay zamanında gelmezse cihaz raporu <strong>silmeyip tekrar gönderiyor</strong>. Kaybetmek yerine tekrarlamak tercih edildi. Alan servis, cihaz kimliği ve zaman damgası ikilisiyle tekilleştirme yapmalı. Yoksa güzergâhta çift noktalar oluşur.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Raporlar gecikmeli gelebilir</div><div style="line-height:1.6;color:inherit">Şebeke kesintisi sonrası tampon boşalırken saatler önce üretilmiş raporlar arka arkaya düşüyor. Rapordaki zaman damgası <strong>üretim anıdır, geliş anı değil</strong>. Alan servis "şimdi geldi = şimdi oldu" varsayamaz; sıralamayı damgaya göre yapmalı.</div></div>

## Güncelleme bildiren servis

Cihaz 6 saatte bir soruyor:

```
GET <adres>?device_id=<imei>&current=<sürüm>&product=drive
```

Beklenen yanıt:

```
{
  "version": "0.2.0",
  "version_code": 200,
  "url": "https://ota.ornek.com/drive/0.2.0.bin",
  "size": 924235,
  "notes": "CAN tablosu düzeltmesi",
  "forced": false
}
```

| Alan | Zorunlu | Davranış |
|---|---|---|
| `version_code` | evet | Cihazdakinden **büyükse** kurulur; eşit veya küçükse kurulmaz |
| `url` | evet | Yazılım dosyasının adresi |
| `version`, `size`, `notes` | hayır | Bilgi amaçlı |
| `forced` | hayır | `true` ise park beklenmez |

Zorunlu alanı eksik bir yanıt reddediliyor, çünkü yarım bir bildirim kurulum başlatmamalı.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Güncelleme adresi diğer ürününkinden ayrı olmalı</div><div style="line-height:1.6;color:inherit">Aynı adres paylaşılırsa saate araç yazılımı iner. Cihaz ayırt edilebilmek için <code>product=drive</code> gönderiyor, ama <strong>güvenilir ayrım sunucu tarafında</strong> yapılmalı: ayrı bir adres ya da bu parametreye göre zorunlu filtre.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">"Acil" işareti dikkatli kullanılmalı</div><div style="line-height:1.6;color:inherit"><code>forced: true</code> aracı <strong>yol ortasında</strong> yeniden başlatabilir ve takip yaklaşık 30 saniye kesilir. Yalnız gerçek kritik yamalarda kullanılmalı.</div></div>

## Sunucu ekibiyle netleşmesi gerekenler

1. **Sunucu adresi ve portu.** Şifreli bağlantı kullanılacak mı? Kullanılacaksa sertifika nasıl sağlanacak?

2. **Kimlik doğrulama.** Kullanıcı adı/şifre mi, sertifika mı, jeton mu? Cihazda alanlar hazır ama boş.

3. **Kanal adları.** Yukarıdaki isimler cihazın varsayımı.

4. **Cihaz kimliği** olarak IMEI kabul edilecek mi?

5. **Toplu gönderim.** Tampon boşalırken raporlar tek tek gidiyor. Sunucu liste hâlinde kabul ederse çok daha verimli olur.

6. **Güncelleme adresi** ve yazılım dosyalarının barındırılacağı yer.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Sunucu ekibine iletilmesi gereken bir bulgu</div><div style="line-height:1.6;color:inherit">Sunucudaki araç verisi eşleştirmesi, mesajın <strong>öncelik bilgisine duyarlı</strong>. Aynı sinyali farklı öncelikle yayınlayan bir araçta veri çözülmüyor ve bu <em>sessizce</em> oluyor. Sunucu hata vermiyor. Cihaz kimliği hiç değiştirmeden gönderdiği için sorun cihazda değil; ama sahada "sinyal gelmiyor" olarak görüleceği için tanım dosyası tarafında öncelikten bağımsız eşleştirme değerlendirilmeli.</div></div>
