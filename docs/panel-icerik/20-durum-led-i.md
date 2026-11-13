**Kısaca:** ekran yok, buton yok, ses yok. Kablo takılıyken konsol var, bağlantı varken uzaktan komut var. Ama kurulumcu aracın altında ve cihaz henüz ağa girmemişken ikisinden de yararlanamıyor. Tek yerel gösterge bu LED.

![LED desenleri ve anlamları](GORSEL: led-desenleri.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>LED desenleri: ne kadar çok ışık, o kadar kötü. Sağlıklı cihaz 3 saniyede bir tek kırpma yapar.</em></div>

Desen dili tek bir kurala dayanıyor: **ne kadar çok ışık, o kadar kötü.**

| Desen | Anlamı |
|---|---|
| Sürekli yanık | Cihaz işini yapamıyor (belleğinde sorun var) |
| Hızlı yanıp sönme | Ayar eksik |
| Uzun tek flaş / 2 sn | Çalışıyor ama şebeke yok |
| Çift kısa flaş / 2 sn | Şebeke var, sunucuya bağlanamıyor |
| Çok kısa kırpma / 3 sn | Her şey yolunda |
| Hiç yanmıyor | Elektrik yok, cihaz arızalı ya da derin uykuda (uyku açıksa park hâlinde normaldir) |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Sağlıklı durumda da kırpıyor</div><div style="line-height:1.6;color:inherit">Bu sayede "hiç yanmıyor" tek başına arıza sinyali oluyor, üstelik ek bir maliyet olmadan. Sağlıklı durumu "sürekli yanık" yapan tasarımlarda bu ayrım kayboluyor: yanmayan LED ile arızalı cihaz aynı görünüyor.</div></div>

**"Şebeke var ama sunucuya bağlanamıyor" ayrı bir desen**, çünkü kurulumcuyu antene değil sunucu adresine bakmaya yönlendiriyor.

Aynı anda birden çok sorun olabilir; en derindeki gösteriliyor. Ayarı eksik bir cihaz aynı zamanda şebekesizdir, ama "şebeke yok" göstermek kurulumcuyu anten aramaya iterdi. Oysa sorun kendi girmediği ayarda.
