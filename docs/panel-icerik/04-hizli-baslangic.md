Cihazı ilk kez eline alan biri için 15 dakikalık yol haritası. Her adımın sonunda ne görmen gerektiği yazıyor; ayrıntılar ve sorun çözümleri bir sonraki bölümde.

1. **Hazırlık.** Gereken üç şey: bir bilgisayar, bir USB kablosu ve ESP32-S3 kartı. Kartı USB ile bilgisayara tak.

2. **Derleme aracını kur** (bir kez yapılır): terminale `pip install platformio` yaz.

3. **Kodu indir ve yükle:**

```
git clone https://github.com/corezzle/NarRTOS-Drive
cd NarRTOS-Drive
pio run -e esp32s3_drive -t upload
```

İlk seferde birkaç dakika sürer; sonunda **SUCCESS** yazısını görmelisin.

1. **Cihazla konuş:** `pio device monitor` yaz. Cihazın açılış mesajları akar ve en sonda şunu görürsün: *"seri porttan 'help' yazarak komut listesini görebilirsiniz"*.

2. **Dene:** `help` yaz, komut listesi gelsin. Sonra `info` yaz; sürüm ve durum bilgisi gelsin. Bu iki komut çalışıyorsa kurulum tamamdır.

3. **Ayarla ve ilk raporu üret:** bir sonraki bölümdeki "İlk ayarlar" adımlarını izle.
