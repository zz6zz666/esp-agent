/*
 * platform_posix.h — POSIX (Linux/macOS) platform implementation
 */
#pragma once

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Paths ---- */
#define PLATFORM_PATH_SEP   '/'
#define PLATFORM_PATH_SEP_S "/"
#define PLATFORM_MAX_PATH   512
#define PLATFORM_NULL_DEVICE "/dev/null"
#define PLATFORM_EXE_SUFFIX ""

#if !defined(PATH_MAX)
#define PATH_MAX 512
#endif

static inline const char* platform_get_home(void) {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

/* ---- Threading ---- */
typedef pthread_t       platform_thread_t;
typedef pthread_mutex_t platform_mutex_t;

#define PLATFORM_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER

static inline int platform_mutex_lock(platform_mutex_t *m) {
    return pthread_mutex_lock(m);
}
static inline int platform_mutex_unlock(platform_mutex_t *m) {
    return pthread_mutex_unlock(m);
}
static inline int platform_mutex_init(platform_mutex_t *m) {
    return pthread_mutex_init(m, NULL);
}
static inline int platform_mutex_destroy(platform_mutex_t *m) {
    return pthread_mutex_destroy(m);
}

static inline int platform_thread_create(platform_thread_t *t,
                                          void *(*fn)(void*), void *arg) {
    return pthread_create(t, NULL, fn, arg);
}
static inline int platform_thread_detach(platform_thread_t t) {
    return pthread_detach(t);
}
static inline void platform_sleep_ms(unsigned ms) {
    usleep(ms * 1000);
}

/* ---- Timing (microseconds since boot, monotonic) ---- */
static inline int64_t platform_get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* ---- Filesystem ---- */
static inline int platform_mkdir(const char *path) {
    return mkdir(path, 0755);
}
static inline int platform_unlink(const char *path) {
    return unlink(path);
}
static inline int platform_file_exists(const char *path) {
    return access(path, F_OK) == 0;
}
static inline int platform_is_dir(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* ---- Process ---- */
static inline int platform_get_pid_impl(void) {
    return (int)getpid();
}

#ifdef __cplusplus
}
#endif
