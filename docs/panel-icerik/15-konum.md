**Kısaca:** uydu alıcısı saniyede bir, metin satırları hâlinde konum bildiriyor. Cihaz bu satırları okuyup enlem, boylam, hız, yön ve uydu sayısını çıkarıyor. Alıcı aynı zamanda ikinci saat kaynağı.

## Alıcı ne gönderiyor?

Konum alıcıları standart bir metin biçimi kullanıyor (NMEA). Her satır bir bilgi taşıyor; cihaz ikisiyle ilgileniyor:

| Satır | İçindekiler |
|---|---|
| **RMC** | Konum, hız, yön, *tarih ve saat*, geçerlilik |
| **GGA** | Konum, sabitleme kalitesi, *uydu sayısı* |

İkisi de gerekli: RMC'de uydu sayısı yok, GGA'da tarih yok (yalnız saat var, dolayısıyla ondan damga üretilemez).

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Bozuk satır tamamen atılıyor</div><div style="line-height:1.6;color:inherit">Her satırın sonunda bir doğrulama kodu var. Kablo üzerinde tek bir baytın bozulması <code>4807.038</code> yerine <code>4907.038</code> okutabilir, yani <strong>111 kilometrelik sıçrama</strong>. Sunucu tarafında bu hem hız alarmı üretir hem güzergâhı bozar, üstelik sunucunun bunu ayıklama şansı yoktur. Doğrulama kodu tutmayan satırdan <em>hiçbir</em> bilgi alınmıyor.</div></div>

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Eski konum, konum yokluğundan kötüdür</div><div style="line-height:1.6;color:inherit">Alıcı tünelde uyduları kaybettiğinde son bilinen konumu tutmaya devam etmek cazip görünüyor ama yanlış: hareket eden bir araçta 10 dakikalık eski nokta "şu anki konum" olarak gidiyor ve sunucu bunu sorgulamıyor. Sonuç, haritada duran ama gerçekte yol alan bir araç.<br><br>Bu yüzden <strong>10 saniyeden eski sabitleme geçersiz</strong> sayılıyor ve rapora "konum yok" bilgisi gidiyor.</div></div>

## Boş alan sıfır demek değil

Sabitleme henüz yokken alıcı alanları boş bırakıyor. Boşu sıfır olarak okumak aracı Gine Körfezi'ne (0 enlem, 0 boylam) koymak olurdu ve sunucu bunu geçerli bir konum sanardı. Cihaz "bu alan yok" ile "bu alan sıfır" arasındaki farkı ayrı tutuyor.

## Kapalı alanda konum: uydu kesilince tahminle sürdürme

Uydu görünürken konum doğrudan uydudan alınıyor. Tünelde ya da kapalı otoparkta uydu kesildiğinde cihaz son bilinen uydu noktasından başlayıp konumu **tahmin ederek sürdürüyor**: ne kadar yol alındığı aracın kendi hız verisinden, ne kadar dönüldüğü jiroskoptan okunuyor. Bu yönteme "ölü hesap" deniyor ve ticari takip cihazlarının tünel çözümü budur. Uydu geri geldiğinde tahmin bırakılıyor ve bir sonraki kesinti için o taze noktadan yeniden başlanıyor; biriken hata her seferinde sıfırlanıyor.

