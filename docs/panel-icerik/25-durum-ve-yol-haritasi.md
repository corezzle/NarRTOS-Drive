| Aşama | Kapsam | Durum |
|---|---|---|
| 1 | Proje iskeleti, bellek bölümleri | tamam |
| 2 | Ayar deposu, komut arayüzü | tamam |
| 3 | Araç verisi toplama | tamam |
| 4 | Tampon, ağ, rapor gönderimi | tamam |
| 5 | Rapor biçimi, saat | tamam |
| 6 | Konum, durum LED'i | tamam |
| 7 | Kablosuz güncelleme (geri alma korumalı) | tamam |
| 8 | Modem | kod tamam, donanım bekliyor |
| 9 | Kapalı alanda konum (ölü hesap) | kod tamam, donanım bekliyor |
| 10 | WiFi tarama (kapalı alan konumlama) | kod tamam, araçta denenmedi |
| 11 | Güç yönetimi (kontak kapanınca derin uyku) | kod tamam, araçta denenmedi |

## Sıradaki işler

1. **Şifreli bağlantı.** Rapor gönderimi ve güncelleme şu an şifresiz. Güncellemede daha ciddi: imzasız bir kanal, araya girenin kendi yazılımını yükletmesine açık.

2. **Donanım doğrulama turu.** Araç hattı, uydu alıcısı, modem ve jiroskop yollarının dördü de yalnız derleniyor; gerçek donanımla sınanmadı.

## Bizim elimizde olmayanlar

- Sunucu tarafıyla anlaşma: adres, kanal adları, kimlik doğrulama

- Rapordaki `positionSource`, `positionAccuracy` ve `wifi` alanlarının telemetri dokümanına eklenmesi

- Bu ürün için ayrı bir güncelleme adresi

- Mesaj öncelik bilgisi bulgusunun sunucu ekibine iletilmesi
