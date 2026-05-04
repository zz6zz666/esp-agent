ESP-Claw Codebase Architecture Report

1. Overall Directory Structure
   application/edge-agent/        ~ The main firmware application
   application/edge-agent/CMakeLists.txt  ~ Top-level project CMake (entry point for idf.py)
   Boards/                    ~ Board support packages (M5Stack, LilyGo, Espressif devkits, etc.)
   components/                ~ Edge-application specific components (cap config, LED strip, camera, esp_wifi)
   main/                      ~ Application entry point (main.c) + Lua scripts + skills
   fatfs-image/               ~ FAT filesystem image contents (memory, router-rules, scheduler, static)
   patches/                   ~ Build patches, scripts (cmake patches, sync scripts)
   components/
   claw_modules/              ~ Core framework: claw_core, claw_cap, claw-event-router, claw_memory, claw_skill
   claw_capabilities/         ~ All capability modules (IM, Lua, MCP, scheduler, web_search, etc.)
   hw_modules/                ~ Board-specific modules for hardware (GPIO, I2C, display, camera, etc.)
   common/                    ~ Shared utilities (gpio helper, display, button, esp_wifi, settings, wifi_manager)
   docs/                         ~ Astro-based documentation website
2. Languages and Proportions| Extension | Count | Role                                                 |
   | --------- | ----- | ---------------------------------------------------- |
   | .c        | 138   | Main firmware implementation                         |
   | .h        | 101   | C headers                                            |
   | .lua      | 30    | Lua scripting (demo scripts + hardware test scripts) |
   | yaml/.yml | 76    | Board/peripheral/device configuration, CI config     |
   | .md       | 51    | Documentation, skill definitions                     |
   | .py       | 6     | Build/utility scripts (board tooling, sync scripts)  |
   | .cmake    | 4     | CMake build infrastructure patches                   |
   | .json     | 50    | Skills list, schedules, router rules, manifests      |

The project is overwhelmingly C (embedded C99/C11 with ESP-IDF framework. Lua is used as a runtime scripting layer for user-defined behaviors. Python is minimal (build tooling only).

---

3. Lua Scripting System
   Architecture: Three Layers
4. Lua Runtime (cap_lua) -- components/claw_capabilities/cap_lua/
   - Manages a Lua 5.4.x state (embedded via ESP-IDF's lua component).
   - cap_lua_register_group() registers the Lua capability as a claw_cap group -- this means the LLM agent can invoke Lua scripts as "tools".
   - cap_lua_run_script() loads a .lua file from FATFS, creates a fresh Lua state, opens all registered modules, sets up args global from JSON, hooks print and timeout, and executes via luaL_dofile().
   - Runs user-defined Lua scripts in a background FreeRTOS task with lifecycle management (init, list, stop, all_stop).
   - Timeout hook runs every 100 lua instructions via lua_sethook.
   - Runline run functions are called after each script execution.
   - An "honesty observer" (cap_lua_honesty_observe) monitors agent completions and auto-triggers scripts based on LLM responses.
5. Lua Module Registration (cap_lua_modules) -- components/common/app_claw/app_lua_modules.c
   - Each module is exposed to Lua with an API registered via claw_lua_module_register(name, open_fn).
   - Each module is conditionally compiled based on Kconfig (CONFIG_APP_CLAW_LUA_MODULE_*).
   - Modules are enabled/disabled by user configuration using enabled_lua_modules.
6. Hardware Lua Modules -- components/hw_modules/
   - Each is a C module that exports functions via luaopen_* (e.g., luaopen_gpio, luaopen_display).
   - Each module defines a public table of C functions callable from Lua.
   - Examples of modules: adc, audio, button, camera, delay, dht, display, environmental_sensor, fuel_gauge, gpio, i2c, imu, ir, knob, lcd, lcd_touch, led_strip, magnetometer, ntpcm, sd1306, system, toucher, uart.

Lua Script Location:

- application/edge-agent/main/lua_scripts/ -- User-facing demo scripts (flyspinner, camera preview, clock dial, LCD touch paint, audio FFT).
- components/*/*/lua_scripts/ -- Documentation/example scripts shipped per module (e.g., basic_gpio.lua, basic_i2c.lua).
- At runtime, scripts are loaded from /fatfs/scripts/ (the FATFS partition). Built-in scripts are synced to /fatfs/scripts/ during build.

How the LLM invokes Lua:
The cap_lua capability group exposes tools like run_lua_script, list_lua_scripts, write_lua_script to the LLM. When the LLM decides to run a script, it calls cap_lua_run_script(), which loads the file from storage and executes it.

---

4. Hardware Abstraction Layer
   The HAL pattern is multi-layered:
   A) Display HAL (display_hal.h / display_hal.c)

- File: components/common/display/include/display_hal.h
- Defines a common abstract interface for display operations: create/destroy, control, drawing primitives (pixel, line, rect, circle, arc, ellipse, triangle, round_rect), text drawing (with alignment), bitmap/JPEG rendering.
- The implementation in display_hal.c directly uses ESP-IDF LCD panel APIs (esp_lcd_panel_io, esp_lcd_panel_ops), with specializations for RGB panels and MIPI DSI.
- The board must call display_hal_create() with a panel_handle and io_handle (obtained from board manager device instantiation).
- The lua_module_display Lua binding calls through display_hal_* functions -- it does NOT call ESP-IDF LCD APIs directly.
  B) Display Arbiter (display_arbiter.h)
