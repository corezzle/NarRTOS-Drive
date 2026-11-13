**Kısaca:** araçta şebeke kesilmesi istisna değil, *normal*. Tünel, kapalı otopark, kırsal yol, iki baz istasyonu arası geçiş. Cihaz bu sürelerde raporları kalıcı belleğine yazıyor ve bağlantı gelince sırayla gönderiyor.

![Kapsama kesintisinde tamponun davranışı](GORSEL: tampon-zaman-cizgisi.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Kapsama kesintisinde raporlar kalıcı tampona yazılır, bağlantı dönünce sırayla gönderilir.</em></div>

Ailenin ilk ürünü olan saatte böyle bir mekanizma yoktu: WiFi yoksa rapor sessizce atlanıyordu. Bir giyilebilir cihazda kaçan ölçüm sadece eksik bir veri noktası. Araç takibinde ise **eksik bir güzergâh** demek. Filo ekranında araç kayboluyor ve hız, mesai, bölge raporları yanlış çalışıyor.

## Kaç rapor sığıyor?

2,4 MB'lık bölüm 624 rapor alıyor. Dakikada bir rapor üreten bir araçta bu **yaklaşık 10 saat** demek. Park hâlinde (10 dakikada bir) çok daha uzun.

## Neden dosya sistemi kullanılmadı?

Bilgisayarlardaki gibi bir dosya sistemi kurmak yerine bellek doğrudan sabit boyutlu bölmelere ayrıldı. Sebebi: dosya sistemi açılırken zaman ve bellek harcıyor, bozulma riski taşıyor, ve küçük parçaları sık sık yazmak tam olarak en kötü çalıştığı durum. Buna karşılık ihtiyacımız çok basit: sırayla dolan, sırayla boşalan sabit bölmeler. Dosya adı, klasör, boyut değiştirme hiçbiri kullanılmayacak.

## Elektrik kesilirse ne olur?

Kalıcı belleğin bir özelliği var: yazma işlemi bitmeden elektrik kesilirse yarım yazılmış veri kalıyor. Bunun yarım bir raporun geçerli sanılmasına yol açmaması için sıra tersine çevrildi: **önce raporun içeriği, en son "bu bölme dolu" işareti** yazılıyor.

Böylece yazma ortasında elektrik kesilirse işaret hiç oluşmuyor ve bölme boş sayılıyor. Yarım rapor hiçbir zaman gönderilmiyor.

## Bellek dolarsa hangi rapor gidiyor?

**En eskisi.** Bu, araç verisi tablosundaki kararın tam tersi (orada yeni gelen atılıyor, mevcutlar korunuyor) ve çelişki değil: orada yarışan şeyler *farklı sinyaller*, hepsi gerekliydi. Burada ise *aynı bilginin farklı zamanları* yarışıyor.

Filo ekranına bakan kişi aracın şu anki yerini görmek istiyor. 10 saat önceki bir noktayı kaybetmek, güncel noktayı kaybetmekten iyidir. Yeni raporu reddetmek ise cihazı canlı veri üretemez hâle getirirdi.

## Silme, onay geldikten sonra

Rapor gönderildikten hemen sonra silinmiyor; sunucunun "aldım" onayı bekleniyor. Tek adımda silseydik, tam o anda kopan bir bağlantı raporu hem göndermemiş hem silmiş olurdu.

## Bellek ne kadar dayanır?

Kalıcı bellek sonsuz kez yazılamıyor; her bölme yaklaşık 100.000 kez silinebiliyor. Hesap: dakikada bir rapor = günde 1440 rapor, 624 bölme = günde 2,3 tam tur. 100.000 ÷ 2,3 ≈ **43.000 gün**, yani yüz yılı aşkın. Cihazın ömrü yanında yok sayılır.
