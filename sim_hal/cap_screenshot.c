#include "cap_screenshot.h"
#include "claw_cap.h"
#include "display_screenshot.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "cap_screenshot";

const char *g_screenshots_dir = NULL;

static void cap_screenshot_build_path(char *path, size_t size,
                                       const char *filename,
                                       char *output, size_t output_size)
{
    if (!g_screenshots_dir || !g_screenshots_dir[0]) {
        snprintf(output, output_size, "screenshot failed: screenshots directory not configured");
        return;
    }

    if (filename && filename[0]) {
        snprintf(path, size, "%s/%s", g_screenshots_dir, filename);
    } else {
        time_t now = time(NULL);
        struct tm tm;
#ifdef PLATFORM_WINDOWS
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        snprintf(path, size, "%s/screenshot_%04d%02d%02d_%02d%02d%02d.jpg",
                 g_screenshots_dir,
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
}

static esp_err_t cap_screenshot_execute(const char *input_json,
                                         const claw_cap_call_context_t *ctx,
                                         char *output,
                                         size_t output_size)
{
    (void)ctx;

    char path[512];
    int quality = 85;
    const char *filename = NULL;

    if (input_json && input_json[0]) {
        cJSON *root = cJSON_Parse(input_json);
        if (root) {
            cJSON *f = cJSON_GetObjectItem(root, "filename");
            if (cJSON_IsString(f) && f->valuestring && f->valuestring[0]) {
                filename = f->valuestring;
            }
            cJSON *q = cJSON_GetObjectItem(root, "quality");
            if (cJSON_IsNumber(q)) {
                quality = q->valueint;
                if (quality < 1) quality = 1;
                if (quality > 100) quality = 100;
            }
            cJSON_Delete(root);
        }
    }

    cap_screenshot_build_path(path, sizeof(path), filename, output, output_size);
    if (output[0]) return ESP_FAIL;

    esp_err_t err = display_hal_screenshot(path, quality);
    if (err != ESP_OK) {
        snprintf(output, output_size, "screenshot failed: %s", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "screenshot saved to %s (quality=%d)", path, quality);
    ESP_LOGI(TAG, "screenshot saved: %s", path);
    return ESP_OK;
}

static const claw_cap_descriptor_t s_descriptors[] = {
    {
        .id = "screenshot",
        .name = "screenshot",
        .family = "display",
        .description = "Capture a JPEG screenshot of the current screen display and save to the screenshots directory.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"filename\":{\"type\":\"string\",\"description\":\"Optional output filename (defaults to timestamp-based name)\"},"
            "\"quality\":{\"type\":\"integer\",\"description\":\"JPEG quality 1-100 (default 85)\"}"
            "}"
            "}",
        .execute = cap_screenshot_execute,
    },
};

static const claw_cap_group_t s_group = {
    .group_id = "cap_screenshot",
    .descriptors = s_descriptors,
    .descriptor_count = sizeof(s_descriptors) / sizeof(s_descriptors[0]),
};

esp_err_t cap_screenshot_register_group(void)
{
    if (claw_cap_group_exists(s_group.group_id)) {
        return ESP_OK;
    }
    return claw_cap_register_group(&s_group);
}
