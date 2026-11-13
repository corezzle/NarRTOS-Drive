/**
 * @file test_dead_reckoning.cpp
 * @brief Ölü hesap matematiğini sınar.
 *
 * Kayan nokta hesabı olduğu için karşılaştırmalar TOLERANSLI. Metre
 * cinsinden hata payı veriliyor; amaç bit eşitliği değil, konumun doğru
 * yönde ve doğru mesafede ilerlemesi.
 *
 * Neyi kanıtlıyor:
 *   - kuzey/doğu/güney/batı yönlerinde doğru mesafede ilerleme
 *   - boylamın enleme göre daralması
 *   - jiroskopla dönüşün yönü çevirmesi
 *   - duran araçta konumun kaymaması
 *   - belirsizliğin büyümesi ve eşiği aşınca tahminin GEÇERSİZ olması
 *   - jiroskopsuz durumda belirsizliğin daha hızlı büyümesi
 */

#include "../../src/services/dead_reckoning.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what)
{
    if (ok) { g_pass++; printf("  ✓ %s\n", what); }
    else    { g_fail++; printf("  ✗ %s\n", what); }
}

/* İstanbul civarı bir başlangıç noktası. */
static const int32_t LAT0 = 410000000;   /* 41,0000000 */
static const int32_t LON0 = 290000000;   /* 29,0000000 */

/** İki nokta arası mesafe (metre), küçük mesafeler için düz yaklaşım. */
static double dist_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2)
{
    const double MPD = 111320.0;
    double dlat = (double)(lat2 - lat1) / 1e7 * MPD;
    double coslat = cos((double)lat1 / 1e7 * M_PI / 180.0);
    double dlon = (double)(lon2 - lon1) / 1e7 * MPD * coslat;
    return sqrt(dlat * dlat + dlon * dlon);
}

/** 60 saniye boyunca sabit hızla, sabit yönde sür. */
static void drive(dr_state_t* st, uint16_t kmh_x10, int16_t yaw_x10,
                  int saniye, bool gyro = true)
{
    for (int i = 0; i < saniye; i++) dr_step(st, kmh_x10, yaw_x10, 1000, gyro);
}

/* ── 1. Yönler ─────────────────────────────────────────────────────────── */
static void test_directions(void)
{
    printf("\n[1] dört yönde ilerleme\n");
    /* 36 km/sa = 10 m/sn. 60 saniyede 600 metre. */
    struct { uint16_t hdg; const char* ad; int dlat; int dlon; } c[] = {
        { 0,    "kuzey", +1,  0 },
        { 900,  "doğu",   0, +1 },
        { 1800, "güney", -1,  0 },
        { 2700, "batı",   0, -1 },
    };
    bool hepsi = true;
    for (auto& t : c) {
        dr_state_t st;
        dr_seed(&st, LAT0, LON0, t.hdg);
        drive(&st, 360, 0, 60);

        double d = dist_m(LAT0, LON0, st.lat_e7, st.lon_e7);
        bool mesafe_ok = fabs(d - 600.0) < 5.0;
        bool yon_ok = true;
        if (t.dlat > 0 && st.lat_e7 <= LAT0) yon_ok = false;
        if (t.dlat < 0 && st.lat_e7 >= LAT0) yon_ok = false;
        if (t.dlon > 0 && st.lon_e7 <= LON0) yon_ok = false;
        if (t.dlon < 0 && st.lon_e7 >= LON0) yon_ok = false;
        if (t.dlat == 0 && abs(st.lat_e7 - LAT0) > 200) yon_ok = false;
        if (t.dlon == 0 && abs(st.lon_e7 - LON0) > 200) yon_ok = false;

        if (!mesafe_ok || !yon_ok) {
            printf("      %s: %.1f m, lat%+d lon%+d\n", t.ad, d,
                   st.lat_e7 - LAT0, st.lon_e7 - LON0);
            hepsi = false;
        }
    }
    check(hepsi, "36 km/sa × 60 sn = 600 m, dört yönde de doğru");
}

/* ── 2. Boylam enleme göre daralıyor ───────────────────────────────────── */
static void test_longitude_scaling(void)
{
    printf("\n[2] boylam derecesi enleme göre daralıyor\n");
    /* Ekvatorda ve 60. enlemde aynı mesafe doğuya gidilsin. 60. enlemde
     * boylam farkı yaklaşık iki katı olmalı (cos 60 = 0,5). */
    dr_state_t a, b;
    dr_seed(&a, 0,         290000000, 900);   /* ekvator */
    dr_seed(&b, 600000000, 290000000, 900);   /* 60. enlem */
    drive(&a, 360, 0, 60);
    drive(&b, 360, 0, 60);

    double dlon_a = (double)(a.lon_e7 - 290000000);
    double dlon_b = (double)(b.lon_e7 - 290000000);
    double oran = dlon_b / dlon_a;
    check(fabs(oran - 2.0) < 0.05, "60. enlemde boylam farkı ~2 katı");
    if (fabs(oran - 2.0) >= 0.05) printf("      oran: %.3f\n", oran);

    /* Metre cinsinden mesafe ikisinde de aynı olmalı. */
    double da = dist_m(0, 290000000, a.lat_e7, a.lon_e7);
    double db = dist_m(600000000, 290000000, b.lat_e7, b.lon_e7);
    check(fabs(da - db) < 5.0, "gerçek mesafe ikisinde de ~600 m");
}

