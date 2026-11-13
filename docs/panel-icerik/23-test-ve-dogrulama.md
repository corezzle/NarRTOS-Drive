**Kısaca:** cihazın donanımı büyük ölçüde elimizde yok. Buna rağmen yazılımın büyük kısmı doğrulanabiliyor, çünkü kritik parçalar donanım bilmeyecek şekilde yazıldı. Geliştirme bilgisayarında çalıştırılıp sınanıyorlar.

## Otomatik testler

```
tools/host-test/run.sh
```

Test edilen kod, cihaza yüklenen kodun **aynısı**, kopyalanmış bir taklit değil.

| Ne test ediliyor | Kontrol | Neyi kanıtlıyor |
|---|---|---|
| Araç verisi tablosu | 22 | Sinyal birleştirme, tablo dolması |
| Tampon | 67 | Elektrik kesintisi, bellek kuralları |
| Rapor biçimi | 47 | Alan adları, zaman damgası hesabı, WiFi kuyruğu |
| Konum çözücü | 41 | Koordinat dönüşümü, bozuk satır |
| Güncelleme bildirimi | 28 | Sürüm karşılaştırması |
| Modem yanıtları | 37 | Komut akışı, saat dilimi dönüşümü |
| Araç hız sinyali | 12 | Tekerlek hızının CAN'den çözümü |
| Ölü hesap | 20 | Konum yürütme, belirsizlik büyümesi, sınırlar |
| Uyku kararı | 16 | Uyku/uyanıklık politikasının iki yönü |
| **Toplam** | **290** |  |

Testlerde kalıcı bellek taklit ediliyor ve **gerçek belleğin kuralları zorlanıyor**: yanlış bir yazma girişimi ihlal olarak sayılıyor. Elektrik kesintisi de taklit edilebiliyor: yazma ortasında "elektrik kesiliyor" ve sonrasında verinin ne olduğu kontrol ediliyor.

## Cihaz üstü self-test (`selftest` komutu)

Host testleri yazılımın **mantığını** doğruluyor; `selftest` komutu ise **gerçek donanımı** cihazın üstünde sınıyor. Seri konsola `selftest` (ya da `test`) yazınca bir checklist çalışır ve her satır PASS / FAIL / UYARI / ATLA olarak döner.

| Satır | Ne sınar |
|---|---|
| Kart bilgisi | Yonga, MAC, sürüm, boş bellek |
| Bellek | Flash / PSRAM / boş heap |
| Store & forward | `buffer` bölümü, slot sayısı, bozuk kayıt |
| IMU · CAN · GNSS · Modem | İlgili çevrebirim (donanımı/derlemesi yoksa ATLA) |
| Wi-Fi tarama | Çevredeki ağ sayısı ve en iyi sinyal |
| Kontak · Durum LED | Kontak girişi; LED'i kısa süre yakar |

Test **açılışta değil, komutla** çalışır — araçtaki çalışmayı hiç etkilemez. Yalnız salt-okunur durum sorguları ve zaten var olan güvenli eylemler (Wi-Fi taraması, LED testi) kullanılır; çalışan hiçbir servis durdurulmaz. Donanımı henüz olmayan çevrebirimler **ATLA** görünür — çerçeve hazırdır, kart gelince kendiliğinden koşar.

## Gerçek cihazda doğrulanan / doğrulanmayan

| Yol | Durum |
|---|---|
| USB konfigürasyonu, ayarların kalıcılığı | doğrulandı |
| WiFi, saat, rapor gönderimi ve onay | doğrulandı |
| Tampon ve zaman damgası hesabı | doğrulandı |
| Uzaktan komut | doğrulandı |
| Araç hattı (CAN) | ara yonga yok |
| Uydu alıcısı | modül yok |
| Modem | modem yok |
| Güncelleme geri alma | sunucu yok |
| Jiroskop (ölü hesap) | sensör yok; matematik bilgisayarda test edildi |
| WiFi taraması | kart takılınca masada doğrulanabilir, bekliyor |
| Derin uyku ve uyanma | masa doğrulaması bekliyor |

## Uçtan uca doğrulama

Cihazın gerçekten gönderdiği bir rapor, filo sisteminin kendi çözücüsünden geçirildi: **30 araç sinyali** doğru şekilde çözüldü (motor devri, çalışma saati, araç hızı dahil), zaman damgası geçerli bir tarihe dönüştü ve "bilinmiyor" değerleri sunucunun beklediği gibi okundu.
