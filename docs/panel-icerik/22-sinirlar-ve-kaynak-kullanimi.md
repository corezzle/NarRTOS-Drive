Hepsi tek bir yapılandırma dosyasında tanımlı ve derleme sırasında değiştirilebiliyor.

## Veri sınırları

| Sınır | Değer | Aşılırsa |
|---|---|---|
| Aynı anda izlenen araç sinyali | 96 | Yeni sinyal atılır ve sayılır |
| Rapor başına araç mesajı | 64 | En güncel 64 tanesi gider |
| Tampon kapasitesi | 624 rapor (~10 saat) | En eski rapor silinir ve sayılır |
| Rapor boyutu (sıkıştırılmış) | en çok 933 bayt (WiFi listesi dahil) | sınır yok |
| Rapor boyutu (metin) | ~2.900 bayt | 3.456 baytlık alana sığıyor (ölçüldü) |
| Aynı anda gönderimde olan rapor | 1 | Sıra korunur; birikmiş tampon yavaş boşalır |
| Onay bekleme süresi | 15 saniye | Rapor korunur, yeniden denenir |
| Komut satırı uzunluğu | 256 bayt (USB) / 192 (uzaktan) | Satır atılır |

## Süreler

| Ne | Süre |
|---|---|
| Rapor sıklığı (çalışıyor / park) | 1 dakika / 10 dakika |
| Bağlantı yeniden deneme | 5 saniye → 5 dakika (giderek uzuyor) |
| Konum tazelik sınırı | 10 saniye |
| Güncelleme kontrolü | 6 saat |
| Yeni yazılımın sağlamlık süresi | 5 dakika |
| Takılan işi yakalayan sayaç | 10 saniye |

## Bellek

| Kaynak | Kullanılan | Toplam |
|---|---|---|
| Çalışma belleği (RAM) | 58 KB | 320 KB (%18) |
| Yazılım boyutu | 952 KB | 2,8 MB (%33) |

Ek bellek yongası (PSRAM) **kullanılmıyor**: ekran olmadığı için gerekmiyor ve tampon kalıcı bellekte duruyor.
