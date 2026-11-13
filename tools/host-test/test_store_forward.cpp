/**
 * @file test_store_forward.cpp
 * @brief store_forward'ın halka mantığını Mac üzerinde sınar.
 *
 * GERÇEK store_forward.cpp derleniyor. Altındaki flash, NOR semantiğini
 * ZORLAYAN bir sahte (fake_flash.cpp): silinmemiş bölgeye yazma, hizasız
 * erişim ve sınır aşımı ihlal olarak sayılıyor. Testin sonunda ihlal sayısı
 * 0 olmalı — yani kod gerçek flash'ta da geçerli işlemler yapıyor.
 *
 * .cpp DOSYASI include EDİLİYOR (link edilmiyor). Sebebi tek: yeniden başlatma
 * taklidi. Boot'ta halkanın flash'tan doğru kurulması bu servisin en kritik
 * özelliği; onu sınamak için modülün statik durumunu sıfırlamak gerekiyor ve
 * bunun için üretim koduna test kancası eklemek istemedik.
 *
 * Neyi kanıtlıyor:
 *   - peek/pop akışı ve FIFO sırası (güzergâh kronolojik olmak zorunda)
 *   - peek'in pop'suz aynı kaydı vermesi — broker onaylamadan kayıt silinmez
 *   - yük sınırları ve 4'e hizasız uzunlukların bayt bayt korunması
 *   - slot yeniden kullanımında silme disiplini (0 -> 1 yazma yok)
 *   - halka dolduğunda EN ESKİ kaydın ezilmesi, kapasitenin aşılmaması
 *   - yeniden başlatmada bekleyenlerin hayatta kalması, gönderilmişlerin
 *     geri dönmemesi
 *   - halka SARDIKTAN sonra sıralamanın slot indisinden değil seq'ten gelmesi
 *   - ELEKTRİK KESİNTİSİ: yarım yazılan kayıt yok sayılır, öncekiler sağ kalır
 *   - CRC bozulmasında peek'in bozuk kaydı atlayıp kuyruğu tıkamaması
 *   - gerçek `buffer` bölümü geometrisi (624 slot)
 *
 * Çalıştırma:  tools/host-test/run.sh
 */

#include "fake_flash.h"

/* Mutex shim'inin al/bırak dengesini saydığı sayaç. */
int g_mutex_balance = 0;

#include "../../src/services/store_forward.cpp"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/* ── Koşum yardımcıları ─────────────────────────────────────────────────── */

/** Sıfırdan kurulu bir cihaz: boş flash + taze halka. */
static void setup(uint32_t slots)
{
    fake_flash_configure((size_t)slots * SF_SLOT_SIZE);
    s_ready = false;
    store_forward_init();
}

/** Yeniden başlatma: flash içeriği korunur, RAM durumu sıfırdan kurulur.
 *  store_forward_init()'in tek gerçek işi budur — halkayı başlıklardan
 *  yeniden inşa etmek. */
static esp_err_t reboot(void)
{
    fake_flash_power_restore();
    s_ready = false;
    return store_forward_init();
}

static esp_err_t push_str(const char* s)
{
    return store_forward_push(s, strlen(s));
}

/** peek + dizeye çevir. Bulunamazsa out boş kalır. */
static esp_err_t peek_str(char* out, size_t cap, uint32_t* seq)
{
    size_t len = 0;
    out[0] = '\0';
    esp_err_t e = store_forward_peek(out, cap - 1, &len, seq);
    if (e == ESP_OK) out[len] = '\0';
    return e;
}

/* ── 1. Temel akış: FIFO, peek onaysız silmez ───────────────────────────── */
static void test_basic(void)
{
    printf("\n[1] temel akış: push -> peek -> pop\n");
    setup(8);

    check(push_str("A") == ESP_OK, "push A");
    check(push_str("B") == ESP_OK, "push B");
    check(push_str("C") == ESP_OK, "push C");

    char buf[64];
    uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "A") == 0,
          "peek en ESKİ kaydı verir (A)");
    check(seq == 1, "seq 1");

    /* Kritik: broker onaylamadan kayıt kaybolmamalı. */
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "A") == 0,
          "pop olmadan peek aynı kaydı verir (onaysız silinmez)");

    check(store_forward_pop(999) == ESP_ERR_NOT_FOUND,
          "olmayan seq için pop NOT_FOUND");
    check(store_forward_pop(1) == ESP_OK, "pop 1");
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "B") == 0,
          "sıradaki B");

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 2 && st.pushed == 3 && st.sent == 1,
          "sayaçlar: bekleyen 2, yazılan 3, gönderilen 1");
    check(st.capacity == 8, "kapasite 8 slot");

    check(store_forward_pop(2) == ESP_OK && store_forward_pop(3) == ESP_OK,
          "kalanlar pop edildi");
    check(peek_str(buf, sizeof(buf), &seq) == ESP_ERR_NOT_FOUND,
          "boş tamponda peek NOT_FOUND");
}

