/*
 * mbedtls/base64.h minimal stub for desktop simulator.
 * Provides base64 encode/decode using a simple internal implementation.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL  -0x002A

#ifdef __cplusplus
extern "C" {
#endif

int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);
int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen);

#ifdef __cplusplus
}
#endif
