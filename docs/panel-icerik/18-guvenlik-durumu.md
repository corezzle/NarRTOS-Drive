**Kısaca:** bu bölüm neyin korunduğunu değil, **şu an neyin korunmadığını** anlatıyor. Cihaz geliştirme aşamasında ve güvenlik katmanı henüz yazılmadı. Sahaya çıkmadan kapatılması gerekenler aşağıda.

## Kanallar

| Kanal | Şifreleme | Kimlik doğrulama | Risk |
|---|---|---|---|
| USB konsolu | yok | yok | Fiziksel erişim gerekiyor; cihaz araç içinde |
| Rapor gönderimi | yok | yok | Konum ve araç verisi açık metin akıyor |
| Uzaktan komut | yok | yok | **En ciddi açık.** Aşağıya bakın |
| Yazılım güncelleme | yok | yok | İmzasız yazılım indirme |
| Cihazdaki ayarlar | yok | geçerli değil | Bellek okunursa şifreler açık metin |

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Uzaktan komut kanalı korumasız</div><div style="line-height:1.6;color:inherit">Mesajlaşma sunucusuna bağlanabilen <strong>herkes</strong> cihaza komut gönderebiliyor. Kanal adında IMEI olması adresleme sağlıyor, güvenlik sağlamıyor.<br><br>Kötü niyetli biri cihazı fabrika ayarına döndürebilir, raporları başka bir sunucuya yönlendirebilir ya da sürekli yeniden başlatarak takibi kesebilir. Kimlik doğrulama eklenene kadar bu kanal yalnız güvenilir bir ağda kullanılmalı.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Yazılım güncellemesi imzasız</div><div style="line-height:1.6;color:inherit">Yazılım şifresiz bir bağlantı üzerinden, imza kontrolü olmadan indiriliyor. Araya girebilen biri kendi yazılımını yükletebilir.<br><br>Geri alma mekanizması bunu <strong>engellemiyor</strong>: geri alma açılışta çöken yazılıma karşı koruma sağlıyor, kötü niyetli ama çalışan bir yazılım kendini sağlam ilan eder. Gereken şifreli bağlantı <em>ve</em> imza doğrulaması.</div></div>

## Şu an var olan korumalar

- **Cihaz aracın veri yoluna yazmıyor.** Yazma yeteneği hiç kurulmuyor, yani cihaz ele geçse bile araca müdahale edilemez. Listedeki en değerli koruma.

- **Şifreler konsola basılmıyor.** Konsol çıktısı loglara ve destek yazışmalarına düşüyor.

- **Geri alma**: açılışta çöken güncellemeye karşı (kötü niyetliye karşı değil).

- **Sürüm düşürme engeli**: sunucu eski sürüm bildirirse kurulmuyor.

## Kapatılması gerekenler

1. **Şifreli bağlantı**: hem rapor gönderimi hem güncelleme için

2. **Sunucu kimlik doğrulaması**: cihazda alanlar hazır, boş duruyor

3. **Yazılım imzası**

4. **Ayarların şifrelenmesi**

5. **Komut yetkilendirmesi**: en azından yıkıcı komutların uzaktan kapatılabilmesi

İlk ikisi sıradaki iş olarak planlandı.
