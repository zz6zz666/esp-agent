/*
 * mbedtls_crypto.c — AES + MD5 stubs for desktop simulator
 *
 * Implements the mbedtls AES and MD5 API subsets used by cap_im_wechat.
 * Uses OpenSSL's libcrypto (already linked for WebSocket TLS).
 */
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include <openssl/aes.h>
#include <openssl/md5.h>
#include <stdlib.h>
#include <string.h>

void mbedtls_aes_init(mbedtls_aes_context *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_aes_free(mbedtls_aes_context *ctx)
{
    if (ctx) {
        free(ctx->rk);
        memset(ctx, 0, sizeof(*ctx));
    }
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
                            const unsigned char *key,
                            unsigned int keybits)
{
    if (!ctx || !key) return -1;
    ctx->nr = (int)(keybits / 32 + 6);
    ctx->rk = calloc(1, (size_t)(ctx->nr + 1) * 32);
    if (!ctx->rk) return -1;
    return AES_set_encrypt_key(key, (int)keybits, (AES_KEY *)ctx->rk) < 0 ? -1 : 0;
}

int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx,
                            const unsigned char *key,
                            unsigned int keybits)
{
    if (!ctx || !key) return -1;
    ctx->nr = (int)(keybits / 32 + 6);
    ctx->rk = calloc(1, (size_t)(ctx->nr + 1) * 32);
    if (!ctx->rk) return -1;
    return AES_set_decrypt_key(key, (int)keybits, (AES_KEY *)ctx->rk) < 0 ? -1 : 0;
}

int mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx,
                           int mode,
                           size_t length,
                           unsigned char iv[16],
                           const unsigned char *input,
                           unsigned char *output)
{
    if (!ctx || !ctx->rk || !iv || !input || !output) return -1;
    unsigned char iv_copy[16];
    memcpy(iv_copy, iv, 16);
    AES_cbc_encrypt(input, output, length, (const AES_KEY *)ctx->rk,
                    iv_copy, mode == 1 ? AES_ENCRYPT : AES_DECRYPT);
    return 0;
}

int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx,
                            int mode,
                            const unsigned char input[16],
                            unsigned char output[16])
{
    if (!ctx || !ctx->rk || !input || !output) return -1;
    if (mode == MBEDTLS_AES_ENCRYPT)
        AES_encrypt(input, output, (const AES_KEY *)ctx->rk);
    else
        AES_decrypt(input, output, (const AES_KEY *)ctx->rk);
    return 0;
}

/* ---- MD5 stubs (wrap OpenSSL) ---- */

void mbedtls_md5_init(mbedtls_md5_context *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_md5_free(mbedtls_md5_context *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_md5_clone(mbedtls_md5_context *dst, const mbedtls_md5_context *src)
{
    if (dst && src) memcpy(dst, src, sizeof(*dst));
}

int mbedtls_md5_starts(mbedtls_md5_context *ctx)
{
    if (!ctx) return -1;
    return MD5_Init((MD5_CTX *)ctx) == 1 ? 0 : -1;
}

int mbedtls_md5_update(mbedtls_md5_context *ctx,
                        const unsigned char *input, size_t ilen)
{
    if (!ctx || (!input && ilen > 0)) return -1;
    return MD5_Update((MD5_CTX *)ctx, input, ilen) == 1 ? 0 : -1;
}

int mbedtls_md5_finish(mbedtls_md5_context *ctx, unsigned char output[16])
{
    if (!ctx || !output) return -1;
    return MD5_Final(output, (MD5_CTX *)ctx) == 1 ? 0 : -1;
}

int mbedtls_md5(const unsigned char *input, size_t ilen,
                unsigned char output[16])
{
    if (!input || !output) return -1;
    return MD5(input, ilen, output) == NULL ? 0 : -1;
}
