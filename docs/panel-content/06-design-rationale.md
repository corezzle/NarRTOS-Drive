**Kısaca:** yazılım dört katmana ayrılmış. En üstte iş yapan servisler, altında donanım sürücüleri. Aralarında bir katman daha var ve projenin en özgün yanı o: *donanım bilmeyen saf modüller*. Bunlar geliştirme bilgisayarında test edilebiliyor, yani cihaz elimizde olmadan doğrulanabiliyorlar.

## Katmanlar

<pre style="background:#0B1626;color:#E4F0EE;border-radius:12px;padding:16px 20px;overflow-x:auto;font-size:13px;line-height:1.5;font-family:Menlo,Consolas,monospace">+--------------------------------------------------------------+
|  SERVİSLER    rapor üretimi, gönderim, ağ, konum, saat,       |
|               güncelleme, tampon, komut arayüzü               |
+--------------------------------------------------------------+
|  SAF MODÜLLER veri biçimlendirici, uydu cümlesi çözücü,       |
|  (bilgisayarda güncelleme bildirimi çözücü, modem yanıtı      |
|   test edilir)  çözücü                                        |
+--------------------------------------------------------------+
|  SÜRÜCÜLER    CAN, uydu alıcısı UART'ı, modem/WiFi,           |
|               kalıcı bellek erişimi                          |
+--------------------------------------------------------------+
|  ÇEKİRDEK     ayar deposu, merkezî yapılandırma               |
+--------------------------------------------------------------+</pre>

Ailenin ilk ürünü olan Wear'da bir **uygulama katmanı** vardı (saat kadranı, ayarlar menüsü, galeri…). Drive'da yok, çünkü kullanıcı yok. Onun yerini saf modül katmanı aldı.

## Verinin yolu

Üç kaynaktan gelen veri tek bir rapora dönüşüyor, kalıcı belleğe yazılıyor, sonra gönderiliyor:

<pre style="background:#0B1626;color:#E4F0EE;border-radius:12px;padding:16px 20px;overflow-x:auto;font-size:13px;line-height:1.5;font-family:Menlo,Consolas,monospace">  CAN hattı          uydu alıcısı        kontak kablosu
      |                   |                   |
      v                   v                   v
 96 sinyallik       taze sabitleme      rapor zamanlayıcı
 tablo              (10 sn'den eski     (kontak açık: 1 dk
                     olan kullanılmaz)   park: 10 dk)
      |                   |                   |
      +---------+---------+---------+---------+
                          |
                          v
              +-------------------------+
              |  RAPOR                  |  sıkıştırılmış ikili
              |  en çok 933 bayt        |  biçim. Okunabilir
              |                         |  metne SONRA çevrilir
              +-------------------------+
                          |
                          v
              +-------------------------+
              |  KALICI BELLEK          |  624 rapor sığıyor
              |  (tampon)               |  elektrik kesilse
              +-------------------------+  bile kaybolmuyor
                          |
                          v
              +-------------------------+
              |  METNE ÇEVİR            |  zaman damgası
              |                         |  burada hesaplanıyor
              +-------------------------+
                          |
                          v
                    şebeke üzerinden
                       GÖNDER
                          |
                          v
                    ==== SUNUCU ====
                          |
              +-----------+-----------+
              |                       |
              v                       v
      araç verisini çöz,      "aldım" onayı
      veritabanına yaz          geri döner
                                      |
                                      v
                            rapor bellekten silinir
                            (ancak ŞİMDİ)</pre>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Akışın iki kuralı</div><div style="line-height:1.6;color:inherit"><strong>1. Her rapor tampondan geçiyor</strong>, şebeke varken bile. Sebebi: yalnızca arıza anında kullanılan bir yol, ihtiyaç duyulduğunda bozuk çıkan yoldur. Her rapor aynı yoldan geçince o yol sürekli sınanıyor.<br><br><strong>2. Rapor, sunucu "aldım" demeden silinmiyor.</strong> Gönderdikten hemen sonra silseydik, tam o anda kopan bir bağlantı raporu hem göndermemiş hem silmiş olurdu.</div></div>

## İşlerin paylaşımı

Cihazın işlemcisi iki çekirdekli. İşler ikisine dağıtılmış:

| İş | Çekirdek | Öncelik |
|---|---|---|
| CAN dinleme | 1 | en yüksek |
| Ağ bağlantısı | 0 | yüksek |
| Rapor üretimi, gönderim, konum | 0 | orta |
| Komut arayüzü | 0 | orta |
| Güncelleme, durum LED'i | 0 | en düşük |

CAN dinleme ayrı çekirdekte ve en yüksek öncelikte, çünkü hattan saniyede binlerce mesaj geçiyor. Modem ya da gönderim işi 20 saniye takılsa bile araç verisi kaçmıyor.

## Bellek bölümleri

Cihazın 8 MB'lık kalıcı belleği parçalara ayrılmış:

```
ayarlar     20 KB     IMEI, sunucu adresi, SIM ayarı
yazılım A    2,8 MB   çalışan firmware
yazılım B    2,8 MB   güncelleme buraya yazılıyor
tampon       2,4 MB   gönderilemeyen raporlar (624 rapor)
```

**İki yazılım bölümü olmasının sebebi güvenlik.** Güncelleme boştaki bölüme yazılıyor; başarılıysa cihaz ona geçiyor. Yarıda kalan bir güncelleme çalışan yazılıma dokunmadığı için cihaz bozulmuyor.

Tampon yazılım bölümlerinin dışında duruyor. Yani **güncelleme, bekleyen raporları silmiyor.**
