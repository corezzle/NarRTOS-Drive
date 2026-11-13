**Kısaca:** kontak kapanınca cihaz bekleyen işlerini bitirip **derin uykuya** geçebiliyor. Uykuda tüketim binlerce kat düşüyor; araç haftalarca park etse de akü cihaz yüzünden bitmiyor. Özellik varsayılan olarak kapalı geliyor ve sahaya çıkan cihazda `set power sleep 1` ile açılıyor.

![Güç yönetimi durum döngüsü](GORSEL: guc-dongusu.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Güç döngüsü: bekleme, derin uyku ve iki uyanma yolu.</em></div>

## Neden var?

Cihaz aracın aküsünden besleniyor ve kontak kapalıyken de tam güçte çalışırsa aküyü yavaş yavaş boşaltıyor. Uzun parkta (havalimanı, servis, tatil) bunun sahadaki adı "takip cihazı aracımı çalışmaz yaptı"dır ve bir takip ürünü için bundan kötü şöhret olmaz.

## Kontak kapanınca ne oluyor?

1. Cihaz hemen uyumuyor: **5 dakika** bekliyor. Bu süre kontak raporunun ve tamponda bekleyenlerin gönderilmesine, hazır bir güncelleme varsa kurulmasına yetiyor. Kısa duraklamalarda (market, trafik) gereksiz uyu/uyan döngüsü de böylece önleniyor.

2. Bekleme dolunca ve iş kalmayınca sunucuya `offline` bilgisi bırakılıp derin uykuya geçiliyor.

3. Uykudan iki şey uyandırıyor: **kontak açılırsa anında**; ya da park raporu zamanı gelince **zamanlayıcıyla**. Zamanlayıcı uyanışında cihaz bir park raporu üretip kısa bir pencere içinde işini bitiriyor ve tekrar uyuyor.

## Uyumayı erteleyen ve uyuduran hâller

| Durum | Karar |
|---|---|
| Kontak açık ya da yeni kapandı | uyumaz |
| Güncelleme iniyor ya da kurulum bekliyor (ağ varsa) | uyumaz |
| Tampon boşalıyor (ağ varsa) | uyumaz, tavana kadar |
| İş kalmadı | uyur |
| Ağ yok (tampon dolu olsa bile) | uyur; kayıtlar kalıcı bellekte bekler |
| Tampon boşalmıyor, tavan doldu | uyur |

Karar mantığı ayrı ve saf bir modülde duruyor ve bilgisayarda testle sınanıyor, çünkü iki yönde de hata pahalı: gereksiz uyanık kalmak akü bitirir, erken uyumak rapor ve güncelleme kaçırır.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Saat uykuda kaybolmuyor</div><div style="line-height:1.6;color:inherit">Cihazın saat tutan devresi (RTC) derin uykuda çalışmaya devam ediyor. Uyanan cihaz saati yeniden öğrenmek zorunda kalmıyor; park raporunun zaman damgası uyanır uyanmaz üretilebiliyor.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">İki bilinen sınır</div><div style="line-height:1.6;color:inherit"><strong>1.</strong> Damgasız kayıtlar uykudan sonra düşer: saat hiç öğrenilmemişse tampondaki kayıtlar zaten hiçbir yeniden başlatmadan sağ çıkamıyordu (bkz. zaman damgası bölümü); uyku bu durumu yaratmıyor, kesinleştiriyor. Saat gelmeden uyumamak ise garajda unutulan aracın aküsünü bitirirdi.<br><br><strong>2.</strong> Uykuda USB konsolu kapanır. Masadaki geliştirme cihazında özellik bu yüzden kapalı tutulmalı; uyuyakalan cihaz "bozuldu" sanılıyor.</div></div>
