/*
 * ESP-IDF esp_system.h stub for desktop simulator
 */
#pragma once

#include "esp_err.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void esp_restart(void)
{
    fprintf(stderr, "esp_restart() called — exiting simulator\n");
    exit(0);
}

#ifdef __cplusplus
}
#endif
