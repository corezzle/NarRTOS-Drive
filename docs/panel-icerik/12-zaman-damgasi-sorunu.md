**Kısaca:** cihazda pil destekli saat yongası yok. Elektrik kesilince saati kaybediyor ve tekrar öğrenmek için internete ya da uyduya ihtiyacı var. Ama raporların çoğu tam da bağlantı yokken üretiliyor. Bu belgede en çok kafa yorulan konu bu.

![Zaman damgası karar akışı](GORSEL: zaman-damgasi.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Gönderim anında damga kararı: dört hâlin dördü de bilinçli.</em></div>

## Sorun nasıl ortaya çıkıyor

Kapalı bir otoparkta duran kamyonu düşünün. Sürücü kontağı açıyor, cihaz çalışmaya başlıyor, ama ne uydu görüyor ne şebeke çekiyor. Yani **saatin kaç olduğunu bilmiyor.** 20 dakika boyunca veri toplayıp belleğe yazıyor. Sonra araç açık havaya çıkıyor, şebeke geliyor, saat öğreniliyor.

O 20 dakikalık verinin zaman damgası ne olacak?

## Çözüm: sonradan geriye doğru hesaplamak

Cihazın iki farklı saati var:

- **Duvar saati**: gerçek tarih ve saat. Öğrenilmesi bağlantı gerektiriyor, öğrenilince ileri/geri sıçrayabiliyor.

- **Açılıştan beri geçen süre**: her zaman biliniyor, hiç sıçramıyor, ama tarih bilgisi taşımıyor.

Cihaz her raporun yanına "açılışın kaçıncı saniyesinde üretildim" bilgisini yazıyor. Saat sonradan öğrenildiğinde şu hesap yapılıyor:

```
Şimdi saat 07:20 ve açılışın 1200. saniyesindeyiz.
Bu rapor açılışın 60. saniyesinde üretilmiş.
Fark 1140 saniye = 19 dakika.
Demek ki rapor 07:01'de üretildi.
```

Bu sayede otoparkta biriken raporlar **üretildikleri saatlerin damgasıyla** gönderiliyor, gönderildikleri saatle değil.

## Saat üç kaynaktan öğrenilebiliyor

| Kaynak | Neye ihtiyaç var |
|---|---|
| İnternet zaman sunucusu | Çalışan bağlantı |
| Uydu alıcısı | Uydu sabitlemesi. Şebeke gerekmiyor |
| Hücresel modem | Şebekeye kayıt |

Uydu kaynağı özellikle değerli: şebeke hiç çekmeyen bir yerde bile doğru damga üretilebiliyor.

## Çözülemeyen durum ve nasıl ele alındığı

Bir açık uç var: cihaz yeniden başlarsa "açılıştan beri geçen süre" sayacı sıfırlanıyor. Yeniden başlatmadan önce yazılmış, damgası olmayan bir raporun sayacı yeni oturumda anlamsız hâle geliyor. Geriye hesaplasak rastgele bir tarih çıkardı.

Bu yüzden her açılışta rastgele bir kimlik üretiliyor ve raporlara yazılıyor. Kimlik uyuşmuyorsa rapor **düşürülüyor**; beklemek de anlamsız, çünkü o rapor bir daha asla damgalanamaz ve sırada bekleyen diğerlerini sonsuza kadar tıkardı.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Saat bilinmiyorsa rapor gönderilmiyor</div><div style="line-height:1.6;color:inherit">Tamponda bekliyor. Yanlış damgalı bir rapor filo veritabanına girdikten sonra ayıklanamaz: güzergâhı, hız raporunu ve mesai raporunu aynı anda bozar. Beklemek çok daha ucuz, zaten tamponun varlık sebebi bu.</div></div>
