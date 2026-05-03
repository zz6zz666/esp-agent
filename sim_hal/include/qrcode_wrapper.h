/* Stub for qrcode_wrapper.h */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef void *qrcode_obj_handle_t;
static inline esp_err_t qrcode_obj_new(const void *cfg, qrcode_obj_handle_t *h) { *h = NULL; return ESP_OK; }
static inline esp_err_t qrcode_obj_del(qrcode_obj_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t qrcode_obj_set_text(qrcode_obj_handle_t h, const char *t) { (void)h; (void)t; return ESP_OK; }
static inline esp_err_t qrcode_obj_get_bitmap(qrcode_obj_handle_t h, uint8_t **d, int *w, int *h_) { (void)h; (void)d; (void)w; (void)h_; return ESP_FAIL; }
