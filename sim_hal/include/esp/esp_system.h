/*
 * ESP-IDF esp_system.h stub for desktop simulator
 *
 * Reads real host memory statistics.
 * On Windows: GlobalMemoryStatusEx
 * On POSIX:   /proc/meminfo
 */
#pragma once

#include "esp_err.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void esp_restart(void)
{
    fprintf(stderr, "esp_restart() called — exiting simulator\n");
    exit(0);
}

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>

static inline size_t _meminfo_read(const char *key)
{
    MEMORYSTATUSEX mex;
    mex.dwLength = sizeof(mex);
    if (!GlobalMemoryStatusEx(&mex)) return 0;

    if (strcmp(key, "MemTotal:") == 0)
        return (size_t)mex.ullTotalPhys;
    if (strcmp(key, "MemAvailable:") == 0)
        return (size_t)mex.ullAvailPhys;
    if (strcmp(key, "MemFree:") == 0)
        return (size_t)(mex.ullAvailPhys > mex.ullTotalPhys / 10
                        ? mex.ullAvailPhys - mex.ullTotalPhys / 10
                        : mex.ullAvailPhys);
    return 0;
}
#else
/* Helper: read a /proc/meminfo key, return value in bytes (file reports kB) */
static inline size_t _meminfo_read(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    size_t val_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            sscanf(line, "%*s %zu", &val_kb);
            break;
        }
    }
    fclose(f);
    return val_kb * 1024;
}
#endif

static inline size_t esp_get_free_heap_size(void)
{
    return _meminfo_read("MemAvailable:");
}

static inline size_t esp_get_minimum_free_heap_size(void)
{
    /* Track minimum across calls — approximates the ESP concept on a desktop */
    static size_t min_seen = 0;
    size_t cur = esp_get_free_heap_size();
    if (cur == 0) return min_seen;
    if (min_seen == 0 || cur < min_seen) min_seen = cur;
    return min_seen;
}

#ifdef __cplusplus
}
#endif
