/*
 * mingw_compat.h — POSIX/GNU functions not available on MinGW-w64
 *
 * This header is auto-included (-include) for all source files on Windows.
 */
#pragma once

#ifdef _WIN32

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- strlcpy / strlcat (BSD) ---- */
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

/* ---- mkdir with mode -> _mkdir ---- */
#include <direct.h>
#ifdef mkdir
#undef mkdir
#endif
static inline int _mingw_mkdir(const char *path, int mode)
{
    (void)mode;
    return _mkdir(path);
}
#define mkdir(path, mode) _mingw_mkdir(path, mode)

/* ---- localtime_r → localtime_s ---- */
#ifndef localtime_r
#define localtime_r(timer, result) \
    (localtime_s((result), (const time_t *)(timer)) == 0 ? (result) : NULL)
#endif

/* ---- strftime → _mingw_strftime_utf8 (ACP→UTF-8 on Windows) ---- */
#if defined(PLATFORM_WINDOWS)
# include <time.h>
size_t __cdecl _mingw_strftime_utf8(char * __restrict__ buf, size_t maxsize,
                                     const char * __restrict__ format,
                                     const struct tm * __restrict__ tm);
# define strftime(buf, maxsize, format, tm) \
    _mingw_strftime_utf8(buf, maxsize, format, tm)
#endif

/* ---- open_memstream (GNU extension, not in MinGW CRT) ---- */
#ifndef open_memstream
FILE *open_memstream(char **bufp, size_t *sizep);
#endif

/* ---- stdout lvalue fix (MinGW: stdout is __acrt_iob_func(1), not an lvalue) ---- */
/*
 * ESP-IDF / POSIX code often assigns stdout = capture to redirect output.
 * On Windows/MinGW the macro is not an lvalue, so we replace it with a
 * modifiable pointer that mimics the original CRT handle.
 */
#include <stdio.h>
#undef stdout
extern FILE *crush_stdout;
#define stdout crush_stdout

/* ---- asprintf (GNU) ---- */
#ifndef asprintf
static inline int asprintf(char **strp, const char *fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (len < 0) { *strp = NULL; return -1; }
    *strp = (char *)malloc((size_t)len + 1);
    if (!*strp) { va_end(args); return -1; }
    vsnprintf(*strp, (size_t)len + 1, fmt, args);
    va_end(args);
    return len;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
