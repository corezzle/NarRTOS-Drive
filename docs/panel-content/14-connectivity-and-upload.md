**Kısaca:** cihaz iki farklı yolla internete çıkabiliyor: sahada hücresel modem, atölyede WiFi. Üst katman hangisinin kullanıldığını bilmiyor, bu yüzden aynı yazılım her iki durumda da çalışıyor.

## İki bağlantı yolu, tek arayüz

| Yol | Nerede |
|---|---|
| Hücresel modem | Sahada, araç içinde. Asıl yol |
| WiFi | Atölyede, geliştirme sırasında |

Seçim yazılımı yeniden derlemekle değil, **ayarla** yapılıyor. Yani aynı yazılım hem atölyede hem araçta çalışıyor; iki ayrı sürüm üretmek gerekmiyor.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Yeniden deneme süresi giderek uzuyor</div><div style="line-height:1.6;color:inherit">Bağlantı kurulamazsa cihaz 5 saniye sonra tekrar deniyor; olmazsa 10, sonra 20… en fazla 5 dakikaya kadar. Sabit aralık araçta yanlış olurdu: tünelde ya da kırsalda saatlerce şebeke olmayabiliyor ve saniyede bir denemek hem elektrik hem veri harcıyor.<br><br>Bu mantık bağlantı yollarının içinde değil, üstünde duruyor. Böylece WiFi ve modem aynı davranışı paylaşıyor, ikinci kez yazılmıyor.</div></div>

## Uzaktan komut gönderme

Cihaz gönderdiği raporların yanı sıra, kendisine ayrılmış bir kanalı da dinliyor. Buraya gönderilen komutlar USB'den yazılmış gibi işleniyor ve yanıt geri dönüyor.

Ekransız bir araç cihazında bu, sahada tek müdahale imkânı olabilir: araç başka şehirde, cihaz aracın altında, kimse ona ulaşamıyor.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Sunucu tarafıyla anlaşma henüz kesinleşmedi</div><div style="line-height:1.6;color:inherit">Verinin hangi adrese, hangi kanal adlarıyla ve nasıl bir kimlik doğrulamasıyla gönderileceği sunucu ekibiyle netleştirilmedi. Cihaz şu an kendi varsayımlarıyla çalışıyor. Bütün varsayımlar <strong>tek bir dosyada</strong> toplandı; anlaşma netleşince yalnız o dosya değişecek, servis kodu değişmeyecek. Ayrıntı Sunucu Tarafı bölümünde.</div></div>