- File: components/common/display_arbiter/include/display_arbiter.h
- Manages shared display access between Lua (user scripts) and Emote (system UI).
- Uses a semaphore to arbitrate ownership, with emote using LUA_DISPLAY_ARBITER_OWNER_EMOTE.
- Provides acquire/release with ownership-change callbacks.
  C) Board Abstraction (esp_board_manager)
- NOT in this repo directly -- it's an ESP-IDF component dependency.
- Board definition is via a file in application/edge-agent/hw/vendors/`<board>`/:
  - board_info.yml -- Board identity (name, manufacturer)
  - board_peripherals.yml -- Pin assignments and peripheral configs (SPI, I2C, RMT, I2S, LEDC)
  - board_devices.yml -- Device configurations (displays, cameras, audio codecs, LED strips) referencing peripherals and IDF component dependencies
- The board manager handles device initialization (USB host for USB cameras, power management, control IC expanders):
  - i2c_bus_handle_t -- handles device creation: given a YAML device description, it creates the appropriate ESP-IDF driver handles (e.g., esp_lcd_panel_handle_t, i2c_bus_handle_t).
    D) Sensor Lua Modules (direct ESP-IDF usage)
- Most sensor modules (GPIO, I2C, UART, magnetometer, etc.) call ESP-IDF driver APIs directly. They are NOT abstracted behind a project-defined HAL -- they depend on esp-idf peripherals (driver/gpio, driver/i2c.h, etc.).
- The Lua module for GPIO, for example, directly calls gpio_set_direction(), gpio_set_level(), etc.
  E) Camera Abstraction
- The lua_module_camera uses esp-video from ESP-IDF. The board's setup_device.c handles USB/OV camera initialization; the Lua module deals with frames.
  F) I2C Bus Abstraction
- lua_module_i2c uses i2c-bus.h from ESP-IDF's component registry, providing a bus/device handle model exposed to Lua.

Key insight for simulation: The display_hal.h is the cleanest HAL boundary. Most other "HALs" are just direct ESP-IDF driver calls, which means a simulator would need to either stub ESP-IDF driver headers or provide a POSIX-compatible reimplementation.

---

5. Agent Execution Framework
   The agent framework is composed of these interconnected modules:
   A) claw_core -- The Agent Loop

- File: components/claw_modules/claw_core/
- This is the central "brain". It runs as a FreeRTOS task.
- claw_core_init(config) sets up LLM backend, context providers, and capability callbacks.
- claw_core_start() launches the agent task.
- The agent runs a loop (claw_core.c main loop, plus claw_core_llm.c):
  a. Wait for requests on a queue (request_queue)
  b. Collect context from providers (conversation, memory, skills, capabilities, time, etc.)
  c. Call the LLM (claw_core_llm.chat_messages()) with system prompt, message history, and tool definitions
  d. If the LLM returns tool calls, executes them via cap_call_callback (which routes to claw_cap_call_from_core)
  e. Feed the tool results back to the LLM for the next iteration (up to max_tool_iterations, default 32)
  f. When the LLM produces a final answer, delivers it on the response_queue, and notifies completion observers
- LLM Backend abstraction: claw_llm_backend_openai, claw_llm_backend_anthropic, claw_llm_backend_custom.c.
- HTTP transport is handled by claw_llm_http_transport.c.
  B) claw_cap -- Capability Registry
- File: components/claw_modules/claw_cap/
- A registry of all tools exposed to the LLM. Each capability has:
  - Kind: CALLABLE, EVENT_SOURCE, or HYBRID
  - Flags: CALLABLE_BY_LLM, EVENTS_EVENTS, etc.
  - State: REGISTERED, STARTED, DISABLED, DRAINING, UNLOADING
  - Lifecycle callbacks: init, start, stop
  - An execute function that processes JSON input and produces JSON output
  - An input schema for LLM tool declaration
- Capabilities are grouped into claw_cap_group_t with shared lifecycle.
- claw_cap_set_llm_visible_groups() controls which groups the LLM can call as tools.
- claw_cap_build_tool_json() renders all visible capabilities into the JSON tool format expected by OpenAI/Anthropic APIs.
  C) claw_event_router -- Event-Driven Messaging
