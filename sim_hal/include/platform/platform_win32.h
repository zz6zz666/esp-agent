/*
 * platform_win32.h — Windows (MinGW-w64 / MSVC) platform implementation
 */
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <direct.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- BSD string functions (not available on Windows) ---- */

#ifndef strlcpy
static inline size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t src_len = strlen(src);
    if (size) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}
#endif

#ifndef strlcat
static inline size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t dst_len = strlen(dst);
    if (dst_len >= size) return size + strlen(src);
    return dst_len + strlcpy(dst + dst_len, src, size - dst_len);
}
#endif

/* ---- Paths ---- */
#define PLATFORM_PATH_SEP   '\\'
#define PLATFORM_PATH_SEP_S "\\"
#define PLATFORM_MAX_PATH   MAX_PATH
#define PLATFORM_NULL_DEVICE "NUL"
#define PLATFORM_EXE_SUFFIX ".exe"

static inline const char* platform_get_home(void) {
    static char home[MAX_PATH] = {0};
    if (home[0]) return home;

    const char *env = getenv("USERPROFILE");
    if (env) {
        strncpy(home, env, MAX_PATH - 1);
        return home;
    }
    /* fallback: HOMEDRIVE\HOMEPATH */
    const char *drive = getenv("HOMEDRIVE");
    const char *path  = getenv("HOMEPATH");
    if (drive && path) {
        snprintf(home, MAX_PATH, "%s%s", drive, path);
        return home;
    }
    return "C:\\";
}

/* ---- Threading ---- */
typedef HANDLE platform_thread_t;
typedef CRITICAL_SECTION platform_mutex_t;

/* MSVC doesn't have a static initializer for CRITICAL_SECTION;
   MinGW maps PTHREAD_MUTEX_INITIALIZER but we use our own. */
#define PLATFORM_MUTEX_INIT {0}

static inline int platform_mutex_init(platform_mutex_t *m) {
    InitializeCriticalSection(m);
    return 0;
}
static inline int platform_mutex_lock(platform_mutex_t *m) {
    EnterCriticalSection(m);
    return 0;
}
static inline int platform_mutex_unlock(platform_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}
static inline int platform_mutex_destroy(platform_mutex_t *m) {
    DeleteCriticalSection(m);
    return 0;
}

/* Win32 thread wrapper */
typedef struct {
    void *(*fn)(void *);
    void *arg;
} platform_thread_data_t;

static inline DWORD WINAPI _platform_thread_proc(LPVOID param) {
    platform_thread_data_t *td = (platform_thread_data_t *)param;
    void *(*fn)(void *) = td->fn;
    void *arg = td->arg;
    /* td is stack-allocated in platform_thread_create; we need to free it.
       The caller uses a static storage pattern, so this is safe.
       See implementation note below. */
    fn(arg);
    return 0;
}

static inline int platform_thread_create(platform_thread_t *t,
                                          void *(*fn)(void*), void *arg) {
    /* Caller must ensure thread_data persists until the thread starts.
       For simple one-shot threads, use a static buffer. */
    static platform_thread_data_t td[8];
    static int td_index = 0;
    int idx = td_index++ % 8;
    td[idx].fn = fn;
    td[idx].arg = arg;

    *t = CreateThread(NULL, 0, _platform_thread_proc, &td[idx], 0, NULL);
    return (*t != NULL) ? 0 : -1;
}

static inline int platform_thread_detach(platform_thread_t t) {
    CloseHandle(t);
    return 0;
}

static inline void platform_sleep_ms(unsigned ms) {
    Sleep(ms);
}

/* ---- Timing (microseconds since boot, monotonic) ---- */
static inline int64_t platform_get_time_us(void) {
    static LARGE_INTEGER freq = {0};
    static int freq_init = 0;
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    if (freq.QuadPart == 0) return 0;
    return (int64_t)(counter.QuadPart * 1000000LL / freq.QuadPart);
}

/* ---- Filesystem ---- */
static inline int platform_mkdir(const char *path) {
    return _mkdir(path);
}

static inline int platform_unlink(const char *path) {
    return DeleteFileA(path) ? 0 : -1;
}

static inline int platform_file_exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static inline int platform_is_dir(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

/* ---- Process ---- */
static inline int platform_get_pid_impl(void) {
    return (int)GetCurrentProcessId();
}

/*
 * Windows directory iteration wrapper (replaces dirent)
 */
typedef struct {
    HANDLE              hFind;
    WIN32_FIND_DATAA    findData;
    int                 first;
    int                 valid;
} platform_dir_t;

static inline platform_dir_t* platform_opendir(const char *path) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    platform_dir_t *d = (platform_dir_t *)calloc(1, sizeof(platform_dir_t));
    if (!d) return NULL;

    d->hFind = FindFirstFileA(pattern, &d->findData);
    if (d->hFind == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = 1;
    d->valid = 1;
    return d;
}

static inline const char* platform_readdir(platform_dir_t *d) {
    if (!d || !d->valid) return NULL;

    if (!d->first) {
        if (!FindNextFileA(d->hFind, &d->findData)) {
            d->valid = 0;
            return NULL;
        }
    }
    d->first = 0;
    return d->findData.cFileName;
}

static inline int platform_readdir_isdir(platform_dir_t *d) {
    return (d->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static inline void platform_closedir(platform_dir_t *d) {
    if (d) {
        FindClose(d->hFind);
        free(d);
    }
}

/*
 * Get the directory containing the current executable.
 * Writes result to buf (size bytes). Returns buf on success, NULL on failure.
 */
static inline const char* platform_get_exe_dir(char *buf, size_t size) {
    char fullpath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, fullpath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return NULL;

    /* Find last backslash */
    char *slash = strrchr(fullpath, '\\');
    if (!slash) return NULL;

    size_t dir_len = (size_t)(slash - fullpath);
    if (dir_len >= size) return NULL;
    memcpy(buf, fullpath, dir_len);
    buf[dir_len] = '\0';
    return buf;
}

#ifdef __cplusplus
}
#endif
