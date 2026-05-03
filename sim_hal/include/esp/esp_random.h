/*
 * ESP-IDF esp_random.h stub for desktop simulator
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t esp_random(void)
{
    return (uint32_t)rand();
}

static inline void esp_fill_random(void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    for (size_t i = 0; i < len; i++)
        p[i] = (unsigned char)(rand() & 0xFF);
}

#ifdef __cplusplus
}
#endif
