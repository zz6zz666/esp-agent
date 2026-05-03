/*
 * camera_stub.c — Camera frame simulation for desktop simulator
 *
 * Provides fake camera frames by reading images from disk when the Lua
 * camera module requests frames. Falls back to a generated test pattern.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "camera_stub";

/* Simulated camera frame buffer */
#define CAM_MAX_WIDTH  640
#define CAM_MAX_HEIGHT 480

static uint16_t s_frame[CAM_MAX_WIDTH * CAM_MAX_HEIGHT];
static int s_frame_w = 320;
static int s_frame_h = 240;
static bool s_initialized = false;

esp_err_t camera_stub_init(int width, int height)
{
    if (width > CAM_MAX_WIDTH || height > CAM_MAX_HEIGHT) {
        ESP_LOGE(TAG, "Frame size %dx%d exceeds max %dx%d", width, height,
                 CAM_MAX_WIDTH, CAM_MAX_HEIGHT);
        return ESP_ERR_INVALID_ARG;
    }
    s_frame_w = width;
    s_frame_h = height;
    s_initialized = true;

    /* Fill with a color test pattern (gradient) */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t r = (uint8_t)(x * 255 / width);
            uint8_t g = (uint8_t)(y * 255 / height);
            uint8_t b = (uint8_t)(128);
            s_frame[y * width + x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }

    ESP_LOGI(TAG, "Camera stub initialized: %dx%d", width, height);
    return ESP_OK;
}

esp_err_t camera_stub_get_frame(const uint16_t **out_frame, int *out_w, int *out_h)
{
    if (!s_initialized || !out_frame || !out_w || !out_h) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Regenerate pattern each frame for a "live" feel */
    static int counter = 0;
    counter++;
    uint8_t offset = (uint8_t)(counter % 255);
    for (int y = 0; y < s_frame_h; y++) {
        for (int x = 0; x < s_frame_w; x++) {
            uint8_t r = (uint8_t)(x * 255 / s_frame_w + offset);
            uint8_t g = (uint8_t)(y * 255 / s_frame_h);
            uint8_t b = (uint8_t)(128 + offset / 2);
            s_frame[y * s_frame_w + x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }

    *out_frame = s_frame;
    *out_w = s_frame_w;
    *out_h = s_frame_h;
    return ESP_OK;
}
