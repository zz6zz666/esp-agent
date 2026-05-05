# Feature Support — esp-claw Desktop Simulator

This document audits every feature of the upstream `esp-claw` embedded AI agent
framework and reports its status in the cross-platform (Linux + Windows) desktop simulator.

**Status key:**
- **Supported** — fully functional, tested
- **Partial** — limited functionality or simulated with caveats
- **Unsupported** — excluded from simulator build (hardware-dependent)

---

## Core Agent Framework

| Feature | Status | Notes |
|---------|--------|-------|
| Agent main loop (`claw_core`) | Supported | Full task loop, LLM handshake, tool-calling cycle |
| LLM HTTP transport | Supported | libcurl backend (`http_curl.c`) replaces `esp_http_client` |
| Anthropic API backend | Supported | Streaming + non-streaming, tool use, thinking blocks, system prompts |
| OpenAI-compatible backend | Supported | Generic OpenAI-compatible providers (dashscope, volcengine, etc.) |
| Custom backend | Supported | User-defined HTTP endpoint |
| Media pipeline | Supported | Image attachment encode/decode |

## LLM Providers (config.json `llm.profile`)

| Profile | Status | Notes |
|---------|--------|-------|
| `openai` | Supported | Standard OpenAI API |
| `anthropic` | Supported | Anthropic Messages API |
| `custom_openai_compatible` | Supported | Any OpenAI-compatible endpoint |
| `deepseek` | Supported | Anthropic backend + extended thinking |
| `dashscope` | Supported | openai_compatible backend |
| `dashscope_coding` | Supported | Anthropic backend |
| `volcengine` | Supported | openai_compatible backend |
| `volcengine_coding` | Supported | Anthropic backend |
| `minimax` | Supported | Anthropic backend |

## LLM Features

| Feature | Status | Notes |
|---------|--------|-------|
| Multi-turn conversation | Supported | Session history stored in `~/.crush-claw/sessions/` |
| Tool calling (function calling) | Supported | Anthropic + OpenAI-compatible |
| Streaming responses | Supported | SSE over libcurl |
| System prompts | Supported | Skills auto-loaded as system blocks |
| Reasoning / thinking blocks | Supported | DeepSeek extended thinking, Anthropic extended thinking |
| Vision (image attachments) | Supported | Media pipeline encodes/decodes images |
| Multi-turn reasoning persistence | Partial | Reasoning preserved within a single request's tool-calling loop. Lost when session is persisted to disk (`role\ttext` format does not store reasoning). |

---

## Capability Groups

### Capability Registry

| Feature | Status | Notes |
|---------|--------|-------|
| `claw_cap` registry | Supported | Full register/unregister/load/unload lifecycle |
| Capability group management | Supported | `cap list groups` CLI command |
| Capability enable/disable | Supported | Per-capability enable/disable at runtime |
| Capability hot-reload | Supported | Load/unload without restart |

### Enabled Capability Groups (7 groups, 33 capabilities)

| Group | Cap Count | Status | Notes |
|-------|-----------|--------|-------|
| claw_memory (memory) | 5 | Supported | Full-mode memory: store, recall, extract, profile, lightweight |
| cap_lua | 16 | Supported | Lua 5.4 runtime, async Lua, full display module |
| cap_files | 8 | Supported | Read/write/delete/list host files (sandboxed) |
| cap_im_local (Web IM) | 2 | Supported | Built-in web chat on localhost |
| cap_session_mgr | 1 | Supported | Session create/switch/delete/list |
| cap_skill_mgr | 1 | Supported | Skill catalog, register/unregister/activate/deactivate |
| cap_router_mgr | 1 | Supported | Event routing rules |

### Disabled Capability Groups

| Group | Reason |
|-------|--------|
| `cap_scheduler` | Requires NVS struct stubs for schedule persistence |
| `cap_system` | Requires deep ESP-IDF structs (chip info, flash, heap layout) |
| `cap_time` | Requires SNTP integration (host has NTP, but API surface differs) |
| `cap_web_search` | Requires `esp_http_client` struct initialization (can be ported to libcurl) |

