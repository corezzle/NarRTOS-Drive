#pragma once
/* Tek iş parçacıklı testte gerçek zamanlayıcıya gerek yok. */
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE  1
#define pdPASS  1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) (x)
