/*
 * esp_mmap_assets_stub.c — File-based asset loading for desktop simulator.
 *
 * On real hardware mmap_assets memory-maps a flash partition.  Here we read
 * individual files from a directory — the partition_label maps to a path.
 */
#include "esp_mmap_assets.h"
#include "esp_log.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "mmap_assets";

#define MAX_ASSETS 128

typedef struct {
    char     *name;
    uint8_t  *data;
    size_t    size;
} asset_entry_t;

struct mmap_assets_s {
    asset_entry_t entries[MAX_ASSETS];
    int count;
    char *base_path;
};

esp_err_t mmap_assets_new(const mmap_assets_config_t *cfg,
                           mmap_assets_handle_t *out_handle)
{
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;

    mmap_assets_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;

    /* Use path if provided, otherwise treat partition_label as dir name */
    const char *dir = cfg->path ? cfg->path : cfg->partition_label;
    if (!dir) {
        free(h);
        return ESP_ERR_INVALID_ARG;
    }

    h->base_path = strdup(dir);
    if (!h->base_path) {
        free(h);
        return ESP_ERR_NO_MEM;
    }

    /* Read all files from the directory */
    DIR *d = opendir(dir);
    if (!d) {
        ESP_LOGW(TAG, "Cannot open asset dir: %s", dir);
        *out_handle = h;
        return ESP_OK;  /* empty — callers can still use it */
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && h->count < MAX_ASSETS) {
        /* d_type/DT_DIR is Linux-specific; use stat() for portability */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) continue;

        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        rewind(fp);
        if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); continue; }

        uint8_t *data = malloc((size_t)sz);
        if (!data || fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
            free(data);
            fclose(fp);
            continue;
        }
        fclose(fp);

        h->entries[h->count].name = strdup(entry->d_name);
        h->entries[h->count].data = data;
        h->entries[h->count].size = (size_t)sz;
        h->count++;
    }
    closedir(d);
    ESP_LOGI(TAG, "Loaded %d assets from %s", h->count, dir);

    *out_handle = h;
    return ESP_OK;
}

void mmap_assets_del(mmap_assets_handle_t h)
{
    if (!h) return;
    for (int i = 0; i < h->count; i++) {
        free(h->entries[i].name);
        free(h->entries[i].data);
    }
    free(h->base_path);
    free(h);
}

int mmap_assets_get_stored_files(mmap_assets_handle_t h) { return h ? h->count : 0; }

const char *mmap_assets_get_name(mmap_assets_handle_t h, int idx)
{
    if (!h || idx < 0 || idx >= h->count) return NULL;
    return h->entries[idx].name;
}

const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t h, int idx)
{
    if (!h || idx < 0 || idx >= h->count) return NULL;
    return h->entries[idx].data;
}

size_t mmap_assets_get_size(mmap_assets_handle_t h, int idx)
{
    if (!h || idx < 0 || idx >= h->count) return 0;
    return h->entries[idx].size;
}

esp_err_t mmap_assets_copy_mem(mmap_assets_handle_t h, size_t offset,
                                void *buffer, size_t size)
{
    /* offset is interpreted as file index * 1000 + internal offset */
    int idx = (int)(offset / 1000);
    size_t off = offset % 1000;
    if (!h || idx < 0 || idx >= h->count) return ESP_ERR_INVALID_ARG;
    if (off + size > h->entries[idx].size) return ESP_ERR_INVALID_ARG;
    memcpy(buffer, h->entries[idx].data + off, size);
    return ESP_OK;
}
