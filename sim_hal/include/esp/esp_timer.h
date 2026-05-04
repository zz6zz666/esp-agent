/*
 * ESP-IDF esp_timer.h stub for desktop simulator
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns microseconds since boot (matches ESP32 behaviour). */
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

#ifdef __cplusplus
}
#endif
