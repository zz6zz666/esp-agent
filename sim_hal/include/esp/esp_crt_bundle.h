/*
 * ESP-IDF esp_crt_bundle.h stub for desktop simulator
 * On desktop, libcurl handles TLS cert verification using the system CA bundle.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* This is a no-op on desktop — libcurl uses system CA certs */
static inline void esp_crt_bundle_attach(void *handle) { (void)handle; }

#ifdef __cplusplus
}
#endif
