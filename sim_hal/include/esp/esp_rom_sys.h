#pragma once
#include <stdint.h>

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <time.h>
# include <errno.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void esp_rom_delay_us(uint32_t us)
{
#if defined(PLATFORM_WINDOWS)
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    int64_t target = start.QuadPart + (freq.QuadPart * us) / 1000000;
    do { QueryPerformanceCounter(&now); } while (now.QuadPart < target);
#else
    struct timespec req, rem;
    req.tv_sec = (time_t)(us / 1000000);
    req.tv_nsec = (long)((us % 1000000) * 1000);
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
#endif
}

#ifdef __cplusplus
}
#endif
