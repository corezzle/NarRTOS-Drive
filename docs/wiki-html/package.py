# -*- coding: utf-8 -*-
"""Drive wiki'sini tek dosyalık, kendi kendine yeten HTML'e paketler.

`index.html` tek başına gönderilemez: CSS ve JS göreli yolla bağlı, dosyayı
tek başına alan kişi stilsiz bir metin görür ve içindekiler menüsü hiç
oluşmaz. Bu betik ikisini de belgeye gömer.

Wear'ın package.py'sinden uyarlandı. Drive'da alt sayfa ve görsel olmadığı
için base64 gömme ve sayfa birleştirme adımları çıkarıldı.

Kullanım:  python3 docs/wiki-html/package.py
Çıktı:     docs/NarRTOS-Drive-Wiki.html   (çift tıklayınca her yerde açılır)
"""
import base64
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "NarRTOS-Drive-Wiki.html"


def main():
    src = (HERE / "index.html").read_text(encoding="utf-8")
    css = (HERE / "assets/css/wiki.css").read_text(encoding="utf-8")
    js = (HERE / "assets/js/wiki.js").read_text(encoding="utf-8")

    # Gömme, DEĞİŞTİRME METNİ OLARAK DEĞİL GERİ ÇAĞRIYLA yapılıyor.
    #
    # re.sub'ın replacement argümanı kaçış dizilerini işler: metindeki `\n`
    # gerçek satır sonuna, `\1` grup referansına çevrilir. wiki.js içinde
    # tek satırlık bir dize sabitinin ortasında `\\n` geçiyor; düz metinle
    # gömdüğümüzde o dize ikiye bölünüyor ve tüm betik
    # "Uncaught SyntaxError" ile ölüyor. Betik ölünce içindekiler menüsü de
    # oluşmuyor; hata sessiz, sayfa açılıyor ama TOC boş kalıyor.
    #
    # Geri çağrı kullanınca kaçış işlenmiyor, içerik olduğu gibi giriyor.
    doc, n_css = re.subn(r'<link rel="stylesheet"[^>]*>',
                         lambda m: f"<style>\n{css}\n</style>", src, count=1)
    doc, n_js = re.subn(r'<script src="[^"]*"></script>',
                        lambda m: f"<script>\n{js}\n</script>", doc, count=1)

    if not n_css or not n_js:
        raise SystemExit("HATA: CSS ya da JS bağlantısı bulunamadı; "
                         "index.html'in <head>/<body> yapısı değişmiş olabilir")

    # Dışarıda kalan göreli bağlantı var mı? (görsel eklenirse fark edilsin)
    kalan = re.findall(r'(?:src|href)="(?!https?:|#|data:|assets/img/)([^"]+)"', doc)
    if kalan:
        print("  ⚠ hâlâ dış dosyaya bağlı:", ", ".join(sorted(set(kalan))))


    # Görseller base64 gömülür: tek dosya, görselleriyle birlikte taşınabilir
    # olmalı. SVG'ler küçük (5-8 KB); gömme dosyayı hissedilir büyütmüyor.
    def _embed_img(m):
        yol = HERE / m.group(1)
        veri = base64.b64encode(yol.read_bytes()).decode()
        tur = ("image/svg+xml" if yol.suffix == ".svg"
               else f"image/{yol.suffix[1:]}")
        return m.group(0).replace(m.group(1), f"data:{tur};base64,{veri}")

    doc = re.sub(r'src="(assets/img/[^"]+)"', _embed_img, doc)

    OUT.write_text(doc, encoding="utf-8")
    kb = OUT.stat().st_size / 1024
    print(f"✓ tek dosya: docs/{OUT.name}  ({kb:.0f} KB)")
    print("  çift tıklayınca açılır; sunucu, internet ya da klasör gerekmez")


if __name__ == "__main__":
    main()
