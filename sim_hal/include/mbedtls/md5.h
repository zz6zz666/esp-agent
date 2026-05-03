/*
 * mbedtls/md5.h — minimal stub for desktop simulator
 *
 * WeChat ilink protocol requires MD5.  Wraps OpenSSL's MD5.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t total[2];
    uint32_t state[4];
    unsigned char buffer[64];
} mbedtls_md5_context;

void mbedtls_md5_init(mbedtls_md5_context *ctx);
void mbedtls_md5_free(mbedtls_md5_context *ctx);
void mbedtls_md5_clone(mbedtls_md5_context *dst, const mbedtls_md5_context *src);
int  mbedtls_md5_starts(mbedtls_md5_context *ctx);
int  mbedtls_md5_update(mbedtls_md5_context *ctx,
                         const unsigned char *input, size_t ilen);
int  mbedtls_md5_finish(mbedtls_md5_context *ctx, unsigned char output[16]);

int  mbedtls_md5(const unsigned char *input, size_t ilen,
                  unsigned char output[16]);

#ifdef __cplusplus
}
#endif
