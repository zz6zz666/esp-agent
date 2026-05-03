/*
 * esp_lcd_panel_io.h — panel IO bus stub
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd,
                                     const void *param, size_t size);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                     const void *color, size_t size);

#ifdef __cplusplus
}
#endif