/* ── 2. Yük sınırları ve hizasız uzunluklar ─────────────────────────────── */
static void test_sizes(void)
{
    printf("\n[2] yük sınırları ve 4'e hizasız uzunluklar\n");
    setup(8);

    check(store_forward_push("x", 0) == ESP_ERR_INVALID_SIZE, "0 bayt reddedilir");
    check(store_forward_push(NULL, 4) == ESP_ERR_INVALID_SIZE, "NULL reddedilir");

    static uint8_t big[SF_PAYLOAD_MAX + 1];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7 + 1);

    check(store_forward_push(big, SF_PAYLOAD_MAX + 1) == ESP_ERR_INVALID_SIZE,
          "SF_PAYLOAD_MAX+1 reddedilir");
    check(store_forward_push(big, SF_PAYLOAD_MAX) == ESP_OK,
          "tam SF_PAYLOAD_MAX kabul edilir");

    static uint8_t out[SF_PAYLOAD_MAX];
    size_t len = 0; uint32_t seq = 0;
    check(store_forward_peek(out, sizeof(out), &len, &seq) == ESP_OK &&
          len == SF_PAYLOAD_MAX && memcmp(out, big, SF_PAYLOAD_MAX) == 0,
          "azami yük bayt bayt geri okundu");
    store_forward_pop(seq);

    /* Yük 4'e hizalanmadığında son 1-3 bayt ayrı bir 4 baytlık yazmayla,
     * 0xFF dolgusuyla gidiyor. Uzunluk başlıkta saklandığı için dolgu geri
     * okumaya karışmamalı. */
    bool hepsi_tam = true;
    for (size_t n = 1; n <= 9; n++) {
        uint8_t p[16];
        for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(0xA0 + i);
        if (store_forward_push(p, n) != ESP_OK) { hepsi_tam = false; break; }
        if (store_forward_peek(out, sizeof(out), &len, &seq) != ESP_OK ||
            len != n || memcmp(out, p, n) != 0) { hepsi_tam = false; break; }
        store_forward_pop(seq);
    }
    check(hepsi_tam, "1-9 bayt uzunluklar bozulmadan döndü (dolgu sızmıyor)");
}

/* ── 3. Slot yeniden kullanımı: silme disiplini ─────────────────────────── */
static void test_reuse(void)
{
    printf("\n[3] slot yeniden kullanımı (halka 5 kez sarıyor)\n");
    setup(4);
    fake_flash_reset_counters();

    bool ok = true;
    for (int i = 0; i < 20; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "rapor-%d", i);
        if (push_str(msg) != ESP_OK) { ok = false; break; }

        char buf[64]; uint32_t seq = 0;
        if (peek_str(buf, sizeof(buf), &seq) != ESP_OK ||
            strcmp(buf, msg) != 0) { ok = false; break; }
        if (store_forward_pop(seq) != ESP_OK) { ok = false; break; }
    }
    check(ok, "20 kayıt 4 slotta sırayla yazıldı ve okundu");

    /* Asıl kanıt bu: gönderilmiş bir slot yeniden kullanılırken siliniyor.
     * Silmeden yazsaydık NOR flash'ta 0 -> 1 geçişi gerekirdi ve sahte flash
     * ihlal sayardı. */
    check(fake_flash_violations() == 0, "NOR ihlali yok — her yazma öncesi silindi");
    check(fake_flash_erase_count() == 20, "kayıt başına TEK silme (gönderimde silmiyoruz)");

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 0 && st.dropped_full == 0, "hiç kayıt düşmedi");
}

