# CLAUDE.md — esp-claw Desktop Simulator

## Project Overview

Linux desktop test environment for the `esp-claw` embedded AI agent framework. Validate agent logic (LLM calls, tool routing, Lua scripting, event routing, memory, skills) without ESP32 hardware.

- `esp-claw/` — upstream repo (read-only reference, do not modify)
- `sim_hal/` — our independent simulator layer (ESP-IDF/FreeRTOS stubs for desktop)
- `esp-agent` — CLI tool for managing the agent service
- `report.md` — full architecture reference of the upstream codebase

## Quick Start

```bash
# First time
./esp-agent config     # Full setup wizard (LLM, channels, search, display)
./esp-agent build      # Compile the binary
./esp-agent start      # Start the agent (background daemon)
./esp-agent ask "Hello"  # Send a prompt to the agent
./esp-agent stop       # Shut down

# Or run in foreground for development:
./_run_desktop.sh run
```

## CLI Tool (`./esp-agent`)

### Management commands (handled locally)

| Command     | Description                                             |
| ----------- | ------------------------------------------------------- |
| `config`  | Interactive first-time setup (LLM keys, display toggle) |
| `start`   | Start agent as background daemon (auto-tails logs)     |
| `stop`    | Graceful shutdown via SIGTERM                           |
| `restart` | Stop then start the agent                               |
| `status`  | Check if agent is running (PID, socket, config)         |
| `logs`    | Tail the agent log file                                 |
| `service` | Systemd user service management (enable/disable/start/stop/status) |
| `build`   | cmake + make (Release mode)                             |
| `clean`   | Remove build/ directory                                 |

### Agent commands (forwarded to REPL)

`esp-agent --help` shows management commands, then appends the REPL's
own `help` output.  `esp-agent esp-claw help` shows only the REPL help.

Every command not in the management table above is forwarded directly to
the agent's built-in CLI over the Unix socket (one-shot request/response).
Examples:
- `esp-agent ask "Hello"` — multi-turn prompt
- `esp-agent session` — show/switch session
- `esp-agent display on|off|status` — SDL2 window control
- `esp-agent lua --list` — list Lua scripts
- `esp-agent cap list` — list capabilities
- `esp-agent hello` → REPL returns "Unknown command: hello"

## Data Directory (`~/.esp-agent/`)

```
~/.esp-agent/
├── config.json              # LLM, channels, search keys, display
├── agent.sock               # Unix domain socket for CLI connect
├── agent.pid                # Process ID (running)
├── skills/
│   ├── skills_list.json     # Skill registry
│   ├── lua_runner.md        # Lua skill doc
│   ├── memory_ops.md        # Memory skill doc
│   └── file_ops.md          # File ops skill doc
├── scripts/builtin/         # Lua scripts
├── router_rules/            # router_rules.json
├── scheduler/               # schedules.json
├── sessions/                # Session state
├── memory/                  # Memory store
└── inbox/                   # IM attachments
```

### config.json structure

```jsonc
{
  "llm": {
    "api_key": "...",         // LLM API key
    "model": "gpt-4o",        // Model name
    "profile": "openai",      // openai | anthropic | custom_openai_compatible
                           //
                           // Preset providers (fixed base_url):
                           //   deepseek           → anthropic backend
                           //   dashscope          → openai_compatible backend
                           //   dashscope_coding   → anthropic backend
                           //   volcengine         → openai_compatible backend
                           //   volcengine_coding  → anthropic backend
                           //   minimax            → anthropic backend
    "base_url": "",           // Custom API base URL (empty = default)
    "auth_type": "",          // Auth type (empty = auto)
    "timeout_ms": "120000",   // Request timeout
    "max_tokens": "8192"      // Max output tokens
  },
  "channels": {
    "local_im": { "enabled": true },    // Built-in web IM (always available)
    "qq": {                             // QQ Bot (requires ESP32)
      "enabled": false,
      "app_id": "", "app_secret": ""
    },
    "telegram": {                       // Telegram Bot (requires ESP32)
      "enabled": false,
      "bot_token": ""
    },
    "feishu": {                         // Feishu/Lark (requires ESP32)
      "enabled": false,
      "app_id": "", "app_secret": ""
    },
    "wechat": {                         // WeChat (requires ESP32)
      "enabled": false,
      "token": "",
      "base_url": "https://ilinkai.weixin.qq.com",
      "cdn_base_url": "https://novac2c.cdn.weixin.qq.com/c2c",
      "account_id": "default"
    }
  },
  "search": {
    "brave_key": "",          // Brave Search API key
    "tavily_key": ""          // Tavily Search API key
  },
  "display": {
    "enabled": true           // SDL2 simulated LCD window
  }
}
```

