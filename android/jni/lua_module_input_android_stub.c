/*
 * lua_module_input_android_stub.c — Android stub for lua_module_input
 *
 * The real lua_module_input uses SDL2 keyboard events and is desktop-only.
 * On Android, provide a no-op registration stub.
 */
#include "esp_err.h"

int lua_module_input_register(void) { return 0; }
