Bir nakliye şirketi düşünün. Yüz kamyonu var ve şu soruların cevabını sürekli bilmek istiyor:

- Hangi kamyon şu an nerede?

- Hangi sürücü ne zaman işe başladı, ne zaman bitirdi?

- Hangi araç boşta rölantide bekleyip yakıt yakıyor?

- Motorda arıza var mı?

Bu cihaz her aracın içine takılıyor ve bu soruların cevabını toplayıp merkeze gönderiyor. Merkezdeki yazılım (buna **filo sistemi** diyoruz) verileri bir haritada ve raporlarda gösteriyor.

![Uçtan uca veri akışı: araç, cihaz, sunucu](GORSEL: veri-akisi.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Uçtan uca akış: cihaz veriyi toplayıp iletir, çözüm sunucudadır.</em></div>

## Cihaz veriyi nereden alıyor?

| Kaynak | Ne öğreniyor |
|---|---|
| Uydu alıcısı (GNSS) | Konum, hız, gidiş yönü |
| Aracın kendi veri yolu (CAN) | Motor devri, yakıt, kilometre, arıza kodları. Aracın kendi bilgisayarlarının konuştuğu hat |
| Kontak kablosu | Araç çalışıyor mu, park hâlinde mi |
| Hücresel modem | Veriyi merkeze göndermek için (telefon gibi SIM kartı var) |

## Bir günün öyküsü

Cihazın sıradan bir iş gününde yaptıklarını sırayla izleyelim. Belgedeki bütün teknik ayrıntılar bu akışın bir parçasını anlatıyor:

| Saat | Olan | Cihaz ne yapıyor |
|---|---|---|
| 07:00 | Sürücü kontağı açıyor | Kontak değişimini **anında** bildiriyor, çünkü mesai başlangıcı raporu buna dayanıyor |
| 07:01 | Araç kapalı otoparkta | Uydu ve şebeke yok. Veri toplamaya devam edip **kendi belleğine yazıyor** |
| 07:20 | Açık havaya çıkıyor | Uydu ve şebeke geliyor. Biriken 20 dakikalık veri, **üretildiği saatlerin damgasıyla** gönderiliyor |
| 09:30 | Tünelden geçiyor | Şebeke kesiliyor, veri yine belleğe yazılıyor; tünel çıkışında gönderiliyor |
| 12:00 | Mola, kontak kapalı | Rapor sıklığı dakikada birden 10 dakikada bire düşüyor, boşa veri harcamamak için |
| 18:00 | Araç garajda, kontak kapalı | Yazılım güncellemesi varsa **şimdi** kuruluyor. Yol ortasında yapılmıyor |

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Bu cihazın diğerlerinden farkı ne?</div><div style="line-height:1.6;color:inherit">Üç şey: <strong>şebeke yokken veri kaybetmiyor</strong>, <strong>saatini bilmese bile doğru zaman damgası üretiyor</strong>, ve <strong>bozuk bir güncelleme kendini geri alıyor</strong>. Üçünün de sebebi aynı: cihaz aracın altında ve kimse ona gidip müdahale edemiyor.</div></div>

<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px;margin:18px 0"><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Veri</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Araç verisini sunucu çözüyor</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Cihaz aracın hattından okuduğu veriyi işlemeden iletiyor. Anlamlandırma merkezde yapılıyor, böylece yeni bir sinyal eklenince araçlara gitmek gerekmiyor.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Dayanıklılık</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Şebeke yokken veri kaybolmuyor</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Tünelde, kapalı otoparkta, kırsalda üretilen raporlar cihazın belleğine yazılıyor ve bağlantı dönünce sırayla gönderiliyor.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Bakım</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Güncelleme geri alınabiliyor</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Açılışta çöken bir yazılım kendiliğinden eski sürüme dönüyor. Aracın altındaki cihaza kimse gidip müdahale edemez.</div></div><div style="background:rgba(128,128,128,.08);border:1px solid rgba(128,128,128,.24);border-radius:14px;padding:20px"><div style="font-size:11.5px;font-weight:800;letter-spacing:1.2px;text-transform:uppercase;color:#12A0AD">Teşhis</div><div style="font-size:17px;font-weight:700;margin:8px 0 6px;color:inherit">Ekransız ama sorgulanabilir</div><div style="font-size:14px;color:inherit;opacity:.72;line-height:1.5">Kablo, uzaktan mesaj ve tek bir LED. Üç kanal, üçü de "neden çalışmıyor" sorusunu cevaplamak için.</div></div></div>
