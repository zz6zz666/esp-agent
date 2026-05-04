/*
 * bsd_strings.h — BSD/POSIX string functions not available on MinGW-w64
 */
#pragma once
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(strlcpy)
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

#if defined(_WIN32) && !defined(strlcat)
static inline size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t dst_len = strlen(dst);
    if (dst_len >= size) return size + strlen(src);
    return dst_len + strlcpy(dst + dst_len, src, size - dst_len);
}
#endif

#ifdef __cplusplus
}
#endif
