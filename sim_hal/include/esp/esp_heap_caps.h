/*
 * ESP-IDF esp_heap_caps.h stub for desktop simulator
 *
 * Reads real host memory from /proc/meminfo.
 * MALLOC_CAP_INTERNAL  → system RAM
 * MALLOC_CAP_SPIRAM    → 0 (no PSRAM on desktop)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_INTERNAL (1 << 0)
#define MALLOC_CAP_SPIRAM   (1 << 1)
#define MALLOC_CAP_DMA      (1 << 2)
#define MALLOC_CAP_DEFAULT  (1 << 3)
#define MALLOC_CAP_8BIT     (1 << 4)

/* Helper: read a /proc/meminfo key, return value in bytes (file reports kB) */
static inline size_t _heap_proc_meminfo_read(const char *key)
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

/* Track historical minimum free (internal RAM only) */
static inline size_t _heap_min_tracker(size_t cur)
{
    static size_t min_seen = 0;
    if (cur == 0) return min_seen;
    if (min_seen == 0 || cur < min_seen) min_seen = cur;
    return min_seen;
}

/* ---- malloc/free (unchanged — desktop libc is fine) ---- */

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    (void)caps;
    return aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
}

static inline void heap_caps_free(void *ptr)
{
    free(ptr);
}

/* ---- size queries — real /proc/meminfo data ---- */

static inline size_t heap_caps_get_total_size(uint32_t caps)
{
    if (caps & MALLOC_CAP_SPIRAM) return 0; /* no PSRAM on desktop */
    return _heap_proc_meminfo_read("MemTotal:");
}

static inline size_t heap_caps_get_free_size(uint32_t caps)
{
    if (caps & MALLOC_CAP_SPIRAM) return 0;
    return _heap_proc_meminfo_read("MemAvailable:");
}

static inline size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
    if (caps & MALLOC_CAP_SPIRAM) return 0;
    return _heap_min_tracker(heap_caps_get_free_size(caps));
}

static inline size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    if (caps & MALLOC_CAP_SPIRAM) return 0;
    /* /proc/meminfo doesn't expose largest contiguous block easily.
       Use MemFree as an approximation. */
    return _heap_proc_meminfo_read("MemFree:");
}

#ifdef __cplusplus
}
#endif