- File: components/claw_modules/claw_event_router/
- Routes events based on JSON rules loaded from router_rules.json
- Each rule specifies: when (callable, event, skill, trigger, event-key, channel, chat-id, sender-id, text, payload_json.
- Rule actions include: CALL_CAP, RUN_AGENT, RUN_SCRIPT, SEND_MESSAGE, EMIT_EVENT, DROP.
- When a message arrives from any IM channel (QQ, Telegram, WeChat, Feishu, local web), the event router processes it. If no rule matches and default_route_messages_to_agent is true, the message goes to the agent for LLM processing.
- Outbound messaging uses logical channels (e.g., "qq", "telegram") to capability names.
- Startup event publishing triggers any rules that match "startup/boot_completed".
  D) claw_memory -- Structured Memory
- File: components/claw_modules/claw_memory/
- Stores structured memory items with summaries, tags, and keywords.
- Supports async extraction from LLM responses for long-term memory and LIGHTWEIGHT.
- Provides context providers for the agent (file profile provider, long-term memory provider, session history provider).
  E) claw_skill -- Skill System
- File: components/claw_modules/claw_skill/
- Skills are markdown documents that describe how to use certain capabilities.
- A skills_list.json catalogs available skills. Each skill can activate certain capability groups.
- The LLM can activate/deactivate skills via capability capabilities.
- The skills list and active skill docs are injected as context providers to the agent loop.
  F) Capabilities (cap_*) -- all in components/claw_capabilities/
  Each is a claw_cap_group that registers one or more claw_cap_descriptor_t entries. The table of registered groups:

| Group ID        | Description                                                  |
| --------------- | ------------------------------------------------------------ |
| cap_im_qq       | QQ IM integration                                            |
| cap_im_feishu   | Feishu/Lark IM                                               |
| cap_im_tg       | Telegram bot                                                 |
| cap_im_wechat   | WeChat integration                                           |
| cap_im_local    | Web-based local IM                                           |
| cap_files       | File system operations (list, read, write)                   |
| cap_scheduler   | Time-based event scheduling                                  |
| cap_lua         | Lua script execution (run, list, write, async jobs)          |
| cap_mcp_client  | MCP client (discover, call tools on external MCP servers)    |
| cap_mcp_server  | MCP server (expose ESP-Claw capabilities as MCP tools)       |
| cap_skill       | Skill management (activate/deactivate skills)                |
| cap_system      | System info and control (reboot, uptime, etc.)               |
| claw_memory     | Memory CRUD operations (store, recall, update, forget, list) |
| cap_time        | Time sync and time queries                                   |
| cap_llm_inspect | LLM request inspection                                       |
| cap_web_search  | Web search (Brave, Tavily APIs)                              |
| cap_router_mgr  | Event router rule management                                 |
| cap_session_mgr | Session management                                           |
| cap_boards      | Board management and configuration                           |

---

6. Build System
   The project uses ESP-IDF's CMake-based build system (idf.py wrapper).

Key files:
application/edge-agent/CMakeLists.txt -- Top-level project: includes ESP-IDF's project.cmake, applies build patches, sets up FATFS image generation and skill/Lua script synchronization
application/edge-agent/tools/cmake/ -- Custom CMake modules:
    - esp_idf_patch.cmake -- Patches ESP-IDF behavior
    - flash_partition_default.cmake -- Default partition table
    - board_manager_patch.cmake -- Board manager integration
    - fatfs_image.cmake -- Sync scripts, skills, and rules into the FATFS image automatically
application/edge-agent/tools/ -- Python scripts:
    - bmp_patch.py -- Board manager patching
    - sync_component_lua_scripts.py -- Copies Lua scripts from component directories into the FATFS image
    - sync_component_skills.py -- Copies skills from each components directory into the FATFS image
    - kconfig/ -- Kconfig definitions and defaults, following standard ESP-IDF component conventions.

- Iconfig files are present in most components for compile-time configuration.
- CI is managed via .github-ci.yml with custom Python scripts in .gitlab-ci/.

The build produces a firmware binary with a FATFS SPI flash partition containing: scripts, skills, memory files, router rules, and scheduler definitions.

---

7. Key Dependencies and Libraries
   ESP-IDF Framework (the core dependency):

- driver/gpio.h, driver/i2c.h, driver/spi_master.h, driver/ledc.h, driver/mcpwm.h, driver/touch_sensor.h, driver/adc.h, driver/uart.h
- esp_lcd -- LCD panel drivers (ST7789, etc.)
- esp_video -- Camera/video pipeline
- esp-timer, freertos/FreeRTOS.h, nvs_flash, esp_vfs_fat -- System services
- esp_hosted, usb/usb_host.h -- USB camera and audio support (some boards)
- esp_board_manager -- Board abstraction layer (YAML-driven device instantiation)

