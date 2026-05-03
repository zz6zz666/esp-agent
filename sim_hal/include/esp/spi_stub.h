/*
 * SPI stub for desktop simulator — all functions return ESP_OK.
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *spi_device_handle_t;

static inline esp_err_t spi_bus_initialize(int host, const void *bus_cfg, int dma_chan) {
    (void)host; (void)bus_cfg; (void)dma_chan; return ESP_OK;
}

#ifdef __cplusplus
}
#endif
