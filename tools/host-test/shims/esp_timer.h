#pragma once
/* Monoton artan sahte saat. Gerçek zamana bağlı olmaması testi
   deterministik yapıyor: enjeksiyon sırası = zaman sırası. */
#include <stdint.h>
static int64_t g_fake_us = 0;
static inline int64_t esp_timer_get_time(void) { return ++g_fake_us; }
