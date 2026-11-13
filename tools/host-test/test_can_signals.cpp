/**
 * @file test_can_signals.cpp
 * @brief Tekerlek hızı çıkarımını sınar.
 *
 * Bu modül "cihaz CAN'i çözmez" kararına yapılan tek istisna. Yanlış çözerse
 * ölü hesap yanlış mesafe yürütür ve tünelde araç haritada başka yere gider.
 *
 * Neyi kanıtlıyor:
 *   - PGN çıkarımı (PDU1/PDU2 ayrımı)
 *   - eşleştirmenin ÖNCELİK ve KAYNAK ADRESİNDEN bağımsız olması
 *   - J1939'un "veri yok" / "hata" aralıklarının hız sanılmaması
 *   - 1/256 km/sa çözünürlüğünün doğru dönüştürülmesi
 */

#include "../../src/services/can_signals.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/* CCVS1 frame'i kur: hız 2. ve 3. baytta, 1/256 km/sa. */
static void mk_ccvs1(uint8_t* d, uint16_t raw)
{
    memset(d, 0xFF, 8);
    d[0] = 0x00;
    d[1] = (uint8_t)(raw & 0xFF);
    d[2] = (uint8_t)(raw >> 8);
}

/* ── 1. PGN çıkarımı ───────────────────────────────────────────────────── */
static void test_pgn(void)
{
    printf("\n[1] PGN çıkarımı\n");
    /* 0x18FEF100: öncelik 6, PF=FE (>=240, PDU2), PS=F1 -> PGN 0xFEF1 */
    check(can_signal_pgn(0x18FEF100u) == 65265u, "0x18FEF100 -> PGN 65265 (CCVS1)");
    /* PDU1 örneği: PF=EF (<240), PS hedef adres, PGN'e girmez */
    check(can_signal_pgn(0x18EF2100u) == 0xEF00u, "PDU1'de hedef adres PGN'e girmiyor");
    check(can_signal_pgn(0x0CF00400u) == 61444u, "0x0CF00400 -> PGN 61444 (EEC1)");
}

/* ── 2. Öncelik ve kaynak adresi fark etmiyor ──────────────────────────── */
static void test_priority_independence(void)
{
    printf("\n[2] eşleştirme öncelik ve kaynak adresinden BAĞIMSIZ\n");
    uint8_t d[8];
    mk_ccvs1(d, 256 * 50);          /* 50,0 km/sa */
    uint16_t v = 0;

    /* Sunucudaki DBC eşleştirmesi önceliğe duyarlı ve bu sahada sorun
     * çıkarıyor. Cihaz içinde böyle bir kısıt olmamalı. */
    bool hepsi = true;
    const uint32_t ids[] = { 0x18FEF100u, 0x0CFEF100u, 0x1CFEF11Fu, 0x00FEF1FEu };
    for (uint32_t id : ids)
        if (!can_signal_wheel_speed(id, d, 8, &v) || v != 500) hepsi = false;
    check(hepsi, "dört farklı öncelik/kaynak adresi de çözüldü");

    /* Başka bir PGN eşleşmemeli. */
    check(!can_signal_wheel_speed(0x18FEE500u, d, 8, &v), "farklı PGN reddedildi");
}

/* ── 3. Çözünürlük ─────────────────────────────────────────────────────── */
static void test_resolution(void)
{
    printf("\n[3] 1/256 km/sa çözünürlüğü\n");
    struct { uint16_t raw; uint16_t kmh10; const char* ad; } c[] = {
        { 0,          0,    "0 km/sa" },
        { 256,        10,   "1,0 km/sa" },
        { 256 * 50,   500,  "50,0 km/sa" },
        { 256 * 90,   900,  "90,0 km/sa" },
        { 128,        5,    "0,5 km/sa" },
        { 25600,      1000, "100,0 km/sa" },
    };
    bool hepsi = true;
    uint8_t d[8]; uint16_t v = 0;
    for (auto& t : c) {
        mk_ccvs1(d, t.raw);
        if (!can_signal_wheel_speed(0x18FEF100u, d, 8, &v) || v != t.kmh10) {
            printf("      %s: %u geldi, %u bekleniyordu\n", t.ad, v, t.kmh10);
            hepsi = false;
        }
    }
    check(hepsi, "dönüşüm tablosu doğru");
}

/* ── 4. Geçersiz değerler ──────────────────────────────────────────────── */
static void test_invalid(void)
{
    printf("\n[4] J1939 'veri yok' ve 'hata' aralıkları\n");
    uint8_t d[8]; uint16_t v = 0;

    /* Bunları 0 km/sa okumak, hareket eden aracı duruyor sanmak olurdu;
     * ölü hesap da hiç ilerlemezdi. */
    mk_ccvs1(d, 0xFFFF);
    check(!can_signal_wheel_speed(0x18FEF100u, d, 8, &v), "0xFFFF (veri yok) reddedildi");
    mk_ccvs1(d, 0xFE00);
    check(!can_signal_wheel_speed(0x18FEF100u, d, 8, &v), "0xFE00 (hata) reddedildi");
    mk_ccvs1(d, 0xFDFF);
    check(can_signal_wheel_speed(0x18FEF100u, d, 8, &v), "0xFDFF hâlâ geçerli veri");

    mk_ccvs1(d, 256 * 50);
    check(!can_signal_wheel_speed(0x18FEF100u, d, 2, &v), "kısa frame reddedildi");
    check(!can_signal_wheel_speed(0x18FEF100u, nullptr, 8, &v), "NULL güvenli");
    check(!can_signal_wheel_speed(0x18FEF100u, d, 8, nullptr), "NULL çıktı güvenli");
}

int main(void)
{
    printf("can_signals testi\n");
    test_pgn();
    test_priority_independence();
    test_resolution();
    test_invalid();
    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
