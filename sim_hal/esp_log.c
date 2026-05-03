/*
 * esp_log.c — shared log state for the desktop simulator
 *
 * g_esp_log_file and g_esp_log_mutex are declared extern in
 * sim_hal/include/esp/esp_log.h.  This single definition is linked
 * into every translation unit so that esp_log_set_log_file() in one
 * TU affects ESP_LOGX calls in all TUs.
 */
#include "esp/esp_log.h"

FILE           *g_esp_log_file  = NULL;
pthread_mutex_t g_esp_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void esp_log_set_log_file(const char *path)
{
    pthread_mutex_lock(&g_esp_log_mutex);
    if (g_esp_log_file) {
        fclose(g_esp_log_file);
        g_esp_log_file = NULL;
    }
    if (path) {
        g_esp_log_file = fopen(path, "a");
    }
    pthread_mutex_unlock(&g_esp_log_mutex);
}