---

## Event Router

| Feature | Status | Notes |
|---------|--------|-------|
| Event publishing | Supported | Internal event bus, startup event published |
| Event subscriptions | Supported | Rule-based subscriptions |
| JSON rule management | Supported | Add/update/delete routing rules |
| Rule hot-reload | Supported | `event_router --reload` |
| Message emission | Supported | Manual `emit_message` for testing |

---

## Memory System

| Feature | Status | Notes |
|---------|--------|-------|
| Memory store | Supported | JSON file-based in `~/.crush-claw/memory/` |
| Memory extract | Supported | Extracts structured data from conversations |
| Memory recall | Supported | Context injection based on relevance |
| Memory profile | Supported | User profile persistence |
| Memory lightweight | Supported | Trimmed memory for token budget |
| Memory sessions | Supported | Session state persistence |
| Memory utils | Supported | File I/O helpers |

---

## Skills System

| Feature | Status | Notes |
|---------|--------|-------|
| Skill catalog | Supported | `~/.crush-claw/skills/skills_list.json` |
| Built-in skills | Supported | Lua runner (3 skills auto-created on first run) |
| Skill registration | Supported | Dynamic register/unregister at runtime |
| Skill activation | Supported | Skills auto-loaded as LLM system prompts |
| Skill deactivation | Supported | Per-skill toggle |

---

## CLI / Console

| Feature | Status | Notes |
|---------|--------|-------|
| Unix socket CLI | Supported | `~/.crush-claw/agent.sock`, one-shot request/response |
| `crush-claw` CLI tool | Supported | Cross-platform management CLI (config, start, stop, status, logs, build, etc.). Linux: shell script; Windows: compiled C binary. |
| Help command | Supported | All registered commands listed |
| `ask <prompt>` | Supported | Multi-turn agent prompt via socket |
| `ask_once <prompt>` | Supported | Single-turn prompt (no session) |
| `session [id]` | Supported | Show or switch session |
| `cap list\|call\|groups\|...` | Supported | Capability introspection |
| `auto reload\|rules\|...` | Supported | Autonomous agent rules |
| `lua --list\|--run\|...` | Supported | Lua script management (argtable3 parsed) |
| `event_router --rules\|...` | Supported | Event router management |
| `skill --catalog\|...` | Supported | Skill management |
| `display on\|off\|status` | Supported | SDL2 window hot-plug control |

---

## Display (SDL2 Simulated LCD)

### Display Hardware Abstraction (display_hal)

