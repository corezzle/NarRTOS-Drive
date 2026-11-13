**Kısaca:** aracın kendi bilgisayarları saniyede binlerce mesaj alışverişi yapıyor. Cihaz bunları dinliyor ama *anlamlandırmıyor*. Ham hâliyle sunucuya iletiyor. Sunucu 5,7 MB'lık bir tanım dosyasıyla "bu mesaj motor devri, şu yakıt seviyesi" diye çözüyor.

## Neden cihaz çözmüyor?

Üç sebep:

- **Tanım dosyası cihaza sığmıyor.** 5,7 MB, 47 bin satır. Cihazın toplam belleği 8 MB.

- **Tanımlar değişiyor.** Yeni bir araç modeli ya da yeni bir sinyal eklendiğinde sunucudaki dosya güncelleniyor. Cihazda olsaydı sahadaki her araca gitmek gerekirdi.

- **Ham veri hiçbir şey kaybetmiyor.** Cihaz süzme yapsaydı, sonradan ihtiyaç duyulan bir sinyal geri getirilemezdi.

Cihazın gönderdiği biçim şöyle: mesajın kimlik numarası, ardından veri baytları.

```
"0x18FEE500,01,02,03,04,05,06,07,08"
```

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Kimlik tam 8 haneli yazılmak zorunda</div><div style="line-height:1.6;color:inherit">Sunucu bu metnin belirli karakterlerini keserek tanım dosyasındaki karşılığını buluyor. Hane sayısı farklı olursa <strong>yanlış sinyale eşleşiyor ve hiçbir hata vermiyor</strong>. Sunucu yanlış veriyi doğru sanıyor. Bu yüzden kısa kimlikler başa sıfır eklenerek yazılıyor: <code>0x00000123</code>, <code>0x123</code> değil.</div></div>

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Mesajın öncelik bilgisi korunmak zorunda</div><div style="line-height:1.6;color:inherit">CAN kimliğinin ilk haneleri mesajın aciliyetini belirtiyor. Sunucudaki eşleştirme buna da duyarlı: aynı sinyal farklı öncelikle gönderilirse tanım dosyasında karşılığı bulunmuyor ve sessizce kaybediliyor. Cihaz bu yüzden kimliği <strong>hiç değiştirmeden</strong> iletiyor.</div></div>

## Neden hepsini değil, her sinyalin son hâlini saklıyor?

Araç hattında her sinyal düzenli olarak tekrar ediyor, bazıları saniyede yüz kez. Cihaz gelen her mesajı sıraya dizip biriktirseydi, bellek saniyeler içinde dolar ve rapora hattın rastgele bir dilimi giderdi.

Oysa sunucunun istediği şey her sinyalin **güncel değeri**, geçmişi değil. Bu yüzden cihaz her sinyal kimliği için yalnız *en son* mesajı tutuyor. Tablo 96 kimlik alıyor; tipik bir araç 30 ile 80 arasında farklı sinyal yayınlıyor, yani rahat bir pay var.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Tablo dolarsa yeni sinyal atılıyor, mevcutlar korunuyor</div><div style="line-height:1.6;color:inherit">Tersi olsaydı, nadiren gelen ama önemli bir sinyal (örneğin arıza kodu) sürekli gelen bir sinyal uğruna düşerdi. Atılan sinyaller sayılıyor; <code>can stats</code> komutuyla görülüyor. Sayı artıyorsa tablo büyütülmeli.</div></div>
