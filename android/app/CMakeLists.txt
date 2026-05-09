# NDK CMake — Crush Claw Android Port (clean architecture)
# sim_hal provides platform layer, esp-claw is unmodified upstream
cmake_minimum_required(VERSION 3.22.1)
project("crushclaw C")

# ---- Platform ----
add_definitions(-DPLATFORM_ANDROID=1 -DSIMULATOR_BUILD=1)
add_definitions(-DCONFIG_APP_CLAW_MEMORY_MODE_FULL=1)
add_definitions(-DCONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=1)
add_definitions(-DCONFIG_FREERTOS_USE_TRACE_FACILITY=1)
add_definitions(-DCONFIG_APP_CLAW_ENABLE_CLI=1)
add_definitions(-DCONFIG_APP_CLAW_ENABLE_EMOTE=1)
add_definitions(-DCONFIG_ESP_CONSOLE_UART_DEFAULT=1)
add_definitions(-DCONFIG_CLAW_CORE_STAGE_VERBOSITY_VERBOSE=1)
add_definitions(-DCONFIG_IDF_TARGET=\"android\")
add_definitions(-DCONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT=1)
add_definitions(-DCONFIG_LWIP_SNTP_MAX_SERVERS=1)

# ---- Capabilities (all enabled, same as desktop) ----
foreach(cap cap_lua cap_im_local cap_im_qq cap_im_tg cap_im_feishu cap_im_wechat
         cap_session_mgr cap_files cap_skill_mgr cap_router_mgr cap_scheduler
         cap_system cap_time cap_web_search cap_llm_inspect cap_cli
         cap_mcp_client cap_mcp_server cap_screenshot cap_emote_text)
    string(TOUPPER ${cap} cap_up)
    add_definitions(-DCONFIG_APP_CLAW_${cap_up}=1)
endforeach()

# Lua module definitions
foreach(mod display board_manager capability delay storage system event_publisher)
    string(TOUPPER ${mod} mod_up)
    add_definitions(-DCONFIG_APP_CLAW_LUA_MODULE_${mod_up}=1)
endforeach()

# ---- Paths ----
set(PROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SH_ANDROID ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
set(SH ${PROOT}/sim_hal)
set(EC ${PROOT}/esp-claw)
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# ---- Source files ----
set(ANDROID_SRCS
    # JNI entry + HAL
    ${SH_ANDROID}/android_entry.c
    ${SH_ANDROID}/desktop_main_android.c
    ${SH_ANDROID}/display_hal_android.c
    ${SH_ANDROID}/font_android.c
    # Android stub for SDL2-based lua_module_input
    ${SH_ANDROID}/lua_module_input_android_stub.c
    ${SH_ANDROID}/android_hal_stubs.c
)

# Remove _FORTIFY_SOURCE added by NDK toolchain (causes false-positive
# __fortify_fatal on bionic with dynamically-allocated snprintf buffers)
string(REGEX REPLACE "-D_FORTIFY_SOURCE=[0-9]+" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(SH_SRCS
    ${SH}/freertos_shim.c ${SH}/esp_log.c ${SH}/platform_utils.c
    ${SH}/nvs_stub.c ${SH}/nvs.c ${SH}/cJSON.c ${SH}/base64.c
    ${SH}/aes_stub.c ${SH}/camera_stub.c
    ${SH}/esp_board_manager_stub.c ${SH}/esp_lcd_panel_stub.c
    ${SH}/esp_mmap_assets_stub.c ${SH}/esp_mcp_stubs.c
    ${SH}/lv_font_stub.c ${SH}/cap_lua_sandbox.c
    ${SH}/cap_screenshot.c ${SH}/cap_emote_text.c
    ${SH}/emote_stub.c ${SH}/console_unix.c
    # libcurl-based transport (replaces android-specific hand-written versions)
    ${SH}/http_curl.c
    ${SH}/esp_http_client.c
    ${SH}/esp_websocket_client.c
)

# GFX library
file(GLOB GFX_SRCS ${SH}/deps/esp_emote_gfx/src/core/base/*.c
                   ${SH}/deps/esp_emote_gfx/src/core/display/*.c
                   ${SH}/deps/esp_emote_gfx/src/core/draw/*.c
                   ${SH}/deps/esp_emote_gfx/src/core/object/*.c
                   ${SH}/deps/esp_emote_gfx/src/core/runtime/*.c
                   ${SH}/deps/esp_emote_gfx/src/lib/eaf/*.c
                   ${SH}/deps/esp_emote_gfx/src/lib/qrcode/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/anim/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/basic/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/font/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/img/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/label/*.c
                   ${SH}/deps/esp_emote_gfx/src/widget/motion/*.c)

# Expression emote engine
file(GLOB EMOTE_SRCS ${SH}/deps/esp_emote_expression/src/*.c
                     ${SH}/deps/esp_emote_expression/src/default_assets/*.c)

# ---- esp-claw (upstream, unmodified) ----
set(CLAW_COMMON_SRCS
    ${EC}/components/common/app_claw/app_claw.c
    ${EC}/components/common/app_claw/app_capabilities.c
    ${EC}/components/common/app_claw/app_lua_modules.c
    ${EC}/components/common/app_claw/app_claw_cli.c
)

set(CLAW_CORE_SRCS
    ${EC}/components/claw_modules/claw_core/src/claw_core.c
    ${EC}/components/claw_modules/claw_core/src/claw_task.c
    ${EC}/components/claw_modules/claw_core/src/claw_core_llm.c
    ${EC}/components/claw_modules/claw_core/src/llm/claw_llm_runtime.c
    ${EC}/components/claw_modules/claw_core/src/llm/backends/claw_llm_backend_anthropic.c
    ${EC}/components/claw_modules/claw_core/src/llm/backends/claw_llm_backend_openai_compatible.c
    ${EC}/components/claw_modules/claw_core/src/llm/backends/claw_llm_backend_custom.c
    ${EC}/components/claw_modules/claw_core/src/llm/media/claw_media_pipeline.c
    ${EC}/components/claw_modules/claw_cap/src/claw_cap.c
    ${EC}/components/claw_modules/claw_event_router/src/claw_event.c
    ${EC}/components/claw_modules/claw_event_router/src/claw_event_router.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_cap.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_extract.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_lightweight.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_profile.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_session.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_storage.c
    ${EC}/components/claw_modules/claw_memory/src/claw_memory_utils.c
    ${EC}/components/claw_modules/claw_skill/src/claw_skill.c
    ${EC}/components/common/display_arbiter/display_arbiter.c
)

set(CAP_SRCS
    ${EC}/components/claw_capabilities/cap_lua/src/cap_lua.c
    ${EC}/components/claw_capabilities/cap_lua/src/cap_lua_async.c
    ${EC}/components/claw_capabilities/cap_lua/src/cap_lua_runtime.c
    ${EC}/components/claw_capabilities/cap_lua/src/cmd_cap_lua.c
    ${EC}/components/claw_capabilities/cap_im_local/src/cap_im_local.c
    ${EC}/components/claw_capabilities/cap_im_qq/src/cap_im_qq.c
    ${EC}/components/claw_capabilities/cap_im_qq/src/cmd_cap_im_qq.c
    ${EC}/components/claw_capabilities/cap_im_tg/src/cap_im_tg.c
    ${EC}/components/claw_capabilities/cap_im_tg/src/cmd_cap_im_tg.c
    ${EC}/components/claw_capabilities/cap_im_feishu/src/cap_im_feishu.c
    ${EC}/components/claw_capabilities/cap_im_feishu/src/cmd_cap_im_feishu.c
    ${EC}/components/claw_capabilities/cap_im_wechat/src/cap_im_wechat.c
    ${EC}/components/claw_capabilities/cap_im_wechat/src/cmd_cap_im_wechat.c
    ${EC}/components/claw_capabilities/cap_im_attachment/src/cap_im_attachment.c
    ${EC}/components/claw_capabilities/cap_files/src/cap_files.c
    ${EC}/components/claw_capabilities/cap_session_mgr/src/cap_session_mgr.c
    ${EC}/components/claw_capabilities/cap_skill_mgr/src/cap_skill_mgr.c
    ${EC}/components/claw_capabilities/cap_skill_mgr/src/cmd_cap_skill.c
    ${EC}/components/claw_capabilities/cap_router_mgr/src/cap_router_mgr.c
    ${EC}/components/claw_capabilities/cap_router_mgr/src/cmd_cap_router_mgr.c
    ${EC}/components/claw_capabilities/cap_scheduler/src/cap_scheduler.c
    ${EC}/components/claw_capabilities/cap_scheduler/src/cap_scheduler_parser.c
    ${EC}/components/claw_capabilities/cap_scheduler/src/cap_scheduler_store.c
    ${EC}/components/claw_capabilities/cap_scheduler/src/cmd_cap_scheduler.c
    ${EC}/components/claw_capabilities/cap_system/src/cap_system.c
    ${EC}/components/claw_capabilities/cap_time/src/cap_time.c
    ${EC}/components/claw_capabilities/cap_time/src/cmd_cap_time.c
    ${EC}/components/claw_capabilities/cap_web_search/src/cap_web_search.c
    ${EC}/components/claw_capabilities/cap_web_search/src/cmd_cap_web_search.c
    ${EC}/components/claw_capabilities/cap_llm_inspect/src/cap_llm_inspect.c
    ${EC}/components/claw_capabilities/cap_llm_inspect/src/cmd_cap_llm_inspect.c
    ${EC}/components/claw_capabilities/cap_cli/src/cap_cli.c
    ${EC}/components/claw_capabilities/cap_mcp_client/src/cap_mcp_client.c
    ${EC}/components/claw_capabilities/cap_mcp_client/src/cap_mcp_client_core.c
    ${EC}/components/claw_capabilities/cap_mcp_client/src/cap_mcp_discover_core.c
    ${EC}/components/claw_capabilities/cap_mcp_client/src/cmd_cap_mcp_client.c
    ${EC}/components/claw_capabilities/cap_mcp_server/src/cap_mcp_server.c
    ${EC}/components/claw_capabilities/cap_mcp_server/src/cmd_cap_mcp_server.c
)

# Lua modules (excluding SDL2-based lua_module_input)
set(LUA_MOD_SRCS
    ${EC}/components/lua_modules/lua_module_display/src/lua_module_display.c
    ${EC}/components/lua_modules/lua_module_board_manager/src/lua_module_board_manager.c
    ${EC}/components/lua_modules/lua_module_call_capability/src/lua_module_capability.c
    ${EC}/components/lua_modules/lua_module_delay/src/lua_module_delay.c
    ${EC}/components/lua_modules/lua_module_storage/src/lua_module_storage.c
    ${EC}/components/lua_modules/lua_module_system/src/lua_module_system.c
    ${EC}/components/lua_modules/lua_module_event_publisher/src/lua_module_event_publisher.c
)

add_library(crushclaw SHARED
    ${ANDROID_SRCS} ${SH_SRCS} ${GFX_SRCS} ${EMOTE_SRCS}
    ${CLAW_COMMON_SRCS} ${CLAW_CORE_SRCS} ${CAP_SRCS} ${LUA_MOD_SRCS}
)

# ---- Include paths ----
target_include_directories(crushclaw PRIVATE
    # JNI (our Android code + stubs)
    ${SH_ANDROID}
    # sim_hal stubs (override ESP-IDF)
    ${SH}/include ${SH}/include/esp ${SH}/include/freertos
    ${SH}/include/lwip ${SH}/include/mbedtls ${SH}/include/platform
    ${SH}/include/soc ${SH}/include/driver ${SH}/include/lib
    ${SH}
    # stb single-header
    ${SH}/deps/stb
    # LVGL stub header
    ${SH}/deps/lvgl
    # GFX
    ${SH}/deps/esp_emote_gfx/include
    ${SH}/deps/esp_emote_gfx/src
    # Emote expression
    ${SH}/deps/esp_emote_expression/include
    ${SH}/deps/esp_emote_expression/priv_include
    # mmap assets
    ${SH}/deps/esp_mmap_assets/include
    # esp-claw core
    ${EC}/components/claw_modules/claw_core/include
    ${EC}/components/claw_modules/claw_core/src
    ${EC}/components/claw_modules/claw_cap/include
    ${EC}/components/claw_modules/claw_event_router/include
    ${EC}/components/claw_modules/claw_memory/include
    ${EC}/components/claw_modules/claw_memory/src
    ${EC}/components/claw_modules/claw_skill/include
    # common
    ${EC}/components/common/app_claw/include
    ${EC}/components/common/display_arbiter/include
    ${EC}/components/common/emote/include
    ${EC}/components/common/settings/include
    # display HAL header (in lua_module_display)
    ${EC}/components/lua_modules/lua_module_display/src
    # capabilities
    ${EC}/components/claw_capabilities/cap_lua/include
    ${EC}/components/claw_capabilities/cap_im_local/include
    ${EC}/components/claw_capabilities/cap_im_qq/include
    ${EC}/components/claw_capabilities/cap_im_tg/include
    ${EC}/components/claw_capabilities/cap_im_feishu/include
    ${EC}/components/claw_capabilities/cap_im_wechat/include
    ${EC}/components/claw_capabilities/cap_im_attachment/include
    ${EC}/components/claw_capabilities/cap_files/include
    ${EC}/components/claw_capabilities/cap_scheduler/include
    ${EC}/components/claw_capabilities/cap_system/include
    ${EC}/components/claw_capabilities/cap_time/include
    ${EC}/components/claw_capabilities/cap_web_search/include
    ${EC}/components/claw_capabilities/cap_skill_mgr/include
    ${EC}/components/claw_capabilities/cap_cli/include
    ${EC}/components/claw_capabilities/cap_session_mgr/include
    ${EC}/components/claw_capabilities/cap_router_mgr/include
    ${EC}/components/claw_capabilities/cap_llm_inspect/include
    ${EC}/components/claw_capabilities/cap_mcp_client/include
    ${EC}/components/claw_capabilities/cap_mcp_server/include
    # Lua modules
    ${EC}/components/lua_modules/lua_module_display/src
    ${EC}/components/lua_modules/lua_module_board_manager/src
    ${EC}/components/lua_modules/lua_module_call_capability/src
    ${EC}/components/lua_modules/lua_module_delay/src
    ${EC}/components/lua_modules/lua_module_storage/src
    ${EC}/components/lua_modules/lua_module_system/src
    ${EC}/components/lua_modules/lua_module_event_publisher/src
    # Lua (cross-compiled)
    ${TP}/installed/lua/${ANDROID_ABI}/include
    # curl + OpenSSL (cross-compiled)
    ${TP}/installed/curl/${ANDROID_ABI}/include
    ${TP}/installed/openssl/${ANDROID_ABI}/include
)

# ---- Link ----
find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)

target_link_directories(crushclaw PRIVATE
    ${TP}/installed/lua/${ANDROID_ABI}/lib
    ${TP}/installed/curl/${ANDROID_ABI}/lib
    ${TP}/installed/openssl/${ANDROID_ABI}/lib
)

target_link_libraries(crushclaw
    ${log-lib} ${android-lib} ${z-lib}
    lua
    curl
    ssl crypto
)
