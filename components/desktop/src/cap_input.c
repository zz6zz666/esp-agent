/*
 * cap_input.c — Desktop input capability group
 *
 * Exposes mouse and keyboard state to the LLM agent via the claw capability
 * system.  Two capabilities:
 *   - get_input_state  — poll current mouse + keyboard state (JSON)
 *   - wait_input       — block until next input event or timeout (JSON)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "claw_cap.h"
#include "cJSON.h"
#include "component_desktop.h"
#include "display_hal_input.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "cap_input";

/* ---- Input schema JSON strings ---- */

#define GET_INPUT_STATE_SCHEMA \
    "{\"type\":\"object\",\"properties\":{}," \
    "\"description\":\"Returns current mouse position/buttons, active " \
    "modifier keys, and currently pressed keyboard keys as JSON.\"}"

#define WAIT_INPUT_SCHEMA \
    "{\"type\":\"object\"," \
    "\"properties\":{" \
      "\"timeout_ms\":{" \
        "\"type\":\"integer\"," \
        "\"description\":\"Max wait time in milliseconds (default 5000)\"" \
      "}}," \
    "\"description\":\"Blocks until a mouse or keyboard event " \
    "occurs, or timeout expires. Returns the event as JSON.\"}"

/* ---- Execute callbacks ---- */

