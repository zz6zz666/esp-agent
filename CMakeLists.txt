cmake_minimum_required(VERSION 3.16)
project(esp-claw-desktop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- Dependencies ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2    REQUIRED sdl2)
pkg_check_modules(CURL    REQUIRED libcurl)
pkg_check_modules(LUA     REQUIRED lua5.4)
pkg_check_modules(JSONC   REQUIRED json-c)

# ---- our sim_hal library ----
set(SIM_HAL_DIR  ${CMAKE_SOURCE_DIR}/sim_hal)
set(SIM_INC_DIR  ${SIM_HAL_DIR}/include)
set(SIM_SRCS
    ${SIM_HAL_DIR}/esp_log.c
    ${SIM_HAL_DIR}/freertos_shim.c
    ${SIM_HAL_DIR}/nvs_stub.c
    ${SIM_HAL_DIR}/http_curl.c
    ${SIM_HAL_DIR}/display_sdl2.c
    ${SIM_HAL_DIR}/camera_stub.c
    ${SIM_HAL_DIR}/console_unix.c
    ${SIM_HAL_DIR}/cJSON.c
    ${SIM_HAL_DIR}/base64.c
    ${SIM_HAL_DIR}/nvs.c
    ${SIM_HAL_DIR}/esp_http_client.c
    ${SIM_HAL_DIR}/esp_websocket_client.c
    ${SIM_HAL_DIR}/aes_stub.c
    ${SIM_HAL_DIR}/esp_board_manager_stub.c
    ${SIM_HAL_DIR}/emote_stub.c
    ${SIM_HAL_DIR}/esp_lcd_panel_stub.c
    ${SIM_HAL_DIR}/esp_mmap_assets_stub.c
    ${SIM_HAL_DIR}/lv_font_stub.c
)

# ---- external deps (emote engine) ----
set(DEPS_DIR ${CMAKE_SOURCE_DIR}/sim_hal/deps)

# gfx library (32 source files — lightweight UI graphics engine)
set(GFX_SRC_DIR ${DEPS_DIR}/esp_emote_gfx/src)
set(GFX_SRCS
    ${GFX_SRC_DIR}/core/base/gfx_color.c
    ${GFX_SRC_DIR}/core/base/gfx_log.c
    ${GFX_SRC_DIR}/core/display/gfx_disp.c
    ${GFX_SRC_DIR}/core/display/gfx_refr.c
    ${GFX_SRC_DIR}/core/display/gfx_render.c
    ${GFX_SRC_DIR}/core/draw/gfx_blend.c
    ${GFX_SRC_DIR}/core/draw/gfx_sw_draw.c
    ${GFX_SRC_DIR}/core/object/gfx_obj.c
    ${GFX_SRC_DIR}/core/object/gfx_widget_class.c
    ${GFX_SRC_DIR}/core/runtime/gfx_core.c
    ${GFX_SRC_DIR}/core/runtime/gfx_timer.c
    ${GFX_SRC_DIR}/core/runtime/gfx_touch.c
    ${GFX_SRC_DIR}/lib/eaf/gfx_eaf_dec.c
    ${GFX_SRC_DIR}/lib/qrcode/qrcode_wrapper.c
    ${GFX_SRC_DIR}/lib/qrcode/qrcodegen.c
    ${GFX_SRC_DIR}/widget/anim/gfx_anim.c
    ${GFX_SRC_DIR}/widget/anim/gfx_anim_decoder.c
    ${GFX_SRC_DIR}/widget/anim/gfx_anim_decoder_eaf.c
    ${GFX_SRC_DIR}/widget/basic/gfx_button.c
    ${GFX_SRC_DIR}/widget/basic/gfx_qrcode.c
    ${GFX_SRC_DIR}/widget/font/gfx_font_ft.c
    ${GFX_SRC_DIR}/widget/font/gfx_font_lv.c
    ${GFX_SRC_DIR}/widget/img/gfx_img.c
    ${GFX_SRC_DIR}/widget/img/gfx_img_dec.c
    ${GFX_SRC_DIR}/widget/img/gfx_mesh_img.c
    ${GFX_SRC_DIR}/widget/label/gfx_draw_label.c
    ${GFX_SRC_DIR}/widget/label/gfx_label.c
    ${GFX_SRC_DIR}/widget/label/gfx_label_obj.c
    ${GFX_SRC_DIR}/widget/motion/gfx_motion.c
    ${GFX_SRC_DIR}/widget/motion/gfx_motion_player.c
    ${GFX_SRC_DIR}/widget/motion/gfx_motion_primitives.c
    ${GFX_SRC_DIR}/widget/motion/gfx_motion_scene.c
    ${GFX_SRC_DIR}/widget/motion/gfx_motion_style.c
)

