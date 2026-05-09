/*
 * ESP-IDF esp_log.h stub for desktop simulator
 */
#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

#ifdef PLATFORM_ANDROID
#include <android/log.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

/* Global log file state — defined in sim_hal/esp_log.c.
   Declared extern here so every translation unit shares one copy. */
extern FILE           *g_esp_log_file;
extern pthread_mutex_t g_esp_log_mutex;

/* Set or rotate the shared log file (thread-safe).
   Defined in sim_hal/esp_log.c. */
void esp_log_set_log_file(const char *path);

/* Internal write helper — inlined so ESP_LOGX macros are zero-overhead. */
static inline void _esp_log_write(const char *level, const char *tag, const char *fmt, ...)
{
    struct timeval tv;
    struct tm tm_info;
    char time_buf[32];
    va_list args, copy;
    const char *color = "";

    /* ANSI color for log level */
    switch (level[0]) {
    case 'E': color = "\033[31m"; break;  /* red */
    case 'W': color = "\033[33m"; break;  /* yellow */
    case 'I': color = "\033[32m"; break;  /* green */
    case 'D': color = "\033[37m"; break;  /* white (bright) */
    case 'V': color = "\033[0m";  break;  /* reset */
    default:  break;
    }

    gettimeofday(&tv, NULL);
    {
        time_t t = (time_t)tv.tv_sec;
#if defined(_WIN32)
        localtime_s(&tm_info, &t);
#else
        localtime_r(&t, &tm_info);
#endif
    }
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    int msec = (int)(tv.tv_usec / 1000);

    va_start(args, fmt);
    va_copy(copy, args);

    pthread_mutex_lock(&g_esp_log_mutex);

#ifdef PLATFORM_ANDROID
    {
        int android_prio;
        switch (level[0]) {
        case 'E': android_prio = ANDROID_LOG_ERROR; break;
        case 'W': android_prio = ANDROID_LOG_WARN;  break;
        case 'I': android_prio = ANDROID_LOG_INFO;  break;
        case 'D': android_prio = ANDROID_LOG_DEBUG; break;
        default:  android_prio = ANDROID_LOG_VERBOSE; break;
        }
        __android_log_vprint(android_prio, tag, fmt, args);
    }
#else
    /* Write to stderr (terminal or /dev/null in daemon mode) */
    fprintf(stderr, "%s[%s.%03d] [%s] [%s] ", color, time_buf, msec, level, tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\033[0m\n");
    fflush(stderr);
#endif
    va_end(args);

    /* Also write to log file if configured */
    if (g_esp_log_file) {
        fprintf(g_esp_log_file, "[%s.%03d] [%s] [%s] ", time_buf, msec, level, tag);
        vfprintf(g_esp_log_file, fmt, copy);
        fprintf(g_esp_log_file, "\n");
        fflush(g_esp_log_file);
    }
    va_end(copy);

    pthread_mutex_unlock(&g_esp_log_mutex);
}

#define ESP_LOGE(tag, fmt, ...)  _esp_log_write("E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...)  _esp_log_write("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...)  _esp_log_write("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)  _esp_log_write("D", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...)  _esp_log_write("V", tag, fmt, ##__VA_ARGS__)
#define ESP_LOG_LEVEL(level, tag, fmt, ...) _esp_log_write( \
    (level) == ESP_LOG_ERROR ? "E" : \
    (level) == ESP_LOG_WARN  ? "W" : \
    (level) == ESP_LOG_INFO  ? "I" : \
    (level) == ESP_LOG_DEBUG ? "D" : "V", \
    tag, fmt, ##__VA_ARGS__)

static inline void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

#ifdef __cplusplus
}
#endif
