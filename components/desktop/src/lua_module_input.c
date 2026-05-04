/*
 * lua_module_input.c — Desktop keyboard/mouse Lua module
 *
 * Provides game-style input for interactive Lua scripts:
 *   - mouse_pos()       → x, y
 *   - mouse_down(btn)   → bool
 *   - mouse_wheel()     → int (accumulated scroll)
 *   - key_down(name|sc) → bool
 *   - modifiers()       → int (bitmask)
 *   - wait_event(ms)    → {type=..., ...}  (blocking)
 *   - poll_events()     → iterator over pending events
 *
 * Intended as the desktop equivalent of touch/button/knob on ESP32.
 */
#include "lua_module_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "cap_lua.h"
#include "display_hal_input.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lauxlib.h"

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define sleep_ms(ms) Sleep(ms)
#else
# include <unistd.h>
# define sleep_ms(ms) usleep((ms) * 1000)
#endif

static const char *TAG = "lua_input";

/* ---- mouse_pos() → x, y ---- */
static int l_input_mouse_pos(lua_State *L)
{
    int16_t x, y;
    display_hal_get_mouse_state(&x, &y, NULL, NULL, NULL, NULL);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

/* ---- mouse_down(button) → bool ---- */
static int l_input_mouse_down(lua_State *L)
{
    int btn = (int)luaL_optinteger(L, 1, 1);
    bool left, middle, right;
    display_hal_get_mouse_state(NULL, NULL, &left, &middle, &right, NULL);
    switch (btn) {
    case 1:  lua_pushboolean(L, left);  break;
    case 2:  lua_pushboolean(L, middle); break;
    case 3:  lua_pushboolean(L, right); break;
    default: lua_pushboolean(L, 0);     break;
    }
    return 1;
}

/* ---- mouse_wheel() → int ---- */
static int l_input_mouse_wheel(lua_State *L)
{
    int wheel = 0;
    display_hal_get_mouse_state(NULL, NULL, NULL, NULL, NULL, &wheel);
    lua_pushinteger(L, wheel);
    return 1;
}

/* ---- key_down(name_or_scancode) → bool ---- */
static int l_input_key_down(lua_State *L)
{
    int32_t sc;
    if (lua_isinteger(L, 1)) {
        sc = (int32_t)lua_tointeger(L, 1);
    } else if (lua_isstring(L, 1)) {
        const char *name = lua_tostring(L, 1);
        /* Try to find scancode by name */
        SDL_Scancode s = SDL_GetScancodeFromName(name);
        if (s == SDL_SCANCODE_UNKNOWN) {
            lua_pushboolean(L, 0);
            return 1;
        }
        sc = (int32_t)s;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, display_hal_is_key_down(sc));
    return 1;
}

/* ---- modifiers() → mask ---- */
static int l_input_modifiers(lua_State *L)
{
    lua_pushinteger(L, display_hal_get_modifiers());
    return 1;
}

/* ---- key_name(scancode) → string ---- */
static int l_input_key_name(lua_State *L)
{
    int32_t sc = (int32_t)luaL_checkinteger(L, 1);
    const char *name = display_hal_get_scancode_name(sc);
    lua_pushstring(L, name);
    return 1;
}

/* ---- wait_event(ms) → {type=..., ...} or nil on timeout ---- */
static int l_input_wait_event(lua_State *L)
{
    int timeout_ms = (int)luaL_optinteger(L, 1, -1);
    int64_t start = (int64_t)clock() * 1000 / CLOCKS_PER_SEC;

    while (timeout_ms < 0 ||
           (int)((int64_t)clock() * 1000 / CLOCKS_PER_SEC - start) < timeout_ms) {
        input_event_t ev;
        if (display_hal_pop_input_event(&ev)) {
            /* Consume and return single event */
            lua_newtable(L);

            switch (ev.type) {
            case INPUT_EVENT_KEY_DOWN:
                lua_pushstring(L, "key_down");
                lua_setfield(L, -2, "type");
                lua_pushstring(L, display_hal_get_scancode_name(ev.key));
                lua_setfield(L, -2, "key");
                lua_pushinteger(L, ev.key);
                lua_setfield(L, -2, "scancode");
                lua_pushinteger(L, ev.mod);
                lua_setfield(L, -2, "mod");
                break;

            case INPUT_EVENT_KEY_UP:
                lua_pushstring(L, "key_up");
                lua_setfield(L, -2, "type");
                lua_pushstring(L, display_hal_get_scancode_name(ev.key));
                lua_setfield(L, -2, "key");
                lua_pushinteger(L, ev.key);
                lua_setfield(L, -2, "scancode");
                lua_pushinteger(L, ev.mod);
                lua_setfield(L, -2, "mod");
                break;

            case INPUT_EVENT_MOUSE_DOWN:
                lua_pushstring(L, "mouse_down");
                lua_setfield(L, -2, "type");
                lua_pushinteger(L, ev.x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, ev.y);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, ev.button);
                lua_setfield(L, -2, "button");
                break;

            case INPUT_EVENT_MOUSE_UP:
                lua_pushstring(L, "mouse_up");
                lua_setfield(L, -2, "type");
                lua_pushinteger(L, ev.x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, ev.y);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, ev.button);
                lua_setfield(L, -2, "button");
                break;

            case INPUT_EVENT_MOUSE_WHEEL:
                lua_pushstring(L, "mouse_wheel");
                lua_setfield(L, -2, "type");
                lua_pushinteger(L, ev.x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, ev.y);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, ev.button);
                lua_setfield(L, -2, "dy");
                break;

            case INPUT_EVENT_TEXT:
                lua_pushstring(L, "text");
                lua_setfield(L, -2, "type");
                lua_pushstring(L, ev.text);
                lua_setfield(L, -2, "text");
                break;

            default:
                lua_pushstring(L, "unknown");
                lua_setfield(L, -2, "type");
                break;
            }
            return 1;
        }
        sleep_ms(5);  /* 5ms poll interval */
    }

    lua_pushnil(L);
    return 1;
}