| HAL Function | Status | Notes |
|-------------|--------|-------|
| `display_hal_create` | Supported | SDL2 window + renderer + texture (RGB565) |
| `display_hal_destroy` | Supported | Proper SDL2 teardown, allows re-creation |
| `display_hal_width` / `display_hal_height` | Supported | Window dimensions (default 320x240) |
| `display_hal_begin_frame` | Supported | Lock texture for drawing |
| `display_hal_present` | Supported | Unlock + render + present + SDL event pump |
| `display_hal_present_rect` | Supported | Partial update |
| `display_hal_end_frame` | Supported | Frame lifecycle |
| `display_hal_is_frame_active` | Supported | Query frame state |
| `display_hal_get_animation_info` | Supported | EAF animation frame metadata |
| `display_hal_clear` | Supported | Fill with RGB565 color |
| `display_hal_set_clip_rect` / `clear_clip_rect` | Supported | Clipping region |
| `display_hal_set_backlight` | Partial | Stored, no hardware PWM (no visual effect) |
| `display_hal_fill_rect` | Supported | Solid rectangle fill |
| `display_hal_draw_rect` | Supported | Rectangle outline |
| `display_hal_draw_pixel` | Supported | Single pixel |
| `display_hal_draw_line` | Supported | Bresenham line |
| `display_hal_fill_circle` / `draw_circle` | Supported | Circle primitives |
| `display_hal_draw_arc` / `fill_arc` | Supported | Arc primitives |
| `display_hal_draw_ellipse` / `fill_ellipse` | Supported | Ellipse primitives |
| `display_hal_draw_round_rect` / `fill_round_rect` | Supported | Rounded rectangles |
| `display_hal_draw_triangle` / `fill_triangle` | Supported | Triangle primitives |
| `display_hal_measure_text` | Supported | ASCII text width/height via font table |
| `display_hal_draw_text` | Supported | ASCII-only text (bitmap font) |
| `display_hal_draw_text_aligned` | Supported | Aligned text (left/center/right + top/middle/bottom) |
| `display_hal_draw_bitmap` | Supported | Full bitmap at (x, y) |
| `display_hal_draw_bitmap_crop` | Supported | Cropped bitmap region |
| `display_hal_draw_bitmap_scaled` | Supported | Scaled bitmap (RGBA input) |
| `display_hal_draw_jpeg` | Supported | JPEG decode + draw (via emote libjpeg) |
| `display_hal_draw_jpeg_crop` | Supported | Cropped JPEG draw |
| `display_hal_draw_jpeg_scaled` | Supported | Scaled JPEG draw |
| `display_hal_jpeg_get_size` | Supported | JPEG dimensions without full decode |
| `display_hal_draw_png_file` | Supported | PNG decode (libpng) → RGB565 → draw |
| `display_hal_draw_rgb565_crop` | Supported | Pre-encoded RGB565 crop |
| `display_hal_draw_rgb565_scaled` | Supported | Pre-encoded RGB565 scale |
| `display_hal_draw_rgb565_fit` | Supported | Pre-encoded RGB565 fit-to-rect |

### Display Lua Module

| Feature | Status | Notes |
|---------|--------|-------|
| Module loading (`require("display")`) | **Supported** | Registered via `lua_module_display_register()` |
| `display.init()` | Supported | Calls `display_hal_create()` |
| `display.deinit()` | Supported | Calls `display_hal_destroy()` |
| `display.width()` / `display.height()` | Supported | Query dimensions |
| `display.clear(color)` | Supported | Fill screen with color |
| `display.set_clip_rect()` / `display.clear_clip_rect()` | Supported | Clipping |
| `display.fill_rect()` / `display.draw_rect()` | Supported | Rectangles |
| `display.draw_pixel()` | Supported | Pixels |
| `display.draw_line()` | Supported | Lines |
| `display.backlight(brightness)` | Partial | Stored, no hardware effect |
| `display.begin_frame()` / `display.present()` / `display.end_frame()` | Supported | Frame lifecycle |
| `display.present_rect()` | Supported | Partial frame update |
| `display.frame_active()` | Supported | Frame state query |
| `display.animation_info()` | Supported | EAF animation info |
| `display.measure_text()` / `display.draw_text()` / `display.draw_text_aligned()` | Supported | ASCII text (built-in fonts) |
| `display.draw_bitmap()` / `display.draw_bitmap_crop()` | Supported | Bitmap rendering |
| `display.draw_rgb565_crop()` / `draw_rgb565_scaled()` / `draw_rgb565_fit()` | Supported | Pre-encoded format |
| `display.draw_jpeg()` / `draw_jpeg_file()` | Supported | JPEG rendering |
| `display.draw_jpeg_crop()` / `draw_jpeg_file_crop()` | Supported | Cropped JPEG |
| `display.draw_jpeg_file_scaled()` / `draw_jpeg_file_fit()` | Supported | Scaled JPEG |
| `display.draw_png_file()` | Supported | PNG via libpng |
| `display.fill_circle()` / `draw_circle()` | Supported | Circles |
| `display.draw_arc()` / `fill_arc()` | Supported | Arcs |
| `display.draw_ellipse()` / `fill_ellipse()` | Supported | Ellipses |
| `display.draw_round_rect()` / `fill_round_rect()` | Supported | Rounded rectangles |
| `display.draw_triangle()` / `fill_triangle()` | Supported | Triangles |

