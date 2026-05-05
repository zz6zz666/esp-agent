/*
 * cap_lua_sandbox.c — Desktop Lua sandbox shim
 *
 * Registered as a built-in Lua module ("__sandbox") so it runs
 * automatically after luaL_openlibs() but before the user script.
 *
 * Additionally intercepts _G.__newindex to wrap the "storage" and
 * "capability" modules the moment they are loaded (they load AFTER
 * this module in the registration order).
 */
#include "cap_lua_sandbox.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cap_lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* ---- per-process configuration (set by main_desktop.c) ---- */

static char s_base_dir[192];

/* ---- memory budget (per-Lua-state) ---- */

#define CAP_LUA_MEMORY_BUDGET_BYTES (10ULL * 1024 * 1024)

typedef struct {
    lua_Alloc  orig_alloc;
    void      *orig_ud;
    size_t     allocated;  /* bytes allocated AFTER this tracker was installed */
    size_t     limit;
} sandbox_mem_t;

static sandbox_mem_t s_mem;

static void *sandbox_mem_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    sandbox_mem_t *ctx = (sandbox_mem_t *)ud;
    size_t growth;
    void *newptr;

    (void)ud;

    if (nsize == 0) {
        /* free */
        if (ptr) {
            if (osize <= ctx->allocated) ctx->allocated -= osize;
            else                         ctx->allocated  = 0;
        }
        return ctx->orig_alloc(ctx->orig_ud, ptr, osize, 0);
    }

    growth = ptr ? (nsize - osize) : nsize;

    if (growth > 0 && ctx->allocated + growth > ctx->limit) {
        return NULL;  /* budget exhausted */
    }

    newptr = ctx->orig_alloc(ctx->orig_ud, ptr, osize, nsize);
    if (newptr) {
        ctx->allocated += growth;
    }
    return newptr;
}

/* ---- helpers ---- */

static bool path_is_safe(const char *path)
{
    size_t base_len;

    if (!path || !path[0] || !s_base_dir[0]) {
        return false;
    }
    if (path[0] != '/') {
        return false;
    }
    base_len = strlen(s_base_dir);
    if (strncmp(path, s_base_dir, base_len) != 0) {
        return false;
    }
    if (path[base_len] != '/' && path[base_len] != '\0') {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }
    return true;
}

/* ---- storage-path wrapper (generic, driven by mask in upvalue[1]) ---- */

