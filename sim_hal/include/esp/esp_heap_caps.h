/*
 * ESP-IDF esp_heap_caps.h stub for desktop simulator
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
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

/* On desktop, all memory is the same — just use regular malloc/free */

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    (void)caps;
    /* Use posix_memalign via aligned_alloc */
    return aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
}

static inline void heap_caps_free(void *ptr)
{
    free(ptr);
}

static inline size_t heap_caps_get_total_size(uint32_t caps)
{
    (void)caps;
    return 1024UL * 1024 * 1024; /* 1 GiB placeholder */
}

static inline size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return 512UL * 1024 * 1024; /* 512 MiB placeholder */
}

static inline size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
    (void)caps;
    return 256UL * 1024 * 1024; /* 256 MiB placeholder */
}

static inline size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return 128UL * 1024 * 1024; /* 128 MiB placeholder */
}

#ifdef __cplusplus
}
#endif
