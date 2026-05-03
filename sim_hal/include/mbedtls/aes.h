/*
 * mbedtls/aes.h — minimal stub for desktop simulator
 *
 * WeChat ilink protocol requires AES-256-CBC.  Provide the subset
 * of the mbedtls API that cap_im_wechat uses.  Implementation lives
 * in sim_hal/aes_stub.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int nr;
    uint32_t *rk;
    uint32_t buf[68];
} mbedtls_aes_context;

#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0

void mbedtls_aes_init(mbedtls_aes_context *ctx);
void mbedtls_aes_free(mbedtls_aes_context *ctx);
int  mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
                             const unsigned char *key,
                             unsigned int keybits);
int  mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx,
                             const unsigned char *key,
                             unsigned int keybits);
int  mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx,
                            int mode,
                            size_t length,
                            unsigned char iv[16],
                            const unsigned char *input,
                            unsigned char *output);
int  mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx,
                            int mode,
                            const unsigned char input[16],
                            unsigned char output[16]);

#ifdef __cplusplus
}
#endif
