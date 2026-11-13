#pragma once
/* Mutex taklidi — testte tek iş parçacığı var. Yine de al/bırak dengesini
   sayıyoruz: kod bir yerde bırakmayı unutursa test yakalasın. */
#include "FreeRTOS.h"
typedef struct { int depth; } sem_t_;
typedef sem_t_* SemaphoreHandle_t;
extern int g_mutex_balance;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static sem_t_ s = {0}; return &s;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) {
    (void)t; if (h) h->depth++, g_mutex_balance++; return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
    if (h) h->depth--, g_mutex_balance--; return pdTRUE;
}