Environment variable overrides: `LLM_API_KEY`, `LLM_MODEL`, `LLM_PROFILE`, `LLM_BASE_URL`,
`LLM_AUTH_TYPE`, `QQ_APP_ID`, `QQ_APP_SECRET`, `TG_BOT_TOKEN`, `FEISHU_APP_ID`,
`FEISHU_APP_SECRET`, `WECHAT_TOKEN`, `BRAVE_SEARCH_KEY`, `TAVILY_SEARCH_KEY`.

## Architecture

### Reused unchanged from esp-claw (hardware-independent C code)

| Module            | Path in esp-claw                                                    | Status                              |
| ----------------- | ------------------------------------------------------------------- | ----------------------------------- |
| claw_core         | `components/claw_modules/claw_core/` — agent loop                | compiled (runs when LLM configured) |
| claw_cap          | `components/claw_modules/claw_cap/` — capability registry        | compiled, working                   |
| claw_event_router | `components/claw_modules/claw_event_router/` — event routing     | compiled, working                   |
| claw_memory       | `components/claw_modules/claw_memory/` — memory store            | compiled, working                   |
| claw_skill        | `components/claw_modules/claw_skill/` — skill system             | compiled, working                   |
| cap_lua           | `components/claw_capabilities/cap_lua/` — Lua execution          | compiled, working                   |
| cap_im_local      | `components/claw_capabilities/cap_im_local/` — web IM            | compiled, working                   |
| cap_files         | `components/claw_capabilities/cap_files/` — file ops             | compiled, working                   |
| cap_session_mgr   | `components/claw_capabilities/cap_session_mgr/` — sessions       | compiled, working                   |
| cap_skill_mgr     | `components/claw_capabilities/cap_skill_mgr/` — skill activation | compiled, working                   |
| cap_router_mgr    | `components/claw_capabilities/cap_router_mgr/` — router rules    | compiled, working                   |
| app_claw + CLI    | `components/common/app_claw/` — app glue + CLI REPL              | compiled, working                   |
| cap_scheduler     | `components/claw_capabilities/cap_scheduler/` — cron             | excluded (NVS struct stubs)         |
| cap_system        | `components/claw_capabilities/cap_system/` — device info         | excluded (deep ESP structs)         |
| cap_time          | `components/claw_capabilities/cap_time/` — SNTP time             | excluded (SNTP stubs)               |
| cap_web_search    | `components/claw_capabilities/cap_web_search/` — search          | excluded (esp_http_client struct)   |

### Our sim_hal layer

```
sim_hal/
├── include/
│   ├── argtable3/            # Minimal argtable3 stub header
│   │   └── argtable3.h
│   ├── esp/                  # ESP-IDF stub headers
│   │   ├── esp_err.h         # Error codes + ESP_ERROR_CHECK
│   │   ├── esp_log.h         # printf-based logging
│   │   ├── esp_console.h     # Console API → Unix socket transport
│   │   ├── esp_heap_caps.h   # malloc stats stubs
│   │   ├── esp_system.h      # esp_restart → exit(0)
│   │   ├── esp_random.h      # → rand()
│   │   ├── esp_timer.h       # → gettimeofday
│   │   ├── esp_check.h       # ESP_RETURN_ON_ERROR etc macros
│   │   ├── esp_event.h       # event loop stub
│   │   ├── esp_netif.h       # netif stub
│   │   ├── esp_crt_bundle.h  # TLS cert no-op
│   │   ├── esp_http_client.h # Macro stubs
│   │   ├── esp_wifi.h        # WiFi mode stubs
│   │   ├── esp_sntp.h        # SNTP config stubs
│   │   ├── nvs_flash.h       # NVS → JSON file
│   │   ├── nvs.h             # NVS key-value API
│   │   ├── gpio_stub.h       # GPIO no-ops
│   │   ├── i2c_stub.h        # I2C no-ops
│   │   ├── spi_stub.h        # SPI no-ops
│   │   └── adc_stub.h        # ADC no-ops
│   └── freertos/             # FreeRTOS API stub headers
│       ├── FreeRTOS.h        # Types + all API declarations
│       ├── task.h → FreeRTOS.h
│       ├── queue.h → FreeRTOS.h
│       ├── semphr.h → FreeRTOS.h
│       ├── projdefs.h        # pdPASS/pdFAIL
│       └── portmacro.h       # portTICK_PERIOD_MS
├── freertos_shim.c            # FreeRTOS → pthread implementation
├── console_unix.c             # esp_console → Unix socket (one-shot RPC)
├── nvs_stub.c                 # NVS flash init stub
├── nvs.c                      # Full NVS key-value store (cJSON)
├── http_curl.c                # LLM HTTP transport (libcurl)
├── display_sdl2.c             # Display adapter (SDL2)
├── camera_stub.c              # Test pattern generator
├── base64.c                   # Minimal base64 encode/decode
├── cJSON.c + cJSON.h          # cJSON v1.7.18
└── CMakeLists.txt             # (unused — built from root)
```

