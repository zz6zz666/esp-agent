/* Stub for esp_jpeg_dec.h */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef void *jpeg_dec_handle_t;
typedef struct { int width, height; } jpeg_dec_header_info_t;

static inline esp_err_t jpeg_dec_new(const void *cfg, jpeg_dec_handle_t *h) { *h = NULL; return ESP_FAIL; }
static inline esp_err_t jpeg_dec_open(jpeg_dec_handle_t h, const uint8_t *d, size_t sz) { (void)h; (void)d; (void)sz; return ESP_FAIL; }
static inline esp_err_t jpeg_dec_get_info(jpeg_dec_handle_t h, jpeg_dec_header_info_t *i) { (void)h; (void)i; return ESP_FAIL; }
static inline esp_err_t jpeg_dec_get_output(jpeg_dec_handle_t h, uint8_t **d, size_t *sz) { (void)h; (void)d; (void)sz; return ESP_FAIL; }
static inline esp_err_t jpeg_dec_del(jpeg_dec_handle_t h) { (void)h; return ESP_OK; }
