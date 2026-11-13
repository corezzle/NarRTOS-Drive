#pragma once
/* Log makroları — testte sessiz. Çıktıyı görmek istersen HOST_TEST_VERBOSE
   tanımlayarak derle. */
#ifdef HOST_TEST_VERBOSE
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E %s] " fmt "\n", tag, ##__VA_ARGS__)
#else
/* tag'e dokunuyoruz ki üretim kodundaki `static const char* TAG`
   "kullanılmayan değişken" uyarısı vermesin — kabuk yüzünden çıkan
   sahte bir uyarı için üretim kodunu değiştirmek yanlış olurdu. */
#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
#define ESP_LOGW(tag, fmt, ...) ((void)(tag))
#define ESP_LOGE(tag, fmt, ...) ((void)(tag))
#endif
