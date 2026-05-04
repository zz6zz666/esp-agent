/*
 * ESP-IDF esp_system.h stub for desktop simulator
 *
 * Reads real host memory statistics from /proc/meminfo.
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

/* Helper: read a /proc/meminfo key, return value in bytes (file reports kB) */
static inline size_t proc_meminfo_read(const char *key)
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

static inline size_t esp_get_free_heap_size(void)
{
    return proc_meminfo_read("MemAvailable:");
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
