/**
 * @file fake_flash.cpp
 * @brief sf_flash'ın host uygulaması (bkz. fake_flash.h).
 */

#include "fake_flash.h"
#include "../../src/drivers/flash/sf_flash.h"
#include "../../src/core/os_config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

static uint8_t* s_mem     = nullptr;
static size_t   s_size    = 0;
static int      s_viol    = 0;
static size_t   s_budget  = SIZE_MAX;
static bool     s_powered = true;
static uint32_t s_erases  = 0;
static uint32_t s_writes  = 0;

static void viol(const char* what, size_t off, size_t len)
{
    s_viol++;
    printf("  ‼ FLASH İHLALİ: %s (off=%zu len=%zu)\n", what, off, len);
}

void fake_flash_configure(size_t size)
{
    free(s_mem);
    s_mem  = (uint8_t*)malloc(size);
    s_size = size;
    memset(s_mem, 0xFF, size);
    /* s_viol KASTEN sıfırlanmıyor: koşumun sonundaki "toplam ihlal 0" kontrolü
     * bütün testleri kapsasın. */
    s_erases = 0; s_writes = 0;
    s_budget = SIZE_MAX;
    s_powered = true;
}

void fake_flash_power_cut_after(size_t n) { s_budget = n; }

void fake_flash_power_restore(void) { s_powered = true; s_budget = SIZE_MAX; }

int fake_flash_violations(void) { return s_viol; }

void fake_flash_poke(size_t off, uint8_t val)
{
    if (off < s_size) s_mem[off] = val;
}

uint32_t fake_flash_erase_count(void) { return s_erases; }
uint32_t fake_flash_write_count(void) { return s_writes; }
void     fake_flash_reset_counters(void) { s_erases = 0; s_writes = 0; }

/* ── sf_flash arayüzü ───────────────────────────────────────────────────── */

esp_err_t sf_flash_init(void)
{
    /* Yeniden başlatmada içerik korunmalı — configure değil, yalnız hazır mı
     * kontrolü. Gerçek donanımda da bölüm bulunur, silinmez. */
    return s_mem ? ESP_OK : ESP_ERR_NOT_FOUND;
}

size_t sf_flash_size(void) { return s_mem ? s_size : 0; }

esp_err_t sf_flash_read(size_t off, void* dst, size_t len)
{
    if (!s_mem) return ESP_ERR_INVALID_STATE;
    if (off + len > s_size) { viol("okuma sınır dışı", off, len); return ESP_ERR_INVALID_SIZE; }
    memcpy(dst, s_mem + off, len);
    return ESP_OK;
}

esp_err_t sf_flash_write(size_t off, const void* src, size_t len)
{
    if (!s_mem) return ESP_ERR_INVALID_STATE;
    if (!s_powered) return ESP_FAIL;
    if (off + len > s_size) { viol("yazma sınır dışı", off, len); return ESP_ERR_INVALID_SIZE; }
    if (off % 4 || len % 4) { viol("yazma 4 bayta hizalı değil", off, len); return ESP_ERR_INVALID_ARG; }

    const uint8_t* p = (const uint8_t*)src;

    /* 1 -> 0 dışında bir geçiş isteniyor mu? Gerçek flash'ta bu sessizce
     * yanlış veri üretir; burada ihlal sayıyoruz. */
    for (size_t i = 0; i < len; i++) {
        uint8_t old = s_mem[off + i];
        if ((uint8_t)(old & p[i]) != p[i]) {
            viol("silinmemiş bölgeye yazma (0 -> 1)", off + i, 1);
            return ESP_FAIL;
        }
    }

    /* Elektrik kesintisi: bütçe bu yazmanın ortasında biterse sığan kısım
     * iner, gerisi inmez ve besleme gider. */
    size_t writable = (s_budget >= len) ? len : s_budget;
    for (size_t i = 0; i < writable; i++) s_mem[off + i] &= p[i];
    s_budget -= writable;
    s_writes++;

    if (writable < len) {
        s_powered = false;
        printf("  ⚡ elektrik kesildi: %zu/%zu bayt yazıldı\n", writable, len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sf_flash_erase(size_t off, size_t len)
{
    if (!s_mem) return ESP_ERR_INVALID_STATE;
    if (!s_powered) return ESP_FAIL;
    if (off + len > s_size) { viol("silme sınır dışı", off, len); return ESP_ERR_INVALID_SIZE; }
    if (off % SF_SLOT_SIZE || len % SF_SLOT_SIZE) {
        viol("silme sektöre hizalı değil", off, len);
        return ESP_ERR_INVALID_ARG;
    }
    memset(s_mem + off, 0xFF, len);
    s_erases++;
    return ESP_OK;
}