/* ── 4. Halka dolduğunda en eski ezilir ────────────────────────────────── */
static void test_ring_full(void)
{
    printf("\n[4] halka dolduğunda EN ESKİ kayıt ezilir\n");
    setup(4);

    check(push_str("r1") == ESP_OK && push_str("r2") == ESP_OK &&
          push_str("r3") == ESP_OK && push_str("r4") == ESP_OK,
          "4 slot doldu, hiçbiri gönderilmedi");

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 4 && st.dropped_full == 0, "bekleyen 4, düşen 0");

    check(push_str("r5") == ESP_OK, "5. kayıt kabul edildi (yazma reddedilmiyor)");
    store_forward_stats(&st);
    check(st.dropped_full == 1, "en eski kayıt düştü ve sayıldı");
    check(st.stored == 4, "kapasite aşılmadı");

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "r2") == 0,
          "en eski artık r2 (r1 gitti)");

    /* Güncel veri yazılabilir kalmalı — dolu tamponda cihaz kör olmamalı. */
    check(push_str("r6") == ESP_OK, "dolu halkada yazma devam ediyor");
    store_forward_stats(&st);
    check(st.dropped_full == 2 && st.stored == 4, "ikinci düşüş sayıldı");
}

/* ── 5. Yeniden başlatma ───────────────────────────────────────────────── */
static void test_reboot(void)
{
    printf("\n[5] yeniden başlatma: bekleyenler hayatta, gönderilenler dönmüyor\n");
    setup(8);

    push_str("A"); push_str("B"); push_str("C");
    check(store_forward_pop(1) == ESP_OK, "A gönderildi");

    check(reboot() == ESP_OK, "reboot: halka başlıklardan kuruldu");

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 2, "bekleyen 2 kayıt flash'tan geri geldi");
    check(st.next_seq == 4, "seq sayacı sürdü (4)");

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "B") == 0 &&
          seq == 2, "sıra korundu: B (seq 2)");

    check(push_str("D") == ESP_OK, "reboot sonrası yazma çalışıyor");
    store_forward_stats(&st);
    check(st.next_seq == 5, "yeni kayıt seq 4 aldı");
}

/* ── 6. Sarma sonrası sıralama ─────────────────────────────────────────── */
static void test_reboot_after_wrap(void)
{
    printf("\n[6] halka sardıktan sonra sıra slot indisinden DEĞİL seq'ten gelir\n");
    setup(4);

    /* 3 kayıt yaz-gönder: slot 0,1,2 kullanıldı, sıradaki slot 3. */
    for (int i = 0; i < 3; i++) {
        char m[16]; snprintf(m, sizeof(m), "eski%d", i);
        push_str(m);
        char b[32]; uint32_t s = 0;
        peek_str(b, sizeof(b), &s);
        store_forward_pop(s);
    }

    push_str("X");   /* slot 3, seq 4 */
    push_str("Y");   /* slot 0'a sardı, seq 5 */

    check(reboot() == ESP_OK, "reboot");

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "X") == 0,
          "X önce geldi (slot 3), Y slot 0'da olmasına rağmen");
    store_forward_pop(seq);
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "Y") == 0,
          "sonra Y");

    /* Sarmanın sonrasında yeni yazma doğru slota gitmeli. */
    store_forward_pop(seq);
    check(push_str("Z") == ESP_OK && fake_flash_violations() == 0,
          "sarma sonrası yazma temiz");
}

/* ── 7. Elektrik kesintisi ─────────────────────────────────────────────── */
static void test_power_loss(void)
{
    printf("\n[7] elektrik kesintisi: yarım kayıt yok sayılır\n");

    /* 7a — yük indi, başlık HİÇ inmedi. magic oluşmadığı için slot boş sayılır. */
    setup(4);
    push_str("saglam");                       /* seq 1, bekliyor */

    fake_flash_power_cut_after(8);            /* 8 baytlık yükten sonra kes */
    esp_err_t e = store_forward_push("12345678", 8);
    check(e != ESP_OK, "7a: kesinti sırasındaki push hata döndü");

    check(reboot() == ESP_OK, "7a: reboot");
    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 1 && st.corrupt == 0,
          "7a: başlıksız slot boş sayıldı, yarım kayıt kuyruğa girmedi");

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "saglam") == 0,
          "7a: kesintiden ÖNCEKİ kayıt sağ kaldı");

    /* Yükü yazılmış ama silinmemiş slota tekrar yazmak 0 -> 1 gerektirir.
     * "her zaman sil" kararı tam bunu önlüyor. */
    check(push_str("yeni") == ESP_OK && fake_flash_violations() == 0,
          "7a: kirli slot yeniden kullanılırken silindi (0 -> 1 yok)");

    /* 7b — başlığın YALNIZ magic'i indi. Bu daha sinsi: magic tutuyor ama
     *      ver/len/crc 0xFF. Tutarsızlık başlıkta yakalanmalı. */
    setup(4);
    push_str("saglam");
    fake_flash_power_cut_after(8 + 4);        /* yük + başlığın ilk 4 baytı */
    e = store_forward_push("12345678", 8);
    check(e != ESP_OK, "7b: push hata döndü");

    check(reboot() == ESP_OK, "7b: reboot");
    store_forward_stats(&st);
    check(st.corrupt == 1, "7b: yarım başlık BOZUK sayıldı");
    check(st.stored == 1, "7b: bekleyen yalnız sağlam kayıt");
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "saglam") == 0,
          "7b: sağlam kayıt etkilenmedi");
}

