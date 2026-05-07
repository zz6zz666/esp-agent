#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_hal_screenshot(const char *path, int quality);

#ifdef __cplusplus
}
#endif
