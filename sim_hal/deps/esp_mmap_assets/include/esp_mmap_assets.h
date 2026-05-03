/*
 * esp_mmap_assets.h — stub for desktop simulator
 *
 * On real hardware this memory-maps a flash partition containing an asset
 * bundle.  On the simulator we read files from a directory.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mmap_assets_s *mmap_assets_handle_t;

typedef struct {
    const char *partition_label;
    const char *path;           /* directory path (simulator extension) */
    struct {
        unsigned char mmap_enable : 1;
        unsigned char use_fs     : 1;
        unsigned char full_check : 1;
    } flags;
} mmap_assets_config_t;

esp_err_t mmap_assets_new(const mmap_assets_config_t *cfg, mmap_assets_handle_t *out_handle);
void      mmap_assets_del(mmap_assets_handle_t handle);
int       mmap_assets_get_stored_files(mmap_assets_handle_t handle);
const char *mmap_assets_get_name(mmap_assets_handle_t handle, int index);
const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t handle, int index);
size_t    mmap_assets_get_size(mmap_assets_handle_t handle, int index);
esp_err_t mmap_assets_copy_mem(mmap_assets_handle_t handle, size_t offset,
                                void *buffer, size_t size);

#ifdef __cplusplus
}
#endif