**Summary:** The **display Lua module is fully supported**. Lua scripts can
`require("display")` and call all 30+ drawing functions. The underlying
`display_hal_*()` functions are all implemented in `display_sdl2.c` backed by SDL2.
The module is registered at startup via `lua_module_display_register()`.

### Display Hot-plug

| Feature | Status | Notes |
|---------|--------|-------|
| Config.json toggle (`display.enabled`) | Supported | Read at startup, controls window creation |
| CLI `display on` | Supported | Open SDL2 window at runtime |
| CLI `display off` | Supported | Close SDL2 window at runtime |
| CLI `display status` | Supported | Query active/inactive state |
| `crush-claw display on\|off\|status` | Supported | Forwarded over Unix socket |
| Window close button (X) | Supported | Auto-closes display, agent keeps running |
| Re-create after close | Supported | `display on` re-opens window after X close |

---

## Emote Animation Engine

| Feature | Status | Notes |
|---------|--------|-------|
| emote_init / emote_load | Supported | EAF animation loading and playback |
| emote_op (play, stop, pause, seek) | Supported | Animation control |
| emote_setup | Supported | Scene and style configuration |
| Default built-in fonts | Supported | Maison Neue Book (12, 26), Puhui Basic (20) |
| GFX engine (32 source files) | Supported | Full widget/animation/drawing stack |
| QR code generation | Supported | lib/qrcode compiled in |
| EAF decoder | Supported | Proprietary animation format |

---

## IM / Chat Channels

| Channel | Status | Notes |
|---------|--------|-------|
| Local IM (Web) | Supported | Built-in HTTP server on localhost, web UI |
| QQ Bot | Compiling | Compiled but requires live QQ credentials + network |
| Telegram Bot | Compiling | Compiled but requires bot token + network |
| Feishu/Lark | Compiling | Compiled but requires app credentials + network |
| WeChat | Compiling | Compiled but requires token + network |
| Attachment (shared helpers) | Supported | Download/upload helpers for all IM channels |

**Note:** QQ, Telegram, Feishu, and WeChat channels compile and register but
require valid credentials in `config.json` and network connectivity to their
respective APIs. They are untested in the simulator.

---

## File Operations (cap_files)

| Capability | Status | Notes |
|------------|--------|-------|
| File read | Supported | Sandboxed to `~/.crush-claw/` |
| File write | Supported | Sandboxed |
| File delete | Supported | Sandboxed |
| File list/directory | Supported | Sandboxed |
| File move/rename | Supported | Sandboxed |
| File stat/size | Supported | POSIX stat |

---

## Storage (NVS / Non-Volatile Storage)

| Feature | Status | Notes |
|---------|--------|-------|
| NVS init | Supported | Creates `~/.crush-claw/nvs.json` |
| NVS read/write/erase | Supported | Full key-value API, cJSON-backed |
| NVS namespace | Supported | Per-namespace key isolation |
| NVS blob | Supported | Base64-encoded JSON blobs |
| config.json | Supported | Read at startup, env var overrides |
| Session persistence | Supported | `role\ttext` line format |
| Memory persistence | Supported | JSON files in `~/.crush-claw/memory/` |
| Skills persistence | Supported | `~/.crush-claw/skills/skills_list.json` |
| Router rules persistence | Supported | `~/.crush-claw/router_rules/router_rules.json` |

---

## FreeRTOS / Runtime