# expression_emote library (7 source files — emote animation engine)
set(EMOTE_SRC_DIR ${DEPS_DIR}/esp_emote_expression/src)
set(EMOTE_SRCS
    ${EMOTE_SRC_DIR}/emote_init.c
    ${EMOTE_SRC_DIR}/emote_load.c
    ${EMOTE_SRC_DIR}/emote_op.c
    ${EMOTE_SRC_DIR}/emote_setup.c
    ${EMOTE_SRC_DIR}/default_assets/font_maison_neue_book_12.c
    ${EMOTE_SRC_DIR}/default_assets/font_maison_neue_book_26.c
    ${EMOTE_SRC_DIR}/default_assets/font_puhui_basic_20_4.c
)

# ---- esp-claw paths ----
set(ESPCLAW_DIR ${CMAKE_SOURCE_DIR}/esp-claw)
set(ESPCLAW_COMPONENTS ${ESPCLAW_DIR}/components)

# ---- Build definitions (replaces ESP-IDF Kconfig) ----
add_compile_definitions(
    SIMULATOR_BUILD
    CONFIG_APP_CLAW_CAP_LUA=1
    CONFIG_APP_CLAW_CAP_IM_LOCAL=1
    CONFIG_APP_CLAW_CAP_IM_QQ=1
    CONFIG_APP_CLAW_CAP_IM_TG=1
    CONFIG_APP_CLAW_CAP_IM_FEISHU=1
    CONFIG_APP_CLAW_CAP_IM_WECHAT=1
    CONFIG_APP_CLAW_CAP_SESSION_MGR=1
    CONFIG_APP_CLAW_CAP_FILES=1
    CONFIG_APP_CLAW_CAP_SKILL_MGR=1
    CONFIG_APP_CLAW_CAP_ROUTER_MGR=1
    CONFIG_APP_CLAW_MEMORY_MODE_FULL=1
    CONFIG_APP_CLAW_ENABLE_CLI=1
    CONFIG_APP_CLAW_LUA_MODULE_DISPLAY=1
    CONFIG_APP_CLAW_LUA_MODULE_BOARD_MANAGER=1
    CONFIG_APP_CLAW_ENABLE_EMOTE=1
    CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT=1
    CONFIG_ESP_CONSOLE_UART_DEFAULT=1
    CONFIG_CLAW_CORE_STAGE_VERBOSITY_VERBOSE=1
    CONFIG_IDF_TARGET=\"linux-x86_64\"
)

# ---- Include paths ----
# sim_hal stubs come FIRST to override any ESP-IDF headers
set(COMMON_INCLUDES
    ${SIM_INC_DIR}
    ${SIM_INC_DIR}/esp
    ${SIM_INC_DIR}/freertos
    ${SIM_INC_DIR}/mbedtls
    # claw_modules
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/include
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_cap/include
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_event_router/include
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/include
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_skill/include
    # claw_capabilities
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_lua/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_local/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_qq/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_tg/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_feishu/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_wechat/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_attachment/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_files/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_scheduler/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_system/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_time/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_web_search/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_skill_mgr/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_session_mgr/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_router_mgr/include
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_llm_inspect/include
    # common
    ${ESPCLAW_COMPONENTS}/common/app_claw/include
    ${ESPCLAW_COMPONENTS}/common/settings/include
    # display arbiter
    ${ESPCLAW_COMPONENTS}/common/display_arbiter/include
    # Lua modules display HAL
    ${ESPCLAW_COMPONENTS}/lua_modules/lua_module_display/include

    # Lua modules board manager
    ${ESPCLAW_COMPONENTS}/lua_modules/lua_module_board_manager/include
    # deps: LVGL stub header (font types only)
    ${DEPS_DIR}/lvgl
    # deps: SoC stubs
    ${SIM_INC_DIR}/soc
    # deps: gfx (public + private includes)
    ${DEPS_DIR}/esp_emote_gfx/include
    ${DEPS_DIR}/esp_emote_gfx/src
    # deps: expression_emote (public + private includes)
    ${DEPS_DIR}/esp_emote_expression/include
    ${DEPS_DIR}/esp_emote_expression/priv_include
    # deps: mmap_assets
    ${DEPS_DIR}/esp_mmap_assets/include
    # esp-claw emote header (sim_hal/emote_stub.c implements it)
    ${ESPCLAW_COMPONENTS}/common/emote/include
)

# ---- Source files from esp-claw ----
# We skip claw_llm_http_transport.c (replaced by our http_curl.c)

