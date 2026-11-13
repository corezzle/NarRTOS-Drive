## Cihaz bağlanmıyor

| Belirti | Sebep / Çözüm |
|---|---|
| `net status` → "ayarsız" | SIM ya da WiFi ayarı girilmemiş. Ağ donanımı bilinçli olarak hiç açılmıyor |
| Şebeke var ama `mqtt status` → bağlı değil | Sunucu adresi/portu yanlış ya da sunucu kapalı. LED çift flaş atar |
| Sunucu adresini değiştirdim, etkisi yok | Artık anında etki ediyor. Eski sürümlerde yeniden başlatma gerekiyordu |
| Modem takılıyor | `net modem` hangi adımda kaldığını söyler: SIM yok mu, şebeke mi bulunamıyor, ayar mı reddedildi |

## Rapor gitmiyor

| Belirti | Sebep / Çözüm |
|---|---|
| `sf peek` → "saat henüz yok" | Beklenen davranış. Saat gelince kendiliğinden gönderilir |
| `sf peek` → "çözülemiyor" | Önceki açılıştan kalan damgasız rapor. Sırası gelince düşürülür |
| Tampon doluyor | Bağlantıyı ve sunucu erişimini kontrol edin |
| "Silinen" sayacı artıyor | Tampon dolduğu için en eski raporlar düşüyor. Uzun süredir bağlantı yok |

## Konum gelmiyor

| Belirti | Sebep / Çözüm |
|---|---|
| Raporda konum sıfır | Sabitleme yok ya da 10 saniyeden eski. `gnss status` ile uydu sayısına bakın |
| Bozuk satır sayısı artıyor | Kablo gürültülü ya da alıcı hızı yanlış ayarlanmış |
| Uydu görünüyor ama sabitleme yok | Anten gökyüzünü göremiyor; kapalı alanda normal |

## Araç verisi çözülmüyor

| Belirti | Sebep / Çözüm |
|---|---|
| Sunucuda hiç sinyal yok | Kimlik biçimi tam 8 haneli olmalı. `can dump` ile kontrol edin |
| Bazı sinyaller çözülüyor, bazıları değil | Mesaj öncelik bilgisi. Sunucudaki tanım dosyasıyla eşleşmiyor. Sunucu ekibine bildirin |
| `can stats` → gelen mesaj 0 | Ara yonga bağlı değil ya da CAN derleme dışı |
| "Atılan" sayacı artıyor | Araç 96'dan fazla sinyal yayınlıyor; tablo büyütülmeli |

## Geliştiricilere özel iki tuzak

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Derleme ayarı ekledikten sonra kontrol edin</div><div style="line-height:1.6;color:inherit">Derleme sisteminin varsayılan ayar dosyası, üretilmiş ayar dosyası <strong>zaten varsa</strong> yeni eklenen ayarı yok sayıyor. Kod doğru görünüyor, ayar hiç devreye girmiyor. Çözüm: üretilmiş dosyaları silip yeniden ürettirmek ve ayarın gerçekten geçtiğini kontrol etmek.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">İlk derleme boşa düşebiliyor</div><div style="line-height:1.6;color:inherit">Derleme ayarı değiştirdikten sonraki <em>ilk</em> çalıştırma yapılandırma aşamasında hata verebiliyor, ikincide geçiyor. Engelleyici değil, komutu tekrar çalıştırın.</div></div>
