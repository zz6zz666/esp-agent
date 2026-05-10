/*
 * android_hal_stubs.c — Android platform stubs for sim_hal layer only
 *
 * Provides implementations of functions declared in sim_hal headers
 * that on desktop are implemented by display_sdl2.c etc.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "display_screenshot.h"
#include "display_hal_android.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "android_hal";

/* ---- aligned_alloc (not in NDK) ---- */
void *aligned_alloc(size_t alignment, size_t size)
{
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) == 0) return ptr;
    return NULL;
}

/* ---- ESP LCD touch stubs ---- */
static esp_lcd_touch_dev_t s_touch_dev = {0};
static esp_lcd_touch_handle_t s_touch_handle = &s_touch_dev;

esp_lcd_touch_handle_t esp_lcd_touch_init_sdl(void)
{
    return s_touch_handle;
}

void esp_lcd_touch_feed_sdl(int16_t x, int16_t y, bool pressed)
{
    s_touch_handle->last_point.x = x;
    s_touch_handle->last_point.y = y;
    s_touch_handle->last_point.strength = pressed ? 128 : 0;
    s_touch_handle->last_point.track_id = 0;
    __atomic_store_n(&s_touch_handle->has_data, pressed, __ATOMIC_RELEASE);
    if (!pressed) s_touch_handle->last_read_ms = 0;
}

esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(
    esp_lcd_touch_handle_t tp, void (*cb)(esp_lcd_touch_handle_t), void *arg)
{ (void)tp; (void)cb; (void)arg; return ESP_OK; }

esp_err_t esp_lcd_touch_register_interrupt_callback(
    esp_lcd_touch_handle_t tp, void (*cb)(esp_lcd_touch_handle_t))
{ (void)tp; (void)cb; return ESP_OK; }

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp)
{ (void)tp; return ESP_OK; }

esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,
    esp_lcd_touch_point_data_t *out, uint8_t *count, uint16_t max)
{
    if (!tp || max == 0) { *count = 0; return ESP_OK; }
    bool has = __atomic_load_n(&tp->has_data, __ATOMIC_ACQUIRE);
    if (!has) { *count = 0; return ESP_OK; }
    *count = 1;
    out[0] = tp->last_point;
    __atomic_store_n(&tp->has_data, false, __ATOMIC_RELEASE);
    return ESP_OK;
}

/* ---- Display screenshot ---- */

esp_err_t display_hal_screenshot(const char *path, int quality)
{
    uint8_t *rgb = NULL;
    size_t   rgb_sz;
    int      w, h;
    int      result;
    esp_err_t err = ESP_OK;

    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_display_ctx.pixels) {
        ESP_LOGW(TAG, "screenshot: no pixel buffer");
        return ESP_ERR_INVALID_STATE;
    }

    w = g_display_ctx.width;
    h = g_display_ctx.height;
    if (w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (quality < 1)  quality = 1;
    if (quality > 100) quality = 100;

    rgb_sz = (size_t)w * h * 3;
    rgb = malloc(rgb_sz);
    if (!rgb) {
        return ESP_ERR_NO_MEM;
    }

    for (int y = 0; y < h; y++) {
        uint16_t *src = g_display_ctx.pixels + (size_t)y * w;
        uint8_t  *dst = rgb + (size_t)y * w * 3;

        for (int x = 0; x < w; x++) {
            uint16_t c = src[x];
            dst[x * 3 + 0] = (uint8_t)((c >> 8) & 0xF8);  /* R */
            dst[x * 3 + 1] = (uint8_t)((c >> 3) & 0xFC);  /* G */
            dst[x * 3 + 2] = (uint8_t)((c << 3) & 0xF8);  /* B */
        }
    }

    result = stbi_write_jpg(path, w, h, 3, rgb, quality);
    free(rgb);

    if (!result) {
        ESP_LOGE(TAG, "screenshot: stbi_write_jpg failed for %s", path);
        err = ESP_FAIL;
    }

    return err;
}