### FreeRTOS → POSIX shim

| FreeRTOS API                       | POSIX equivalent                               |
| ---------------------------------- | ---------------------------------------------- |
| xTaskCreate                        | pthread_create                                 |
| vTaskDelete                        | pthread_cancel + pthread_join                  |
| vTaskDelay                         | usleep                                         |
| xQueueCreate/Send/Receive          | ring buffer + pthread_mutex_t + pthread_cond_t |
| xSemaphoreCreateMutex/Take/Give    | pthread_mutex_t + pthread_cond_t               |
| xSemaphoreCreateRecursiveMutex     | PTHREAD_MUTEX_RECURSIVE                        |
| xSemaphoreCreateBinary             | pthread_mutex_t + pthread_cond_t (count=0)     |
| xTaskGetTickCount                  | gettimeofday → ms                             |
| xTaskNotifyGive / ulTaskNotifyTake | pthread_key thread-local counter               |

## Agent CLI Commands (forwarded over Unix socket)

```
help                    List all registered commands
ask <prompt>            Multi-turn agent prompt
ask_once <prompt>       Single-turn prompt (no session)
session [id]            Show or switch session
cap list|call|groups|enable|disable|unload|load
auto reload|rules|rule|add_rule|update_rule|delete_rule|last|emit_message|emit_trigger
lua --list|--write|--run|--run-async|--jobs|--job  (argtable3 parsed)
event_router --rules|--rule|--add-rule-json|--update-rule-json|--delete-rule|--reload|--emit-message
skill --catalog|--register|--unregister|--list|--activate|--deactivate|--clear
```

## Build

### Prerequisites

```bash
sudo apt install build-essential cmake pkg-config \
  libcurl4-openssl-dev liblua5.4-dev libsdl2-dev libjson-c-dev
```

### Dev build (Debug)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Release build

```bash
./esp-agent build
```

## CONFIG defines (CMakeLists.txt)

| Define                                 | Status  |
| -------------------------------------- | ------- |
| `CONFIG_APP_CLAW_CAP_LUA=1`          | enabled |
| `CONFIG_APP_CLAW_CAP_IM_LOCAL=1`     | enabled |
| `CONFIG_APP_CLAW_CAP_SESSION_MGR=1`  | enabled |
| `CONFIG_APP_CLAW_CAP_FILES=1`        | enabled |
| `CONFIG_APP_CLAW_CAP_SKILL_MGR=1`    | enabled |
| `CONFIG_APP_CLAW_CAP_ROUTER_MGR=1`   | enabled |
| `CONFIG_APP_CLAW_MEMORY_MODE_FULL=1` | enabled |
| `CONFIG_APP_CLAW_ENABLE_CLI=1`       | enabled |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT=1`  | enabled |
| `SIMULATOR_BUILD`                    | enabled |

## Verified at runtime

All 7 capability groups register, CLI REPL serves 9 commands over Unix socket:

```
[I] [claw_skill] Initialized registry with 3 skill(s)
[I] [app_capabilities] Register local / Web IM cap ok (groups=1, caps=2)
[I] [app_capabilities] Register files cap ok (groups=2, caps=8)
[I] [cap_lua_rt] Lua runtime ready
[I] [app_capabilities] Register Lua cap ok (groups=3, caps=16)
[I] [app_capabilities] Register skill cap ok (groups=4, caps=21)
[I] [app_capabilities] Register claw_memory group ok (groups=5, caps=26)
[I] [app_capabilities] Register router manager cap ok (groups=6, caps=32)
[I] [app_capabilities] Register session manager cap ok (groups=7, caps=33)
[I] [console_unix] Unix socket ready at ~/.esp-agent/agent.sock
```

## Test Checklist

- [X] FreeRTOS shim (tasks, queues, semaphores, recursive mutex, binary sem, notifications)
- [X] NVS storage (JSON file read/write)
- [X] Event router (startup event published/processed)
- [X] Memory init (root dir, extract disabled without LLM)
- [X] Skills init (3 skills loaded, auto-created on first run)
- [X] All 7 capability groups registered (33 capabilities)
- [X] Unix socket CLI (help, ask, cap, auto, lua, event_router, skill, session)
- [X] esp-agent CLI tool (config/start/stop/status/logs/ask/display/build/clean)
- [X] config.json read + env var overrides
- [X] Auto-create ~/.esp-agent/ on first run
- [ ] LLM API call (set api_key + model in config.json)
- [ ] cap_im_local serves web UI on localhost
- [ ] cap_lua runs Lua scripts
- [ ] Skills auto-load into LLM context
- [ ] Memory store/recall in LLM session
- [ ] Display toggle (SDL2 window on/off via config.json)
