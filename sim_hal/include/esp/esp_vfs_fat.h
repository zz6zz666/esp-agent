#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t esp_vfs_fat_info(const char *base_path,
                                          uint64_t *out_total_bytes,
                                          uint64_t *out_free_bytes)
{
    /* Desktop: report fixed reasonable values */
    (void)base_path;
    if (out_total_bytes) *out_total_bytes = 100ULL * 1024 * 1024 * 1024;
    if (out_free_bytes)  *out_free_bytes  =  50ULL * 1024 * 1024 * 1024;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
