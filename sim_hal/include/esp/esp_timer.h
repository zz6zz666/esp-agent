/*
 * ESP-IDF esp_timer.h stub for desktop simulator
 *
 * Returns microseconds since boot from a monotonic clock.
 * On Windows: QueryPerformanceCounter
 * On POSIX:   clock_gettime(CLOCK_MONOTONIC)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>

static inline int64_t esp_timer_get_time(void)
{
    static LARGE_INTEGER freq = {0};
    static int freq_init = 0;
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    if (freq.QuadPart == 0) return 0;
    return (int64_t)(counter.QuadPart * 1000000LL / freq.QuadPart);
}
#else
# include <time.h>

/* Returns microseconds since boot (matches ESP32 behaviour). */
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
#endif

#ifdef __cplusplus
}
#endif