static esp_err_t get_input_state_exec(const char *input_json,
                                       const claw_cap_call_context_t *ctx,
                                       char *output, size_t output_size)
{
    (void)input_json;
    (void)ctx;

    int16_t mx, my;
    bool left, middle, right;
    int wheel;
    uint16_t mod;

    display_hal_get_mouse_state(&mx, &my, &left, &middle, &right, &wheel);
    mod = display_hal_get_modifiers();

    cJSON *root = cJSON_CreateObject();

    /* Mouse */
    cJSON *mouse_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(mouse_obj, "x", mx);
    cJSON_AddNumberToObject(mouse_obj, "y", my);
    cJSON_AddBoolToObject(mouse_obj, "left", left);
    cJSON_AddBoolToObject(mouse_obj, "middle", middle);
    cJSON_AddBoolToObject(mouse_obj, "right", right);
    cJSON_AddNumberToObject(mouse_obj, "wheel", wheel);
    cJSON_AddItemToObject(root, "mouse", mouse_obj);

    /* Modifiers */
    cJSON *mod_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(mod_obj, "ctrl",  (mod & 0x00C0) != 0);
    cJSON_AddBoolToObject(mod_obj, "shift", (mod & 0x0003) != 0);
    cJSON_AddBoolToObject(mod_obj, "alt",   (mod & 0x0300) != 0);
    cJSON_AddBoolToObject(mod_obj, "super", (mod & 0x0C00) != 0);
    cJSON_AddItemToObject(root, "modifiers", mod_obj);

    /* Pressed keys (list of scancode strings for known keys) */
    cJSON *keys_arr = cJSON_CreateArray();
    /* Report a sampling of commonly-interesting keys */
    static const struct {
        int sc; const char *name;
    } watch_keys[] = {
        { 4,  "a" }, { 5,  "b" }, { 6,  "c" }, { 7,  "d" }, { 8,  "e" },
        { 40, "enter" }, { 41, "escape" }, { 42, "backspace" },
        { 43, "tab" }, { 44, "space" },
        { 79, "right" }, { 80, "left" }, { 81, "down" }, { 82, "up" },
        { 225, "lctrl" }, { 229, "lshift" }, { 226, "lalt" },
    };
    int n_keys = sizeof(watch_keys) / sizeof(watch_keys[0]);
    for (int i = 0; i < n_keys; i++) {
        if (display_hal_is_key_down(watch_keys[i].sc)) {
            cJSON_AddItemToArray(keys_arr,
                cJSON_CreateString(watch_keys[i].name));
        }
    }
    cJSON_AddItemToObject(root, "keys_down", keys_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        snprintf(output, output_size, "%s", json_str);
        free(json_str);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wait_input_exec(const char *input_json,
                                  const claw_cap_call_context_t *ctx,
                                  char *output, size_t output_size)
{
    (void)ctx;

    int timeout_ms = 5000;
    if (input_json && input_json[0]) {
        cJSON *root = cJSON_Parse(input_json);
        if (root) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "timeout_ms");
            if (cJSON_IsNumber(t)) timeout_ms = t->valueint;
            cJSON_Delete(root);
        }
    }

    /* Poll with 20ms granularity until event or timeout */
    int elapsed = 0;
    input_event_t evt;
    bool got = false;
    while (elapsed < timeout_ms) {
        got = display_hal_pop_input_event(&evt);
        if (got) break;
        usleep(20000);
        elapsed += 20;
    }

    if (!got) {
        snprintf(output, output_size,
                 "{\"event\":\"timeout\",\"elapsed_ms\":%d}", elapsed);
        return ESP_OK;
    }

    const char *type_str = "unknown";
    switch (evt.type) {
    case INPUT_EVENT_KEY_DOWN:     type_str = "key_down"; break;
    case INPUT_EVENT_KEY_UP:       type_str = "key_up"; break;
    case INPUT_EVENT_TEXT:         type_str = "text"; break;
    case INPUT_EVENT_MOUSE_DOWN:   type_str = "mouse_down"; break;
    case INPUT_EVENT_MOUSE_UP:     type_str = "mouse_up"; break;
    case INPUT_EVENT_MOUSE_WHEEL:  type_str = "mouse_wheel"; break;
    case INPUT_EVENT_WINDOW_RESIZED: type_str = "window_resized"; break;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", type_str);
    cJSON_AddNumberToObject(root, "x", evt.x);
    cJSON_AddNumberToObject(root, "y", evt.y);
    cJSON_AddNumberToObject(root, "key", evt.key);
    cJSON_AddNumberToObject(root, "button", evt.button);
    cJSON_AddBoolToObject(root, "ctrl",  (evt.mod & 0x00C0) != 0);
    cJSON_AddBoolToObject(root, "shift", (evt.mod & 0x0003) != 0);
    cJSON_AddBoolToObject(root, "alt",   (evt.mod & 0x0300) != 0);

    if (evt.type == INPUT_EVENT_TEXT && evt.text[0]) {
        cJSON_AddStringToObject(root, "text", evt.text);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        snprintf(output, output_size, "%s", json_str);
        free(json_str);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- Descriptors and group ---- */

static claw_cap_descriptor_t s_cap_descriptors[] = {
    {
        .id   = "get_input_state",
        .name = "get_input_state",
        .family = "input",
        .description = "Get current mouse position/buttons and active keyboard "
                       "modifiers/pressed keys as JSON.  Use this to check "
                       "what the user is doing with mouse and keyboard.",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = GET_INPUT_STATE_SCHEMA,
        .execute = get_input_state_exec,
    },
    {
        .id   = "wait_input",
        .name = "wait_input",
        .family = "input",
        .description = "Wait for the next mouse or keyboard input event. "
                       "Returns the event as JSON with type, position, and "
                       "key/button details. Accepts optional timeout_ms "
                       "(default 5000).",
        .kind = CLAW_CAP_KIND_CALLABLE,
        .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = WAIT_INPUT_SCHEMA,
        .execute = wait_input_exec,
    },
};

static const claw_cap_group_t s_cap_group = {
    .group_id         = CLAW_CAP_GROUP_DESKTOP_PREFIX "input",
    .plugin_name      = CLAW_DESKTOP_PLUGIN_PREFIX "input",
    .version          = CLAW_DESKTOP_COMPONENT_VERSION,
    .descriptors      = s_cap_descriptors,
    .descriptor_count = sizeof(s_cap_descriptors) / sizeof(s_cap_descriptors[0]),
};

esp_err_t cap_input_register_group(void)
{
    if (claw_cap_group_exists(s_cap_group.group_id)) {
        return ESP_OK;
    }
    esp_err_t err = claw_cap_register_group(&s_cap_group);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s: %s",
                 s_cap_group.group_id, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Registered capability group: %s (%zu caps)",
             s_cap_group.group_id, s_cap_group.descriptor_count);
    return ESP_OK;
}
