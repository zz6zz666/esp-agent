/*
 * nvs.c — NVS stub for desktop. Reads/writes from a JSON file.
 */
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static cJSON *s_nvs_json = NULL;
static const char *s_nvs_path = NULL;

static void nvs_ensure_loaded(void)
{
    if (s_nvs_json) return;

    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    char path[512];
    snprintf(path, sizeof(path), "%s/.esp-claw-sim/nvs.json", home);
    s_nvs_path = strdup(path);

    FILE *fp = fopen(path, "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *data = malloc(sz + 1);
        if (data) {
            fread(data, 1, sz, fp);
            data[sz] = '\0';
            s_nvs_json = cJSON_Parse(data);
            free(data);
        }
        fclose(fp);
    }
    if (!s_nvs_json) {
        s_nvs_json = cJSON_CreateObject();
    }
}

static void nvs_save(void)
{
    if (!s_nvs_json || !s_nvs_path) return;
    char *data = cJSON_Print(s_nvs_json);
    if (data) {
        FILE *fp = fopen(s_nvs_path, "w");
        if (fp) {
            fputs(data, fp);
            fclose(fp);
        }
        free(data);
    }
}

esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *out_handle)
{
    (void)name;
    (void)open_mode;
    nvs_ensure_loaded();
    *out_handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length)
{
    (void)handle;
    nvs_ensure_loaded();
    cJSON *item = cJSON_GetObjectItem(s_nvs_json, key);
    if (!item || !cJSON_IsString(item)) return ESP_ERR_NOT_FOUND;
    size_t needed = strlen(item->valuestring) + 1;
    if (*length < needed) {
        *length = needed;
        return ESP_ERR_NO_MEM;
    }
    strncpy(out_value, item->valuestring, *length);
    *length = needed;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle;
    nvs_ensure_loaded();
    cJSON_DeleteItemFromObject(s_nvs_json, key);
    cJSON_AddStringToObject(s_nvs_json, key, value);
    nvs_save();
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    nvs_ensure_loaded();
    cJSON *item = cJSON_GetObjectItem(s_nvs_json, key);
    if (!item || !cJSON_IsString(item)) return ESP_ERR_NOT_FOUND;
    /* Store blobs as base64-encoded strings for simplicity */
    size_t slen = strlen(item->valuestring);
    if (*length < slen) { *length = slen; return ESP_ERR_NO_MEM; }
    memcpy(out_value, item->valuestring, slen);
    *length = slen;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle;
    nvs_ensure_loaded();
    char *str = malloc(length + 1);
    if (!str) return ESP_ERR_NO_MEM;
    memcpy(str, value, length);
    str[length] = '\0';
    cJSON_DeleteItemFromObject(s_nvs_json, key);
    cJSON_AddStringToObject(s_nvs_json, key, str);
    free(str);
    nvs_save();
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    nvs_save();
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    nvs_ensure_loaded();
    cJSON_DeleteItemFromObject(s_nvs_json, key);
    nvs_save();
    return ESP_OK;
}
