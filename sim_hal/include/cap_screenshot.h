#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *g_screenshots_dir;

esp_err_t cap_screenshot_register_group(void);

#ifdef __cplusplus
}
#endif
