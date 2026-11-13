Drive, NarRTOS ürün ailesinin ikinci ürünü. Birincisi **NarRTOS Wear**: dokunmatik ekranlı, kol saati benzeri bir cihaz. İkisi aynı geliştirme disiplinini paylaşıyor ama **ayrı kod depoları, ayrı sürüm numaraları, ayrı yazılım**.

## Neyi paylaşıyorlar, neyi paylaşmıyorlar

| Konu | Wear (saat) | Drive (araç) |
|---|---|---|
| Kullanıcı arayüzü | Dokunmatik ekran, 12 uygulama | **yok.** Ekran, buton, ses hiçbiri yok |
| İnternet bağlantısı | WiFi | Hücresel modem (WiFi yalnız atölyede) |
| Konum | Çevredeki WiFi ağlarından tahmin (±19 m) | Uydu. Hareket eden araçta WiFi yöntemi işlemiyor |
| Şebeke yokken | Rapor atlanıyor | Belleğe yazılıyor, sonra gönderiliyor |
| Bozuk güncelleme | Kullanıcı cihazı USB'ye takıp kurtarıyor | **Kendiliğinden geri alınıyor**, çünkü kimse ulaşamıyor |
| Güncelleme zamanı | Gece, kullanıcı uyurken | Araç park hâlindeyken |
| Cihaz kimliği | Ağ kartı adresi | IMEI. Filo sistemi bunu kullanıyor |
| Sorun teşhisi | Ekrandaki ayarlar menüsü | Kablo, uzaktan mesaj, tek LED |
| Saat kaynağı | İnternet + saat yongası | İnternet + uydu + modem (saat yongası yok) |

## Ortak kod: henüz yok, sebebi var

Doğal soru: *"İki ürün bu kadar benzer disiplin paylaşıyorsa neden kodu ortaklaştırmadınız?"*

Her aşama sonunda bu yeniden değerlendirildi. Sonuç: **kod paylaşılmıyor, kalıp paylaşılıyor.**

En güçlü aday kablosuz güncelleme kodu oldu. Wear'ın ilgili dosyası 710 satır; gerçekten ortak olabilecek çekirdek ~150 satır. Geri kalanı ürüne özgü: güncelleme ne zaman tetiklenir, ne zaman kurulur, bozuksa ne olur, cihaz nasıl tanınır. Hepsi farklı.

İki ayrı depoya paylaşılan bir bileşen koymak ek altyapı istiyor: sürüm takibi, eşgüdümlü değişiklik, ayrı test kurulumu. 150 satır iyi anlaşılmış kod için bu maliyet kendini karşılamıyor.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Karar noktası üçüncü ürün</div><div style="line-height:1.6;color:inherit">Ailede dört ürün planlanıyor. <strong>İki ürün kopyayı taşıyabilir, dört ürün taşıyamaz.</strong> Üçüncü ürün başladığında aynı kod üçüncü kez yazılacak. Asıl tetikleyici o an olacak. Üstelik o noktada hangi kodun gerçekten sabit kaldığı iki ürünlük deneyimle görülmüş olacak.</div></div>

Bu arada ortaklaştırma **cihazda değil sunucuda** yapıldı: güncelleme bildirimi biçimi Wear ile birebir aynı tutuldu, böylece tek bir sunucu iki ürünü de besleyebiliyor.
