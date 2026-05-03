/*
 * nvs_stub.c — NVS → JSON file mapping for desktop simulator
 *
 * Stores key-value pairs in a JSON file at ~/.esp-claw-sim/nvs.json
 */
#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cJSON.h"

#define NVS_DIR  ".esp-claw-sim"
#define NVS_FILE "nvs.json"

static char nvs_path[512] = {0};

static const char *get_nvs_path(void)
{
    if (nvs_path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(nvs_path, sizeof(nvs_path), "%s/%s/%s", home, NVS_DIR, NVS_FILE);
    }
    return nvs_path;
}

static void ensure_nvs_dir(void)
{
    const char *home = getenv("HOME");
    char dir[512];
    if (!home) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/%s", home, NVS_DIR);
    mkdir(dir, 0755);
}

esp_err_t nvs_flash_init(void)
{
    ensure_nvs_dir();

    FILE *fp = fopen(get_nvs_path(), "r");
    if (!fp) {
        /* Create empty NVS file */
        fp = fopen(get_nvs_path(), "w");
        if (!fp) return ESP_FAIL;
        fprintf(fp, "{}");
        fclose(fp);
        return ESP_OK;
    }
    fclose(fp);
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void)
{
    ensure_nvs_dir();
    FILE *fp = fopen(get_nvs_path(), "w");
    if (!fp) return ESP_FAIL;
    fprintf(fp, "{}");
    fclose(fp);
    return ESP_OK;
}
