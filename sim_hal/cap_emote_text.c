#include "claw_cap.h"
#include "emote.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "cap_emote_text";

const char *g_emote_config_path = NULL;

static esp_err_t cap_emote_text_execute(const char *input_json,
                                         const claw_cap_call_context_t *ctx,
                                         char *output,
                                         size_t output_size)
{
    (void)ctx;

    if (!input_json || !input_json[0]) {
        snprintf(output, output_size, "missing input: provide {\"text\":\"...\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *t = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(t) || !t->valuestring) {
        cJSON_Delete(root);
        snprintf(output, output_size, "missing \"text\" field");
        return ESP_ERR_INVALID_ARG;
    }

    const char *text = t->valuestring;
    emote_set_network_msg(text);
    esp_err_t err = emote_set_network_status(true, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "emote_set_network_status failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "emote text set to: \"%s\"", text);

    if (g_emote_config_path && g_emote_config_path[0]) {
        FILE *fp = fopen(g_emote_config_path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long flen = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = NULL;
            if (flen > 0) {
                buf = malloc(flen + 1);
                if (buf) {
                    size_t rd = fread(buf, 1, flen, fp);
                    buf[rd] = '\0';
                }
            }
            fclose(fp);

            if (buf) {
                cJSON *cfg = cJSON_Parse(buf);
                free(buf);
                if (cfg) {
                    cJSON *display = cJSON_GetObjectItem(cfg, "display");
                    if (!display) {
                        display = cJSON_AddObjectToObject(cfg, "display");
                    }
                    if (display) {
                        cJSON *old = cJSON_DetachItemFromObject(display, "emote_text");
                        if (old) cJSON_Delete(old);
                        cJSON_AddStringToObject(display, "emote_text", text);

                        char *out = cJSON_Print(cfg);
                        if (out) {
                            FILE *wfp = fopen(g_emote_config_path, "wb");
                            if (wfp) {
                                fwrite(out, 1, strlen(out), wfp);
                                fclose(wfp);
                                ESP_LOGI(TAG, "config.json updated: display.emote_text");
                            } else {
                                ESP_LOGW(TAG, "failed to write config.json");
                            }
                            free(out);
                        }
                    }
                    cJSON_Delete(cfg);
                }
            }
        } else {
            ESP_LOGW(TAG, "cannot open config.json for update");
        }
    }

    cJSON_Delete(root);
    snprintf(output, output_size, "emote text updated to: %s", text);
    return ESP_OK;
}

static const claw_cap_descriptor_t s_descriptors[] = {
    {
        .id = "emote_set_text",
        .name = "emote_set_text",
        .family = "display",
        .description = "Change the text displayed on the emote screen and persist it to config.json.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json =
            "{"
            "\"type\":\"object\","
            "\"properties\":{"
            "\"text\":{\"type\":\"string\",\"description\":\"The text to display on the emote screen\"}"
            "},"
            "\"required\":[\"text\"]"
            "}",
        .execute = cap_emote_text_execute,
    },
};

static const claw_cap_group_t s_group = {
    .group_id = "cap_emote_text",
    .descriptors = s_descriptors,
    .descriptor_count = sizeof(s_descriptors) / sizeof(s_descriptors[0]),
};

esp_err_t cap_emote_text_register_group(void)
{
    if (claw_cap_group_exists(s_group.group_id)) {
        return ESP_OK;
    }
    return claw_cap_register_group(&s_group);
}
