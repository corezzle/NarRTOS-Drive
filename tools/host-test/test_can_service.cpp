/**
 * @file test_can_service.cpp
 * @brief can_service'in tablo mantığını Mac üzerinde sınar.
 *
 * GERÇEK can_service.cpp derleniyor — kopyalanmış bir mantık değil. ESP-IDF
 * başlıkları `shims/` altındaki taklitlerle karşılanıyor. Böylece sahada
 * çalışacak kodun aynısı test ediliyor.
 *
 * Neyi kanıtlıyor:
 *   - kimlik başına birleştirme (FIFO değil) — M2'nin ana tasarım kararı
 *   - en son verinin eskisini ezmesi
 *   - std/ext aynı sayısal kimliğin ayrı tutulması
 *   - dlc kırpma
 *   - anlık görüntünün en tazeden başlaması ve sınırı respect etmesi
 *   - tablo dolduğunda YENİ kimliğin atılması, mevcutların korunması
 *   - clear sonrası sayaçların sıfırlanması
 *   - mutex al/bırak dengesi
 *
 * Çalıştırma:  tools/host-test/run.sh
 */

#include "../../src/services/can_service.h"
#include "../../src/core/os_config.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

int g_mutex_balance = 0;

/* can_service_init() bunu çağırıyor; testte sink'i tutmuyoruz. */
extern "C" void command_service_set_can_sink(void (*fn)(uint32_t, bool,
                                                        const uint8_t*, uint8_t))
{
    (void)fn;
}

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

static void inject(uint32_t id, bool ext, std::initializer_list<uint8_t> bytes)
{
    uint8_t buf[8] = {0};
    uint8_t n = 0;
    for (uint8_t b : bytes) { if (n < 8) buf[n++] = b; }
    can_service_ingest(id, ext, buf, (uint8_t)bytes.size());
}

/* ── 1. Kimlik başına birleştirme ──────────────────────────────────────── */
static void test_dedup(void)
{
    printf("\n[1] kimlik başına birleştirme (FIFO değil)\n");
    can_service_clear();

    inject(0x18FEE500, true, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    inject(0x18F00400, true, {0xAA, 0xBB, 0xCC});
    inject(0x18FEE500, true, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});

    can_stats_t st;
    can_service_stats(&st);
    check(st.rx_total == 3, "3 frame alındı");
    check(st.distinct == 2, "2 farklı kimlik (FIFO olsaydı 3 kayıt olurdu)");

    can_frame_t f[8];
    size_t n = can_service_snapshot(f, 8);
    check(n == 2, "anlık görüntü 2 frame");

    /* En son enjekte edilen 0x18FEE500 olduğu için ilk sırada olmalı. */
    check(f[0].id == 0x18FEE500, "en taze kimlik başta");
    check(f[0].data[0] == 0x11 && f[0].data[7] == 0x88,
          "son veri eskisini ezdi (01.. değil 11..)");
    check(f[0].count == 2, "bu kimlikten 2 frame sayıldı");
    check(f[0].dlc == 8, "dlc 8");
    check(f[1].id == 0x18F00400 && f[1].dlc == 3, "ikinci kimlik dlc 3");
}

/* ── 2. std / ext ayrımı ───────────────────────────────────────────────── */
static void test_std_ext(void)
{
    printf("\n[2] aynı sayısal kimlik, farklı format\n");
    can_service_clear();

    inject(0x123, false, {0x01});
    inject(0x123, true,  {0x02});

    can_stats_t st;
    can_service_stats(&st);
    check(st.distinct == 2, "std ve ext ayrı girdi (CAN'de farklı kimlikler)");
}

/* ── 3. dlc kırpma ─────────────────────────────────────────────────────── */
static void test_dlc_clamp(void)
{
    printf("\n[3] dlc kırpma\n");
    can_service_clear();

    uint8_t big[16];
    memset(big, 0xEE, sizeof(big));
    can_service_ingest(0x200, false, big, 16);   /* geçersiz dlc */

    can_frame_t f[2];
    size_t n = can_service_snapshot(f, 2);
    check(n == 1 && f[0].dlc == 8, "dlc 16 -> 8'e kırpıldı (taşma yok)");
}

/* ── 4. Anlık görüntü sırası ve sınırı ─────────────────────────────────── */
static void test_snapshot_order(void)
{
    printf("\n[4] anlık görüntü: en tazeden başlar, sınırı aşmaz\n");
    can_service_clear();

    for (uint32_t i = 0; i < 10; i++) inject(0x300 + i, false, {(uint8_t)i});

    can_frame_t f[4];
    size_t n = can_service_snapshot(f, 4);
    check(n == 4, "sınır 4 -> 4 frame");
    check(f[0].id == 0x309, "en son eklenen başta");
    check(f[3].id == 0x306, "dördüncü sırada 0x306");

    bool azalan = true;
    for (size_t i = 1; i < n; i++)
        if (f[i].last_us > f[i-1].last_us) azalan = false;
    check(azalan, "zaman damgaları azalan sırada");
}

/* ── 5. Tablo taşması ──────────────────────────────────────────────────── */
static void test_overflow(void)
{
    printf("\n[5] tablo dolduğunda yeni kimlik atılır\n");
    can_service_clear();

    for (uint32_t i = 0; i < CAN_ID_TABLE_SIZE; i++)
        inject(0x1000 + i, false, {0x01});

    can_stats_t st;
    can_service_stats(&st);
    check(st.distinct == CAN_ID_TABLE_SIZE, "tablo tam doldu");
    check(st.dropped_full == 0, "henüz atılan yok");

    inject(0xBEEF, false, {0x02});          /* yeni kimlik — atılmalı */
    can_service_stats(&st);
    check(st.dropped_full == 1, "yeni kimlik atıldı ve sayıldı");
    check(st.distinct == CAN_ID_TABLE_SIZE, "kapasite aşılmadı");

    /* Mevcut bir kimlik hâlâ güncellenebilmeli — taşma onları kilitlememeli.
     * Bu önemli: dolu tabloda araç verisi donmuş olurdu. */
    inject(0x1000, false, {0x99});
    can_frame_t f[CAN_ID_TABLE_SIZE];
    size_t n = can_service_snapshot(f, CAN_ID_TABLE_SIZE);
    bool guncellendi = (n > 0 && f[0].id == 0x1000 && f[0].data[0] == 0x99);
    check(guncellendi, "dolu tabloda mevcut kimlik güncellenmeye devam ediyor");
}

/* ── 6. clear ──────────────────────────────────────────────────────────── */
static void test_clear(void)
{
    printf("\n[6] clear\n");
    inject(0x1, false, {0x01});
    can_service_clear();

    can_stats_t st;
    can_service_stats(&st);
    check(st.rx_total == 0 && st.distinct == 0 && st.dropped_full == 0,
          "sayaçlar ve tablo sıfırlandı");

    can_frame_t f[2];
    check(can_service_snapshot(f, 2) == 0, "anlık görüntü boş");
}

int main(void)
{
    printf("can_service tablo testi (CAN_ID_TABLE_SIZE=%d)\n", CAN_ID_TABLE_SIZE);
    can_service_init();

    test_dedup();
    test_std_ext();
    test_dlc_clamp();
    test_snapshot_order();
    test_overflow();
    test_clear();

    printf("\n[7] mutex dengesi\n");
    check(g_mutex_balance == 0, "her al bir bırakla eşleşti");

    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
