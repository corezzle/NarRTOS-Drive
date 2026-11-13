**Kısaca:** cihaz düzenli aralıklarla bir rapor hazırlıyor. İçinde konum, hız, kontak durumu ve o anki araç verileri var. Biçim filo sisteminin beklediğine göre belirlendi. Tahminle değil, sunucu kodu okunarak.

## Rapordaki alanlar

| Alan | Ne | Bilinmiyorsa |
|---|---|---|
| `imei` | Cihaz kimliği | her zaman var |
| `dateTime` | Raporun üretildiği an | Rapor gönderilmiyor |
| `latitude`, `longitude` | Enlem, boylam | `0` |
| `speed` | Hız (km/sa) | `0` |
| `direction` | Gidiş yönü (derece) | `0` |
| `ignitionState` | Kontak açık mı | her zaman var |
| `satelliteCount` | Görülen uydu sayısı | `0` |
| `gsmSignalQuality` | Şebeke sinyal gücü | `-1` |
| `fuel` | Yakıt seviyesi | `-1` |
| `positionSource` | Konum uydudan mı tahminden mi (`gnss` / `dr` / `none`) | `none` |
| `positionAccuracy` | Konumun hata yarıçapı (metre) | `-1` |
| `wifi` | Çevredeki ağlar (MAC + sinyal gücü), kapalı alan konumlaması için | alan hiç yazılmaz |
| `can` | Ham araç verisi listesi | boş liste |

Rölanti süresi, çalışma süresi ve araç durumu gibi alanları **sunucu kendisi hesaplıyor**; cihaz göndermiyor.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">"Bilinmiyor" için özel değerler</div><div style="line-height:1.6;color:inherit">Sunucu kodunda yakıt için <code>-1</code>, konum için <code>0</code> "bu bilgi yok" anlamında kullanılıyor. Üstelik sunucu güzergâh çizerken konumu <code>0</code> olan noktaları zaten atlıyor. Yani uydu sabitlemesi olmadığında gönderilen <code>0,0</code> haritada yanlış bir nokta oluşturmuyor, sessizce eleniyor. Cihaz bu değerleri bilerek kullanıyor.</div></div>

## Örnek rapor

```
{"imei":"351234567890123",
 "dateTime":"2026-08-14T13:22:32.390Z",
 "latitude":41.0123456,"longitude":29.0123456,
 "speed":54.3,"ignitionState":true,"direction":123.4,
 "gsmSignalQuality":-1,"satelliteCount":0,"fuel":-1.0,
 "positionSource":"gnss","positionAccuracy":5,
 "can":["0x18FEE500,01,02,03,04,05,06,07,08",
        "0x0CF00400,FF,7D,80,7D,7D,00,00,FF"]}
```

## Belleğe okunabilir metin değil, sıkıştırılmış biçim yazılıyor

Rapor tampona yukarıdaki okunabilir metin olarak değil, sıkıştırılmış ikili biçimde yazılıyor. Metne çevirme işi *gönderim anında* yapılıyor. İki sebep var:

1. **Zaman damgası.** Rapor üretildiği anda cihaz saati bilmiyor olabilir. Metni o an yazsaydık damga ya yanlış olurdu ya da rapor çöpe gidecekti. Ayrıntısı bir sonraki bölümde.

2. **Yer.** Sıkıştırılmış biçim en çok 933 bayt, metin hâli ~2.900 bayt. Aynı bellekte üç kat fazla rapor duruyor: 10 saat yerine yaklaşık 30 saat.

## Rapor sıklığı

| Durum | Sıklık |
|---|---|
| Kontak açık (araç çalışıyor) | 1 dakika |
| Kontak kapalı (park) | 10 dakika |
| Kontak değişimi | **anında** |

Kontak değişimi beklemiyor, çünkü filo sisteminin mesai başlangıç/bitiş raporu tam bu ana dayanıyor. 10 dakika gecikmeli bildirmek o raporu doğrudan yanlışlar.
