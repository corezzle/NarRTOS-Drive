**Kısaca:** cihaz yazılımını kendi kendine güncelleyebiliyor. Mekanizma ailenin ilk ürünüyle aynı ama *politika* tamamen farklı, ve bunun tek bir sebebi var: **bu cihaza kimse fiziksel olarak ulaşamıyor.** Saatte bozuk bir güncelleme kullanıcıyı rahatsız eder, kullanıcı cihazı bilgisayara takar. Araç cihazı ise aracın altında, ekransız, belki başka şehirde.

![A/B bölüm ve geri alma akışı](GORSEL: ota-rollback.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>A/B bölüm ve geri alma: kendini kanıtlayamayan güncelleme eski sürüme döner.</em></div>

## Bozuk güncelleme kendini geri alıyor

Güncelleme boştaki yazılım bölümüne yazılıyor ve cihaz ona geçiyor. Yeni yazılım **kendini "sağlam" ilan etmezse**, bir sonraki açılışta cihaz eski sürüme dönüyor.

Peki "sağlam" ne demek? İki koşuldan biri yeterli, hangisi önce gerçekleşirse:

1. **Bir rapor gönderilip sunucudan onay alındı**. Bütün zincir çalışıyor demek, en güçlü kanıt

2. **Yazılım 5 dakika boyunca çökmeden çalıştı**

Neden ikisi birden? Yalnız birincisi olsaydı, sunucu bakımdayken güncellenen bir cihaz kendini asla sağlam ilan edemez, ilk elektrik kesintisinde eski sürüme döner ve **sonsuz güncelleme döngüsüne** girerdi. Sunucu arızası yazılım hatası değil. Yalnız ikincisi olsaydı, açılan ama işe yaramayan bir yazılım sağlam sayılırdı.

## Güncelleme ne zaman kuruluyor?

**Araç park hâlindeyken.** Yol ortasında yeniden başlatmak takibi yaklaşık 30 saniye kesiyor ve filo ekranında araç kayboluyor.

Saatte bunun karşılığı "gece, kullanıcı uyurken"di. Araçta daha iyi bir sinyal var: kontak.

Sunucu bir güncellemeyi "acil" işaretleyebiliyor; o durumda park saati beklenmiyor. Kritik bir güvenlik yaması için ayrılmış bir yol.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Kurulum sırasında kontak açılırsa?</div><div style="line-height:1.6;color:inherit">Sorun olmuyor. Güncelleme boştaki bölüme yazıldığı için yarım kalan yazma hiç devreye alınmıyor; cihaz eski yazılımla çalışmaya devam ediyor.</div></div>

## Sürüm karşılaştırması

Cihaz sunucudan gelen sürüm numarasını kendisiyle karşılaştırıyor. Karşılaştırma metin olarak değil **sayı olarak** yapılıyor, çünkü metin karşılaştırmasında `"0.10.0"` `"0.2.0"`'dan küçük çıkıyor, bu da güncellemede yanlış yöne gitmek demek.

Sunucu yanlışlıkla eski bir sürüm bildirirse kurulum yapılmıyor; sahadaki araçlar geri düşürülmüyor.