![Tünelde ölü hesap: büyüyen hata payı ve yeniden tohumlama](GORSEL: tunel-olu-hesap.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Tünel geçişi: konum hız + jiroskopla yürütülür, hata payı halka olarak büyür, uydu dönünce sıfırlanır.</em></div>

| Kaynak | Ne veriyor | Durum |
|---|---|---|
| Aracın hız verisi (CAN) | Ne kadar yol alındığı | Uygulandı. Tek sinyal çözülüyor: tekerlek hızı |
| Jiroskop (ICM-42688-P) | Ne kadar dönüldüğü | Sürücü hazır, sensör henüz elde yok |
| WiFi ağ taraması | Kaba konum doğrulaması | Uygulandı (aşağıda ayrı bölüm) |

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Neden yalnız ivmeölçer yetmiyor</div><div style="line-height:1.6;color:inherit">İvmeölçerden konum çıkarmak için ivmeyi iki kez toplamak gerekiyor ve sensörün küçük sapması bu işlemde <strong>zamanın karesiyle büyüyor</strong>. Yarım dakikada hata onlarca metreye çıkıyor, birkaç dakikada kullanılamaz hâle geliyor.<br><br>Bu yüzden mesafe bilgisi <strong>aracın kendi hız verisinden</strong> alınıyor: o ölçümün hatası mesafeyle doğrusal büyüyor ve yüzde birkaç düzeyinde kalıyor. Jiroskop yalnız yön değişimi için kullanılıyor.</div></div>

Tahmin ölçüm değildir ve cihaz bunu saklamıyor:

- **Hata payı takip ediliyor ve raporlanıyor.** Her tahmin adımında hata yarıçapı büyütülüyor ve rapora yazılıyor.

- **Aşırı sapmış tahmin gönderilmiyor.** Hata yarıçapı 500 metreyi ya da süre 10 dakikayı aşarsa rapora "konum yok" bilgisi gidiyor. "Eski konum, konum yokluğundan kötüdür" kuralının tahmine uygulanmış hâli.

- **Hız verisi yoksa tahmin hiç başlamıyor.** Jiroskop tek başına konum üretemez.

- **Jiroskop sapması araç dururken ölçülüyor** ve okumadan düşülüyor. Sapma düzeltilmezse duran araçta bile yön kayar ve tünele girildiği anda tahmin yanlış yöne gider. Jiroskop hiç yoksa yön sabit varsayılıyor ve hata payı daha hızlı büyüyor.

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Tahmin, ölçümden ayırt edilebilir gidiyor</div><div style="line-height:1.6;color:inherit">Rapora iki alan eklendi: <code>positionSource</code> (uydu için <code>"gnss"</code>, tahmin için <code>"dr"</code>, konum yoksa <code>"none"</code>) ve metre cinsinden hata yarıçapı <code>positionAccuracy</code>. Bu alanlar telemetri dokümanında henüz yok; <strong>sunucu tarafıyla mutabakat gerekiyor</strong>. Sunucu bu ayrımı kaydetmezse tahmini konum uydu konumu gibi işlenir ve güzergâh doğruluğu sessizce bozulur.</div></div>

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">"Araç verisini cihaz çözmez" kuralına sınırlı istisna</div><div style="line-height:1.6;color:inherit">Yol bilgisi için cihaz araç hattından tek bir sinyali (tekerlek hızı) kendisi çözüyor. Ham veri yine değiştirilmeden sunucuya gidiyor; istisna bu tek sinyalle sınırlı.</div></div>

Durum `pos status` komutuyla izleniyor: konum kaynağı, hata yarıçapı, tahminle gidilen yol, jiroskop sapması ve hız verisinin gelip gelmediği. Bu bölümün matematiği bilgisayarda testlerle sınandı; jiroskop ve CAN ara yongası henüz elde olmadığı için araçta **doğrulanmadı**.

## Kapalı alanda ek kanıt: WiFi listesi

Tünelin çözümü yukarıdaki tahmin; uydu görmeyen diğer kapalı alanlarda (otopark, AVM, bina içi) ise bir kanıt daha var. Konum uydudan gelmediği sürece cihaz 30 saniyede bir çevredeki WiFi ağlarını tarıyor ve en güçlü 8 ağın kimliğini (MAC) + sinyal gücünü rapora ekliyor. **Konumu cihaz çözmüyor**: araç verisindeki kalıbın aynısı, ham liste sunucuya gidiyor ve konum orada, bilinen ağ konumları veritabanından çözülüyor.

![WiFi listesiyle kapalı alan konumlaması](GORSEL: wifi-konumlama.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Kapalı alanda WiFi listesi rapora eklenir; konumu sunucu çözer, cihazın internete ihtiyacı olmaz.</em></div>

- **Cihazın internete ihtiyacı yok.** Liste raporla birlikte tamponda bekliyor; şebekesiz kapalı alanda üretilen rapor sonradan gönderildiğinde konum geriye dönük çözülüyor.

- **Aile içinde hazır altyapı.** Saat ürünü konumu aynı yöntemle buluyor ve sunucu tarafındaki çözücü servis yazılmış durumda (varsayılan beaconDB, tek ayarla Google'a geçiyor). Drive için yeniden yazılması gerekmiyor.

<div style="border-left:4px solid #12A0AD;background:rgba(18,160,173,.10);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Bayat liste rapora girmiyor</div><div style="line-height:1.6;color:inherit">Son taramanın üzerinden 2 dakikadan fazla geçtiyse liste rapora eklenmiyor. Araç ilerlemişken iki dakika önce çıkılan kapalı alanın ağlarını "şu an buradayım" diye göndermek sunucuyu yanıltırdı. Bayat sabitleme kuralının bir uygulaması daha.</div></div>

Sınırı da açık: tünelde WiFi yok, orada bu listenin katkısı olmaz. Baz istasyonu bilgisiyle kaba doğrulama modem donanımı gelince değerlendirilecek. Tarama `wifi scan` komutuyla elle de çalıştırılıyor; modül olmayan ortamda `wifi inject` ile liste beslenip kayıt yolu uçtan uca sürülebiliyor.

## İki haneli yıl sorunu

Alıcı yılı iki haneli gönderiyor: `26`. Bu 2026 mı 1926 mı? "80'den büyükse 19xx" gibi bir varsayım kurmadık, çünkü arızalı bir alıcı yaklaşık 19,6 yıl geçmişte bir tarih üretebiliyor ve böyle bir varsayım o hatayı **makul bir tarihe çevirip gizlerdi**.

Bunun yerine çözücü dürüst kalıyor (2000'i ekliyor) ve makullük kontrolü saati kuran yerde yapılıyor: 2020 öncesi her tarih reddediliyor ve reddedilenler sayılıyor.