set(ESPCLAW_SRCS
    # claw_core (minus http_transport)
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/claw_core.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/claw_task.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/claw_core_llm.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/llm/claw_llm_runtime.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/llm/backends/claw_llm_backend_anthropic.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/llm/backends/claw_llm_backend_openai_compatible.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/llm/backends/claw_llm_backend_custom.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_core/src/llm/media/claw_media_pipeline.c

    # claw_cap
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_cap/src/claw_cap.c

    # claw_event_router
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_event_router/src/claw_event.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_event_router/src/claw_event_router.c

    # claw_memory
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_cap.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_extract.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_lightweight.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_profile.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_session.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_storage.c
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_memory/src/claw_memory_utils.c

    # claw_skill
    ${ESPCLAW_COMPONENTS}/claw_modules/claw_skill/src/claw_skill.c

    # app_claw
    ${ESPCLAW_COMPONENTS}/common/app_claw/app_claw.c
    ${ESPCLAW_COMPONENTS}/common/app_claw/app_capabilities.c
    ${ESPCLAW_COMPONENTS}/common/app_claw/app_lua_modules.c
    ${ESPCLAW_COMPONENTS}/common/app_claw/app_claw_cli.c

    # CLI commands for enabled capabilities
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_lua/src/cmd_cap_lua.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_router_mgr/src/cmd_cap_router_mgr.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_skill_mgr/src/cmd_cap_skill.c

    # cap_lua
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_lua/src/cap_lua.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_lua/src/cap_lua_async.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_lua/src/cap_lua_runtime.c

    # cap_im_local
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_local/src/cap_im_local.c

    # cap_im_qq
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_qq/src/cap_im_qq.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_qq/src/cmd_cap_im_qq.c

    # cap_im_tg
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_tg/src/cap_im_tg.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_tg/src/cmd_cap_im_tg.c

    # cap_im_feishu
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_feishu/src/cap_im_feishu.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_feishu/src/cmd_cap_im_feishu.c

    # cap_im_wechat
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_wechat/src/cap_im_wechat.c
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_wechat/src/cmd_cap_im_wechat.c

    # cap_im_attachment (shared attachment download/upload helpers)
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_im_attachment/src/cap_im_attachment.c

    # cap_files
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_files/src/cap_files.c

    # cap_scheduler — requires full NVS integration, skip for now
    # ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_scheduler/src/cap_scheduler.c
    # ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_scheduler/src/cap_scheduler_parser.c
    # ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_scheduler/src/cap_scheduler_store.c

    # cap_system — requires many ESP-IDF structs, skip for now
    # ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_system/src/cap_system.c

    # cap_time — requires SNTP, skip for now
    # ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_time/src/cap_time.c

    # cap_session_mgr
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_session_mgr/src/cap_session_mgr.c

    # cap_skill_mgr
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_skill_mgr/src/cap_skill_mgr.c

    # cap_router_mgr
    ${ESPCLAW_COMPONENTS}/claw_capabilities/cap_router_mgr/src/cap_router_mgr.c

    # cap_web_search — requires esp_http_client struct init, skip for now
    # (can be migrated to use http_curl.c directly)

    # display_arbiter (shared display ownership tracking)
    ${ESPCLAW_COMPONENTS}/common/display_arbiter/display_arbiter.c

    # lua_module_display (SDL2-backed display drawing)
    ${ESPCLAW_COMPONENTS}/lua_modules/lua_module_display/src/lua_module_display.c

    # lua_module_board_manager (simulated board info)
    ${ESPCLAW_COMPONENTS}/lua_modules/lua_module_board_manager/lua_module_board_manager.c
)

# ---- Desktop main ----
set(DESKTOP_MAIN ${CMAKE_SOURCE_DIR}/main_desktop.c)

# ---- Add executable ----
add_executable(esp-claw-desktop
    ${DESKTOP_MAIN}
    ${SIM_SRCS}
    ${GFX_SRCS}
    ${EMOTE_SRCS}
    ${ESPCLAW_SRCS}
)

target_include_directories(esp-claw-desktop PRIVATE
    ${COMMON_INCLUDES}
    ${LUA_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${JSONC_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/components/include
    ${CMAKE_SOURCE_DIR}/components/desktop/include
)

target_link_libraries(esp-claw-desktop PRIVATE
    ${CURL_LIBRARIES}
    ${LUA_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${JSONC_LIBRARIES}
    png16
    pthread
    m
    dl
    ssl
    crypto
)

target_compile_options(esp-claw-desktop PRIVATE
    -Wall -Wextra
    ${CURL_CFLAGS_OTHER}
    ${LUA_CFLAGS_OTHER}
    ${SDL2_CFLAGS_OTHER}
    ${JSONC_CFLAGS_OTHER}
)

# Desktop-only extension components
include(${CMAKE_SOURCE_DIR}/components/desktop/CMakeLists.txt)

# Allow the sim to include its own dir as system include (for esp_ headers)
target_include_directories(esp-claw-desktop SYSTEM BEFORE PRIVATE ${SIM_INC_DIR})

message(STATUS "=== esp-claw Desktop Simulator Build ===")
message(STATUS "  SDL2:    ${SDL2_LIBRARIES}")
message(STATUS "  CURL:    ${CURL_LIBRARIES}")
message(STATUS "  Lua:     ${LUA_LIBRARIES}")
message(STATUS "  JSON-C:  ${JSONC_LIBRARIES}")