/* ── 8. CRC bozulması ──────────────────────────────────────────────────── */
static void test_crc_guard(void)
{
    printf("\n[8] CRC tutmayan kayıt kuyruğu tıkamaz\n");
    setup(8);

    push_str("bozulacak");
    push_str("saglam");

    /* Slot 0'ın yükünde bir bit çürüt. Gerçek flash'ta yıllar içinde olur;
     * bozuk veriyi sunucuya göndermek çözümsüz bir kayıt üretirdi. */
    fake_flash_poke(SF_HEADER_SIZE + 2, 0x00);

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_OK && strcmp(buf, "saglam") == 0,
          "bozuk kayıt atlandı, sıradaki verildi");

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.corrupt == 1, "bozulma sayıldı");
    check(st.stored == 1, "bozuk kayıt kuyruktan düştü (tıkamıyor)");
}

/* ── 9. clear ──────────────────────────────────────────────────────────── */
static void test_clear(void)
{
    printf("\n[9] clear\n");
    setup(8);
    push_str("a"); push_str("b"); push_str("c");

    check(store_forward_clear() == ESP_OK, "clear");
    sf_stats_t st;
    store_forward_stats(&st);
    check(st.stored == 0, "bekleyen kalmadı");

    char buf[64]; uint32_t seq = 0;
    check(peek_str(buf, sizeof(buf), &seq) == ESP_ERR_NOT_FOUND, "peek boş");

    /* Bölüm SİLİNMEDİĞİ için reboot sonrası da geri gelmemeli — işaret
     * flash'a yazılmış olmalı, yalnız RAM'de değil. */
    check(reboot() == ESP_OK, "reboot");
    store_forward_stats(&st);
    check(st.stored == 0, "clear kalıcı (işaret flash'ta)");
}

/* ── 10. Gerçek bölüm geometrisi ───────────────────────────────────────── */
static void test_real_geometry(void)
{
    printf("\n[10] gerçek `buffer` bölümü (0x270000)\n");
    setup(0x270000 / SF_SLOT_SIZE);

    sf_stats_t st;
    store_forward_stats(&st);
    check(st.capacity == 624, "624 slot (2,4 MB / 4 KB)");

    /* 2,5 KB'lık tipik bir rapor (64 ham CAN frame + konum) varsayımıyla
     * bu ~10 saatlik kapsama kaybını tamponlar. */
    bool ok = true;
    for (uint32_t i = 0; i < st.capacity; i++) {
        char m[32]; snprintf(m, sizeof(m), "%lu", (unsigned long)i);
        if (push_str(m) != ESP_OK) { ok = false; break; }
    }
    check(ok, "624 kayıt yazıldı");
    store_forward_stats(&st);
    check(st.stored == 624 && st.dropped_full == 0, "tamamı bekliyor, düşen yok");

    push_str("tasan");
    store_forward_stats(&st);
    check(st.dropped_full == 1 && st.stored == 624, "625. kayıt en eskiyi ezdi");
    check(fake_flash_violations() == 0, "tam turda NOR ihlali yok");
}

int main(void)
{
    printf("store_forward halka testi "
           "(SF_SLOT_SIZE=%d, SF_PAYLOAD_MAX=%d, SF_MAX_SLOTS=%d)\n",
           SF_SLOT_SIZE, SF_PAYLOAD_MAX, SF_MAX_SLOTS);

    test_basic();
    test_sizes();
    test_reuse();
    test_ring_full();
    test_reboot();
    test_reboot_after_wrap();
    test_power_loss();
    test_crc_guard();
    test_clear();
    test_real_geometry();

    printf("\n[11] değişmezler\n");
    check(g_mutex_balance == 0, "her mutex al bir bırakla eşleşti");
    check(fake_flash_violations() == 0, "toplam NOR semantiği ihlali: 0");

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