/* ---- poll_events() → iterator ---- */
static int l_input_poll_events_iter(lua_State *L)
{
    /* L holds our closure: closure[1] = nil */
    input_event_t ev;
    if (display_hal_pop_input_event(&ev)) {
        /* Build event table */
        lua_newtable(L);
        switch (ev.type) {
        case INPUT_EVENT_KEY_DOWN:
            lua_pushstring(L, "key_down"); lua_setfield(L, -2, "type");
            lua_pushstring(L, display_hal_get_scancode_name(ev.key)); lua_setfield(L, -2, "key");
            lua_pushinteger(L, ev.key); lua_setfield(L, -2, "scancode");
            lua_pushinteger(L, ev.mod); lua_setfield(L, -2, "mod");
            break;
        case INPUT_EVENT_KEY_UP:
            lua_pushstring(L, "key_up"); lua_setfield(L, -2, "type");
            lua_pushstring(L, display_hal_get_scancode_name(ev.key)); lua_setfield(L, -2, "key");
            lua_pushinteger(L, ev.key); lua_setfield(L, -2, "scancode");
            lua_pushinteger(L, ev.mod); lua_setfield(L, -2, "mod");
            break;
        case INPUT_EVENT_MOUSE_DOWN:
            lua_pushstring(L, "mouse_down"); lua_setfield(L, -2, "type");
            lua_pushinteger(L, ev.x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, ev.y); lua_setfield(L, -2, "y");
            lua_pushinteger(L, ev.button); lua_setfield(L, -2, "button");
            break;
        case INPUT_EVENT_MOUSE_UP:
            lua_pushstring(L, "mouse_up"); lua_setfield(L, -2, "type");
            lua_pushinteger(L, ev.x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, ev.y); lua_setfield(L, -2, "y");
            lua_pushinteger(L, ev.button); lua_setfield(L, -2, "button");
            break;
        case INPUT_EVENT_MOUSE_WHEEL:
            lua_pushstring(L, "mouse_wheel"); lua_setfield(L, -2, "type");
            lua_pushinteger(L, ev.x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, ev.y); lua_setfield(L, -2, "y");
            lua_pushinteger(L, ev.button); lua_setfield(L, -2, "dy");
            break;
        case INPUT_EVENT_TEXT:
            lua_pushstring(L, "text"); lua_setfield(L, -2, "type");
            lua_pushstring(L, ev.text); lua_setfield(L, -2, "text");
            break;
        default: break;
        }
        return 1;
    }
    return 0;  /* no more events */
}

static int l_input_poll_events(lua_State *L)
{
    lua_pushcfunction(L, l_input_poll_events_iter);
    return 1;
}

/* ---- Module open ---- */
int luaopen_input(lua_State *L)
{
    lua_newtable(L);

    lua_pushcfunction(L, l_input_mouse_pos);
    lua_setfield(L, -2, "mouse_pos");

    lua_pushcfunction(L, l_input_mouse_down);
    lua_setfield(L, -2, "mouse_down");

    lua_pushcfunction(L, l_input_mouse_wheel);
    lua_setfield(L, -2, "mouse_wheel");

    lua_pushcfunction(L, l_input_key_down);
    lua_setfield(L, -2, "key_down");

    lua_pushcfunction(L, l_input_modifiers);
    lua_setfield(L, -2, "modifiers");

    lua_pushcfunction(L, l_input_key_name);
    lua_setfield(L, -2, "key_name");

    lua_pushcfunction(L, l_input_wait_event);
    lua_setfield(L, -2, "wait_event");

    lua_pushcfunction(L, l_input_poll_events);
    lua_setfield(L, -2, "poll_events");

    return 1;
}

/* ---- Register with cap_lua ---- */
esp_err_t lua_module_input_register(void)
{
    return cap_lua_register_module("input", luaopen_input);
}