/* ── 3. Dönüş ──────────────────────────────────────────────────────────── */
static void test_turning(void)
{
    printf("\n[3] jiroskopla dönüş\n");
    dr_state_t st;
    dr_seed(&st, LAT0, LON0, 0);            /* kuzeye bakıyor */
    /* 9 derece/sn sağa, 10 saniye = 90 derece. */
    drive(&st, 360, 90, 10);
    check(st.heading_deg_x10 >= 880 && st.heading_deg_x10 <= 920,
          "10 sn × 9 derece/sn -> yön ~90 derece (doğu)");

    /* Sola dönüş ve 0'ın altına sarma. */
    dr_state_t s2;
    dr_seed(&s2, LAT0, LON0, 100);          /* 10 derece */
    drive(&s2, 0, -50, 10);                 /* 5 derece/sn sola, 10 sn = -50 */
    /* 10 derece - 50 derece = -40 -> +360 = 320 derece */
    check(s2.heading_deg_x10 >= 3180 && s2.heading_deg_x10 <= 3220,
          "0 derecenin altına sarma doğru (yön ~320)");
    if (s2.heading_deg_x10 < 3180 || s2.heading_deg_x10 > 3220)
        printf("      yön: %u\n", s2.heading_deg_x10);
}

/* ── 4. Duran araç ─────────────────────────────────────────────────────── */
static void test_stationary(void)
{
    printf("\n[4] duran araçta konum kaymıyor\n");
    dr_state_t st;
    dr_seed(&st, LAT0, LON0, 0);
    drive(&st, 0, 0, 120);                  /* 2 dakika, hız sıfır */

    check(st.lat_e7 == LAT0 && st.lon_e7 == LON0, "konum hiç değişmedi");
    check(st.distance_m == 0, "mesafe sıfır");
    /* Duruyorken bile belirsizlik bir miktar büyüyebilir ama patlamamalı. */
    check(st.valid, "iki dakika duruşta tahmin hâlâ geçerli");
}

/* ── 5. Belirsizlik ve geçerlilik sınırı ───────────────────────────────── */
static void test_uncertainty(void)
{
    printf("\n[5] belirsizlik büyüyor ve sınırı var\n");
    dr_state_t st;
    dr_seed(&st, LAT0, LON0, 0);
    uint16_t u0 = dr_uncertainty_m(&st);

    drive(&st, 900, 0, 60);                 /* 90 km/sa, 1 dakika = 1,5 km */
    check(dr_uncertainty_m(&st) > u0, "belirsizlik büyüdü");
    /* %3 mesafe hatası: 1500 m yolda ~45 m. */
    check(dr_uncertainty_m(&st) >= 40 && dr_uncertainty_m(&st) <= 90,
          "1,5 km sonra belirsizlik 40-90 m arası");
    if (dr_uncertainty_m(&st) < 40 || dr_uncertainty_m(&st) > 90)
        printf("      belirsizlik: %u m\n", dr_uncertainty_m(&st));
    check(st.valid, "bu noktada hâlâ geçerli");

    /* Uzun süre sürünce eşik aşılmalı ve tahmin düşmeli. */
    drive(&st, 900, 0, 900);                /* 15 dakika daha */
    check(!st.valid, "eşik aşılınca tahmin GEÇERSİZ (aşırı sapmış tahmin gönderilmiyor)");

    /* Geçersizken adım atmak durumu değiştirmemeli. */
    int32_t lat = st.lat_e7;
    dr_step(&st, 900, 0, 1000, true);
    check(st.lat_e7 == lat, "geçersiz durumda adım yok sayılıyor");
}

/* ── 6. Jiroskopsuz ────────────────────────────────────────────────────── */
static void test_no_gyro(void)
{
    printf("\n[6] jiroskop yokken belirsizlik daha hızlı büyüyor\n");
    dr_state_t a, b;
    dr_seed(&a, LAT0, LON0, 0);
    dr_seed(&b, LAT0, LON0, 0);
    drive(&a, 600, 0, 60, true);
    drive(&b, 600, 0, 60, false);
    check(b.uncertainty_cm > a.uncertainty_cm,
          "jiroskopsuz belirsizlik daha büyük (dönüşler görülmüyor)");

    /* Jiroskopsuzken yön değişmemeli. */
    dr_state_t c;
    dr_seed(&c, LAT0, LON0, 450);
    drive(&c, 360, 200, 10, false);
    check(c.heading_deg_x10 == 450, "jiroskopsuz yön sabit kalıyor");
}

/* ── 7. Sıfırlama ve tohumlama ─────────────────────────────────────────── */
static void test_seed_reset(void)
{
    printf("\n[7] tohumlama ve sıfırlama\n");
    dr_state_t st;
    dr_seed(&st, LAT0, LON0, 0);
    drive(&st, 900, 0, 60);

    /* Uydu geri geldiğinde yeniden tohumlanıyor: sayaçlar sıfırlanmalı. */
    dr_seed(&st, LAT0, LON0, 1800);
    check(st.distance_m == 0 && st.elapsed_ms == 0, "tohumlama sayaçları sıfırlıyor");
    check(dr_uncertainty_m(&st) <= 10, "tohumlamadan sonra belirsizlik küçük");
    check(st.heading_deg_x10 == 1800, "yeni yön alındı");

    dr_reset(&st);
    check(!st.valid, "sıfırlama sonrası geçersiz");
    dr_step(&st, 900, 0, 1000, true);
    check(!st.valid, "sıfırlanmış durum adımla geri gelmiyor");
}

int main(void)
{
    printf("dead_reckoning testi\n");
    test_directions();
    test_longitude_scaling();
    test_turning();
    test_stationary();
    test_uncertainty();
    test_no_gyro();
    test_seed_reset();
    printf("\n%d geçti, %d başarısız\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
