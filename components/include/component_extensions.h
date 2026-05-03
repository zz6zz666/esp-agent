/*
 * component_extensions.h — ESP32-compatible component namespace declaration
 *
 * This header establishes naming conventions for extension components
 * that compile on BOTH real ESP32 hardware AND the Linux desktop simulator.
 *
 * Extension components in ../ (siblings of desktop/) follow these rules:
 *   1. Use ESP-IDF component conventions (include/, src/, CMakeLists.txt)
 *   2. Register capabilities via claw_cap_register_group()
 *   3. Use #ifndef SIMULATOR_BUILD guards for hardware-specific code paths
 *   4. If a component has NO hardware dependency, it compiles identically
 *      on both targets
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Capability group ID prefix for custom extension components */
#define CLAW_CAP_GROUP_EXTENSION_PREFIX  "ext_"

/* Plugin name prefix for extension capability groups */
#define CLAW_EXTENSION_PLUGIN_PREFIX     "ext_"

/* Version string for extension component packages */
#define CLAW_EXTENSION_COMPONENT_VERSION "1.0.0"

#ifdef __cplusplus
}
#endif
