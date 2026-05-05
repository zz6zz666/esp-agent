/*
 * nvs_stub.c — NVS → JSON file mapping for desktop simulator
 *
 * Stores key-value pairs in a JSON file at ~/.esp-claw-sim/nvs.json
 */
#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <direct.h>
# define mkdir_mode(path, mode) _mkdir(path)
#else
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
# define mkdir_mode(path, mode) mkdir(path, mode)
#endif

#include "cJSON.h"

#define NVS_DIR  ".crush-claw"
#define NVS_FILE "nvs.json"

static char nvs_path[512] = {0};

static const char *get_home_dir(void)
{
#ifdef PLATFORM_WINDOWS
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEDRIVE"); /* fallback */
#else
    const char *home = getenv("HOME");
#endif
    if (!home) home = ".";
    return home;
}

static const char *get_nvs_path(void)
{
    if (nvs_path[0] == '\0') {
        snprintf(nvs_path, sizeof(nvs_path), "%s/%s/%s", get_home_dir(), NVS_DIR, NVS_FILE);
    }
    return nvs_path;
}

static void ensure_nvs_dir(void)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", get_home_dir(), NVS_DIR);
    mkdir_mode(dir, 0755);
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