| FreeRTOS API | Status | Notes |
|-------------|--------|-------|
| Task create/delete | Supported | pthread_create / pthread_cancel |
| Task delay | Supported | usleep |
| Task notifications | Supported | pthread thread-local counter |
| Queues | Supported | Ring buffer + mutex + cond var |
| Mutex (plain) | Supported | pthread_mutex_t |
| Recursive mutex | Supported | PTHREAD_MUTEX_RECURSIVE |
| Binary semaphore | Supported | pthread_mutex_t + cond var + count |
| Counting semaphore | Supported | pthread_mutex_t + cond var + count |
| Event groups | Supported | Bitmask + mutex + cond var |
| Tick count | Supported | gettimeofday → ms |
| Software timers | Supported | POSIX timer_create |
| Task priority | No-op | All threads equal on Linux |
| Task affinity / core pinning | No-op | No SMP core selection |
| ISR context | No-op | No interrupt service routines on Linux |

---

## Hardware Dependencies — NOT Supported

These require physical ESP32 hardware and are **excluded** from the simulator
build (stubs or not compiled):

### GPIO / IO

| Feature | Status | Notes |
|---------|--------|-------|
| GPIO input/output | Unsupported | Stub header only (`gpio_stub.h`), all calls no-op |
| GPIO interrupts | Unsupported | No hardware interrupts |
| GPIO pull-up/down | Unsupported | No hardware |
| I2C master/slave | Unsupported | Stub header only (`i2c_stub.h`) |
| SPI master/slave | Unsupported | Stub header only (`spi_stub.h`) |
| UART | Unsupported | Not stubbed |
| ADC (analog input) | Unsupported | Stub header only (`adc_stub.h`) |

### Display Hardware

| Feature | Status | Notes |
|---------|--------|-------|
| Physical LCD panel (SPI/8080/I2C) | Unsupported | Simulated via SDL2 window |
| LCD panel IO (esp_lcd_panel_io_*) | Unsupported | Stub (`esp_lcd_panel_stub.c`) |
| LCD touch controller | Unsupported | No touch HW simulation |
| MIPI DSI | Unsupported | No hardware |
| LVGL integration | Unsupported | Font struct stubs only (`lv_font_stub.c`) |

### Sensors

| Feature | Status | Notes |
|---------|--------|-------|
| IMU / accelerometer / gyroscope | Unsupported | No hardware |
| Magnetometer / compass | Unsupported | No hardware |
| Environmental (temp, humidity, pressure) | Unsupported | No hardware |
| Ambient light sensor | Unsupported | No hardware |
| Proximity sensor | Unsupported | No hardware |

### Audio

| Feature | Status | Notes |
|---------|--------|-------|
| Audio codec (ES8311/ES8388/etc.) | Unsupported | No hardware |
| I2S audio output | Unsupported | No hardware |
| Microphone input | Unsupported | No hardware |
| Speaker output | Unsupported | No hardware |
| Audio player (MP3/AAC/etc.) | Unsupported | No hardware codec |

### Other Hardware

| Feature | Status | Notes |
|---------|--------|-------|
| LED strip (WS2812/SK6812) | Unsupported | No hardware, no RMT peripheral |
| IR transmitter/receiver | Unsupported | No hardware |
| MCPWM (motor control) | Unsupported | No hardware |
| SD card (SPI/SDMMC) | Unsupported | Host filesystem used instead |
| Battery / power management | Unsupported | No hardware PMIC |
| USB OTG / device mode | Unsupported | No hardware |
| ESP32 RTC / deep sleep | Unsupported | No hardware |
| ESP32 efuse / secure boot | Unsupported | No hardware |

### Wireless

| Feature | Status | Notes |
|---------|--------|-------|
| WiFi station mode | Unsupported | Stub headers; actual TCP works via host network |
| WiFi AP mode | Unsupported | No WiFi radio |
| WiFi scan | Unsupported | No WiFi radio |
| Bluetooth Classic | Unsupported | No Bluetooth radio |
| BLE (Bluetooth Low Energy) | Unsupported | No Bluetooth radio |
| ESP-NOW | Unsupported | No ESP32 radio |
| Thread / Zigbee (802.15.4) | Unsupported | No radio |

### Lua Hardware Modules (not registered)