Third-party libraries pulled via IDF component registry:

- lua (5.4) -- Embedded Lua interpreter
- cJSON -- JSON parsing throughout
- led_strip -- Addressable LED driver
- i2c_dev, i2cdevices -- I2C bus abstraction and device drivers
- dht -- DHT temperature/humidity sensor driver
- esp_io_expander_and955 -- IO expander (M5Stack CoreS3 specific)

Internal/Project Libraries:

- esp_painter -- Framebuffer-based rendering engine with font support
- display_arbiter -- Shared display ownership management
- settings -- System UI/menu
- settings -- Configuration persistence

---

8. Recommendations for Building a Desktop Simulator
   Goal: Reuse the upper-layer code (agent framework, Lua runtime, capabilities, event router, memory, skills) on a desktop platform (Linux/macOS/Windows) without ESP32 hardware.

Architecture strategy -- three layers of concern:
Layer 1 -- Needs Full Replacement: Hardware HAL and ESP-IDF shim
The biggest surface. You need a POSIX compatibility layer or reimplements:

- FreeRTOS APIs (tasks, queues, semaphores, timers) -- use pthreads and libuv or a lightweight RTOS shim
- ESP-IDF driver headers (esp_lcd.h, esp_log.h, driver/gpio.h, etc.) -- stub or return null values
- NVS -- replace with a file/JSON store; nvs_flash_* -- redirect to malloc/free
- vfs_fat -- redirect to a flat file
- esp_lcd_panel_* -- redirect to SDL2 or similar for display

Layer 2 -- Can Reuse with Minimal Changes: Agent Framework

- claw_core -- The agent logic is hardware-independent (HTTP to LLM, tool call routing, context providers). It depends on FreeRTOS queues/semaphores which can be replaced with pthread mutex/condvar.
- claw_event_router -- Pure registry logic, no hardware dependency. Reusable directly.
- claw_memory -- Pure file I/O for most systems. Reusable (needs file I/O for rules (POSIX compatible).
- claw_skill -- Pure YAML/JSON registry. Reusable directly.
- All cap_* capability modules -- Hardware-neutral except cap_lua, MCP, web search and hardware-independent. The hardware-dependent ones (cap_lua runs scripts, cap_system reads ESP-specific info) need stubbing.

Layer 3 -- Needs Per-Platform Backend: Lua Hardware Modules
The Lua hardware modules (lua_module_gpio, lua_module_i2c, lua_module_display, etc.) directly call ESP-IDF driver APIs. For simulation you have two approaches:

- Stub approach: Replace each ESP-IDF driver call with a no-op or a simulated value. The Lua scripts run but interact with virtual hardware.
- Native backend approach: Map GPIO/I2C/SPI operations to desktop APIs (e.g., display to SDL, GPIO/I2C to virtual device files or sockets).

Recommended approach:

1. Create a sim/ directory with POSIX reimplementations of the ESP-IDF APIs that the framework needs.
2. Use CMake's add_compile_definitions to select between CONFIG_IDF_TARGET (real build) and a new SIMULATOR_BUILD define.
3. Use FreeRTOS add-ons with a thin pthreads wrapper providing TaskCreate, QueueSend, and semaphores.
4. For the display interface use SDL2 or similar. If the interface is clean and well-defined.
5. Map the FATFS partition contents (scripts, skills, rules) to a local directory, adjust the file paths.
6. The HTTP transport for LLM calls (claw_llm_http_transport.c uses ESP-IDF's HTTP Client -- replace with libcurl or platform native HTTP.
7. The file system should use real OS directories instead of FATFS -- the paths are already configurable through app_claw_storage_paths_t.

Key files to start with for the simulation port:

- components/claw_modules/claw_core/ -- Agent loop (core logic is portable)
- components/claw_modules/claw_cap/ -- Capability registry (fully portable)
- components/claw_modules/claw_event_router/ -- Event routing (portable with threading shim)
- components/claw_modules/claw_memory/ -- Memory (portable file I/O)
- components/claw_modules/claw_skill/ -- Skills (portable file I/O)
- components/claw_capabilities/cap_lua/ -- Lua runner (portable if lua library is available)
- components/claw_capabilities/cap_im_local/ -- Local IM (perfect for desktop testing)
- components/common/settings/ -- Settings logic (portable I/O)
- components/common/app_claw/ -- Application glue (portable with minor changes)

Estimated effort: Building a minimal simulation stub with the agent loop with Lua scripting, memory, skills, and event routing should take approximately 2-4 weeks for a software developer familiar with ESP-IDF internals. Adding display simulation (SDL) and hardware Lua module stubs would add another 1-2 weeks.
