/*
 * nvs.h stub — NVS key-value read/write for desktop simulator.
 * Backed by the JSON file created via nvs_flash.h.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;
typedef uint32_t nvs_handle;

#define NVS_READONLY  1
#define NVS_READWRITE 2
#define NVS_KEY_NAME_MAX_SIZE 16
#define ESP_ERR_NVS_NOT_FOUND  0x1104

esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);

#ifdef __cplusplus
}
#endif