static int storage_path_wrapper(lua_State *L)
{
    int mask = (int)(intptr_t)lua_touserdata(L, lua_upvalueindex(1));

    if (mask & 1) {
        const char *p = lua_tostring(L, 1);
        if (p && !path_is_safe(p)) {
            return luaL_error(L, "storage: path outside sandbox base");
        }
    }
    if (mask & 2) {
        const char *p = lua_tostring(L, 2);
        if (p && !path_is_safe(p)) {
            return luaL_error(L, "storage: path outside sandbox base");
        }
    }

    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static void wrap_storage_fn(lua_State *L, int orig_idx, int wrapper_idx,
                            const char *name, int check_mask)
{
    lua_pushlightuserdata(L, (void *)(intptr_t)check_mask);
    lua_getfield(L, orig_idx, name);
    lua_pushcclosure(L, storage_path_wrapper, 2);
    lua_setfield(L, wrapper_idx, name);
}

static void copy_storage_fn(lua_State *L, int orig_idx, int wrapper_idx,
                            const char *name)
{
    lua_getfield(L, orig_idx, name);
    lua_setfield(L, wrapper_idx, name);
}

/*
 * Stack before call:  [1]=_G  [2]="storage"  [3]=original_table
 * After:  _G.storage = wrapped_table  (stack restored)
 */
static void wrap_storage_table(lua_State *L)
{
    lua_newtable(L); /* [4] wrapper */

    wrap_storage_fn(L, 3, 4, "read_file",  1);
    wrap_storage_fn(L, 3, 4, "write_file", 1);
    wrap_storage_fn(L, 3, 4, "exists",     1);
    wrap_storage_fn(L, 3, 4, "stat",       1);
    wrap_storage_fn(L, 3, 4, "listdir",    1);
    wrap_storage_fn(L, 3, 4, "remove",     1);
    wrap_storage_fn(L, 3, 4, "mkdir",      1);
    wrap_storage_fn(L, 3, 4, "rename",     3);

    copy_storage_fn(L, 3, 4, "get_root_dir");
    copy_storage_fn(L, 3, 4, "join_path");
    copy_storage_fn(L, 3, 4, "get_free_space");

    lua_replace(L, 3); /* [3] = wrapper */
    lua_rawset(L, 1);  /* _G["storage"] = wrapper */
}

/* ---- capability.call whitelist wrapper ---- */

static const char *const s_cap_allowlist[] = {
    "get_system_info",
    "get_ip_address",
    "get_memory_info",
    "get_cpu_info",
    "get_wifi_info",
    "memory_list",
    "memory_get",
    "memory_search",
    "memory_count",
    "memory_stats",
    "get_time_info",
    "lua_list_scripts",
    "lua_run_script",
    "lua_list_async_jobs",
    "lua_get_async_job",
    NULL,
};

static bool cap_is_allowed(const char *name)
{
    int i;
    if (!name) {
        return false;
    }
    for (i = 0; s_cap_allowlist[i]; i++) {
        if (strcmp(name, s_cap_allowlist[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int capability_call_wrapper(lua_State *L)
{
    const char *name = lua_tostring(L, 1);

    if (name && !cap_is_allowed(name)) {
        return luaL_error(L, "cap '%s' not allowed from Lua", name);
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

/*
 * Stack: [1]=_G  [2]="capability"  [3]=original_table
 * Result: _G.capability = wrapped_table
 */
static void wrap_capability_table(lua_State *L)
{
    lua_newtable(L); /* [4] wrapper */

    lua_getfield(L, 3, "call");
    lua_pushcclosure(L, capability_call_wrapper, 1);
    lua_setfield(L, 4, "call");

    lua_replace(L, 3);
    lua_rawset(L, 1);
}

/* ---- _G.__newindex metatable handler ---- */

static int g_newindex_handler(lua_State *L)
{
    const char *key = lua_tostring(L, 2);

    if (key && strcmp(key, "storage") == 0 && lua_istable(L, 3)) {
        wrap_storage_table(L);
        return 0;
    }
    if (key && strcmp(key, "capability") == 0 && lua_istable(L, 3)) {
        wrap_capability_table(L);
        return 0;
    }

    lua_rawset(L, 1);
    return 0;
}

/* ---- module open function ---- */

static int luaopen_sandbox(lua_State *L)
{
    /*
     * 1.  Remove dangerous base globals
     */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");

    /*
     * 2.  Erase io & os from _G AND package.loaded
     */
    lua_pushnil(L); lua_setglobal(L, "io");
    lua_pushnil(L); lua_setglobal(L, "os");

    lua_getglobal(L, "package");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "loaded");
        if (lua_istable(L, -1)) {
            lua_pushnil(L); lua_setfield(L, -2, "io");
            lua_pushnil(L); lua_setfield(L, -2, "os");
        }
        lua_pop(L, 1);

        lua_pushnil(L); lua_setfield(L, -2, "loadlib");
        lua_getfield(L, -1, "searchers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L); lua_rawseti(L, -2, 3);
            lua_pushnil(L); lua_rawseti(L, -2, 4);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    /*
     * 3.  Strip debug — keep only traceback
     */
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1)) {
        static const char *const kill[] = {
            "debug", "gethook", "sethook", "getregistry",
            "getmetatable", "setmetatable",
            "getupvalue", "setupvalue", "upvalueid", "upvaluejoin",
            "getlocal", "setlocal", "getinfo",
            "getuservalue", "setuservalue",
            NULL
        };
        for (const char *const *k = kill; *k; k++) {
            lua_pushnil(L);
            lua_setfield(L, -2, *k);
        }
    }
    lua_pop(L, 1);

    /*
     * 4.  Nil bytecode generator
     */
    lua_getglobal(L, "string");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
    }
    lua_pop(L, 1);

    /*
     * 5.  Intercept _G.__newindex so we can wrap storage & capability
     *     the moment they are loaded (they are registered AFTER us).
     */
    lua_getglobal(L, "_G");
    lua_newtable(L);
    lua_pushcfunction(L, g_newindex_handler);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);

    /*
     * 6.  Memory budget — install a wrapper around the C allocator so
     *     user scripts cannot exhaust host RAM.  The budget counts only
     *     allocations that happen AFTER this point (standard libraries
     *     are already loaded and don't count against the limit).
     */
    s_mem.orig_alloc = lua_getallocf(L, &s_mem.orig_ud);
    s_mem.allocated  = 0;
    s_mem.limit      = CAP_LUA_MEMORY_BUDGET_BYTES;
    lua_setallocf(L, sandbox_mem_alloc, &s_mem);

    /* Tune GC for tighter memory pressure (half pause, 3× speed). */
    lua_gc(L, LUA_GCCOLLECT,  0);
    lua_gc(L, LUA_GCSETPAUSE,  50);
    lua_gc(L, LUA_GCSETSTEPMUL, 300);

    lua_pushboolean(L, 1);
    return 1;
}

/* ---- lifecycle ---- */

esp_err_t cap_lua_sandbox_init(const char *base_dir)
{
    size_t n;

    if (!base_dir || !base_dir[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    n = strlen(base_dir);
    if (n >= sizeof(s_base_dir)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_base_dir, base_dir, n + 1);

    return cap_lua_register_module("__sandbox", luaopen_sandbox);
}
