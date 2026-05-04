/*
 * component_desktop.h — Desktop component namespace declaration
 *
 * This header establishes the naming conventions and capability group
 * identifiers for all Linux desktop-specific extensions.
 *
 * All desktop components MUST:
 *   1. Use the CLAW_CAP_GROUP_DESKTOP_PREFIX for capability group IDs
 *   2. Set CLAW_CAP_FLAG_RESTRICTED on any capability that touches host OS
 *   3. Gate registration on #ifdef SIMULATOR_BUILD (never available on ESP32)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Capability group ID prefix for all desktop-only capability groups.
 * Example: "desktop_exec", "desktop_notify", "desktop_fs" */
#define CLAW_CAP_GROUP_DESKTOP_PREFIX  "desktop_"

/* Plugin name prefix for desktop capability groups */
#define CLAW_DESKTOP_PLUGIN_PREFIX     "desktop_"

/* Version string for desktop component packages */
#define CLAW_DESKTOP_COMPONENT_VERSION "1.0.0"

/* Register the desktop input Lua module (keyboard + mouse for scripts). */
esp_err_t lua_module_input_register(void);

#ifdef __cplusplus
}
#endif
