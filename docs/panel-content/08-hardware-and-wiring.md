<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Pin haritası geçici</div><div style="line-height:1.6;color:inherit">Drive'ın kendi elektronik kartı henüz üretilmedi. Aşağıdaki bacak numaraları geliştirme kartında çakışmayan pinlerden seçildi. Gerçek kart gelince <strong>tek dosyadan</strong> düzeltilecek; kodun geri kalanı değişmeyecek.</div></div>

![Araca bağlantı şeması: besleme, kontak, CAN, antenler](GORSEL: vehicle-wiring.svg)

<div style="font-size:12.5px;color:inherit;opacity:.72;text-align:center;margin:-6px 0 14px"><em>Araca bağlantı şeması. Pin haritası geçicidir; kesin bağlantı Drive'ın kendi kartıyla netleşecek.</em></div>

## Bağlantı noktaları

"GPIO" işlemcinin dışa açılan bacaklarına verilen ad; her biri numaralı.

| İşlev | GPIO | Yön | Not |
|---|---|---|---|
| CAN veri çıkışı | 4 | çıkış | Ara yongaya bağlanır (cihaz hatta yazmasa da pin bağlanır) |
| CAN veri girişi | 5 | giriş | Ara yongadan gelir |
| CAN uyku hattı | yok | yok | Kullanılmıyor |
| Uydu alıcısı girişi | 8 | giriş | Konum verisi buradan gelir |
| Uydu alıcısı çıkışı | 9 | çıkış | Kullanılmıyor. Alıcıya komut gönderilmiyor |
| Modem çıkışı | 17 | çıkış | Modemin giriş bacağına |
| Modem girişi | 18 | giriş | Modemin çıkış bacağından |
| Modem güç düğmesi | 15 | çıkış | Modemi açmak için 1,2 saniyelik darbe |
| Kontak girişi | 10 | giriş | Aracın kontak hattından, gerilim düşürücü üzerinden |
| Durum LED'i | 48 | çıkış | Bağlanmazsa servis hiç çalışmıyor |

## Seri hatların dağılımı

İşlemcinin üç seri haberleşme birimi var; her biri bir iş için ayrılmış:

| Birim | Kullanım | Hız |
|---|---|---|
| UART0 | Bilgisayar bağlantısı ve komutlar | 115200 |
| UART1 | Uydu alıcısı | 9600 (ayarlanabilir) |
| UART2 | Modem | 115200 |

<div style="border-left:4px solid #D95757;background:rgba(217,87,87,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">CAN için ara yonga zorunlu</div><div style="line-height:1.6;color:inherit">İşlemcinin içinde CAN denetleyicisi var ama <strong>elektriksel arayüzü yok</strong>. Bacakları doğrudan aracın CAN kablosuna bağlamak ne işe yarar ne de güvenlidir. Hem hattı bozar hem işlemciyi. Arada bir <em>transceiver</em> (SN65HVD230, TJA1050 gibi) olmak zorunda; hat uçlarında da 120 ohm'luk sonlandırma direnci.</div></div>

## Kontak girişi

Araç kontağı 12 volt, işlemcinin girişi 3,3 volt. Arada gerilim düşürücü ve koruma devresi şart.

Kontak bilgisi üç yerde kullanılıyor: **rapor sıklığı** (açıkken dakikada bir, park hâlinde 10 dakikada bir), **anında rapor** (kontak değişince beklemeden), ve **güncelleme zamanı** (yalnız park hâlindeyken). Bu kablo bağlanmazsa cihaz aracı sürekli park hâlinde sanar.

## Besleme

Cihazın aracın sürekli 12 volt hattına bağlanması öngörülüyor; kontak bilgisi ayrı bir giriş olarak okunuyor. Ters bağlama, aşırı gerilim ve motor çalıştırma anındaki gerilim darbelerine karşı koruma kart tasarımının işi.

<div style="border-left:4px solid #D9922F;background:rgba(217,146,47,.12);border-radius:0 10px 10px 0;padding:14px 18px;margin:18px 0"><div style="font-weight:800;margin-bottom:6px;color:inherit">Elektrik tüketimi ölçülmedi</div><div style="line-height:1.6;color:inherit">Donanım olmadığı için tüketim hiç ölçülmedi ve <strong>güç yönetimi henüz yazılmadı</strong>: kontak kapalıyken cihaz tam güçte çalışmaya devam ediyor, modem açık kalıyor. Uzun süre park eden bir araçta bu aküyü tüketir. Yapılacaklar listesinde.</div></div>