These Lua modules exist in upstream `esp-claw` but are **NOT enabled** in the
simulator build (`CONFIG_APP_CLAW_LUA_MODULE_*` defines not set):

| Module | Reason |
|--------|--------|
| `adc` | No ADC hardware |
| `gpio` | No GPIO hardware |
| `i2c` | No I2C hardware |
| `button` | No physical buttons (keyboard via SDL2 works) |
| `audio` | No audio codec |
| `camera` | No real camera (test pattern only) |
| `touch` | No touch controller |
| `imu` | No IMU sensor |
| `led_strip` | No LED strip hardware |
| `ir` | No IR hardware |

---

## Partially Supported Features

| Feature | Status | Details |
|---------|--------|---------|
| **Camera** | Partial | Test pattern generator only (`camera_stub.c`). Produces valid RGB565 frames — works for testing the display JPEG pipeline. No real camera capture. |
| **WiFi networking** | Partial | All TCP/UDP works transparently via host POSIX stack. LLM API calls, webhooks, IM channels all work. WiFi mode/config APIs are stubs. Cannot scan, cannot create AP. |
| **LCD backlight** | Partial | `display_hal_set_backlight()` stores the value, but has no visual effect (SDL2 window brightness is fixed). |
| **ESP32 chip info** | Partial | Board manager returns simulated LCD params. No real chip revision, flash size, or efuse data. |
| **Waiting/animations** | Partial | EAF animations play. Face/expression rendering depends on emote engine which is compiled. Full fidelity depends on asset data which may be incomplete in the simulator stub (`emote_stub.c`). |
| **Keyboard input** | Partial | SDL2 keyboard events are captured in the display event loop. Not integrated as a Lua `button` module. Can be used for text input in the console. |
| **Session reasoning persistence** | Partial | Reasoning content preserved within a single-request tool-calling loop. Lost when session is persisted to disk (`role\ttext` line format does not include `reasoning_content`). |

---

## Build & Tooling

| Feature | Status | Notes |
|---------|--------|-------|
| CMake build (Release) | Supported | `crush-claw build` (Linux + Windows) |
| CMake build (Debug) | Supported | `cmake -DCMAKE_BUILD_TYPE=Debug` |
| `.deb` packaging (Linux) | Supported | `dpkg-buildpackage` |
| `.zip` packaging (Windows) | Supported | `package.bat` with PowerShell |
| systemd user service | Supported | Linux only; `crush-claw service enable\|start\|stop\|status` |
| `crush-claw config` wizard | Supported | Interactive setup (cross-platform) |
| `crush-claw clean` | Supported | Remove build/ directory |
| `_run_desktop.sh` dev script | Supported | Linux foreground run |
| `_run_desktop.bat` dev script | Supported | Windows quick launcher (run/build/debug/clean/daemon) |
| Platform abstraction layer | Supported | `platform.h` / `platform_posix.h` / `platform_win32.h` |
| Windows Named Pipe IPC | Supported | `\\.\pipe\crush-claw` replacing Unix socket |
| MinGW-w64 (MSYS2) build | Supported | CMake + MinGW Makefiles generator |

---

## Upstream esp-claw Features Not Yet Examined

| Feature | Location | Reason |
|---------|----------|--------|
| Multi-agent coordination | `claw_multi_agent` | Not compiled, not explored |
| Agent-to-agent messaging | `claw_multi_agent` | Not compiled |
| Full scheduler with NVS | `cap_scheduler` | Excluded, NVS struct dependency |
| Web search capability | `cap_web_search` | Excluded, HTTP struct dependency |
| System/device info capability | `cap_system` | Excluded, deep ESP32 structs |
| SNTP time capability | `cap_time` | Excluded, SNTP API gap |
| OTA firmware update | Unknown | Not applicable to simulator |
| Secure storage / encryption | Unknown | AES stub exists, not integrated |
| Factory reset | Unknown | Not applicable to simulator |

---

*Generated 2026-05-03 from source audit of esp-claw (submodule) and sim_hal.*
