/*
 * base64.c — minimal base64 encode/decode for desktop simulator
 * Replaces mbedtls base64 functions.
 */
#include "mbedtls/base64.h"
#include <string.h>

int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j;
    size_t needed = ((slen + 2) / 3) * 4 + 1;

    if (dlen < needed) { if (olen) *olen = needed; return -1; }

    for (i = 0, j = 0; i < slen; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < slen) v |= (uint32_t)src[i + 1] << 8;
        if (i + 2 < slen) v |= (uint32_t)src[i + 2];
        dst[j++] = b64[(v >> 18) & 0x3F];
        dst[j++] = b64[(v >> 12) & 0x3F];
        dst[j++] = (i + 1 < slen) ? b64[(v >> 6) & 0x3F] : '=';
        dst[j++] = (i + 2 < slen) ? b64[v & 0x3F] : '=';
    }
    dst[j] = '\0';
    if (olen) *olen = j;
    return 0;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen)
{
    size_t i, j;
    int vals[4];
    int n;

    if (slen % 4 != 0) return -1;

    for (i = 0, j = 0; i < slen; i += 4) {
        for (n = 0; n < 4; n++) {
            if (src[i + n] == '=') {
                vals[n] = 0;
            } else {
                vals[n] = b64_val(src[i + n]);
                if (vals[n] < 0) return -1;
            }
        }
        if (j < dlen) dst[j++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
        if (j < dlen && src[i + 2] != '=') dst[j++] = (unsigned char)((vals[1] << 4) | (vals[2] >> 2));
        if (j < dlen && src[i + 3] != '=') dst[j++] = (unsigned char)((vals[2] << 6) | vals[3]);
    }
    if (olen) *olen = j;
    return 0;
}
