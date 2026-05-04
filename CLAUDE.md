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

| Command     | Description                                                        |
| ----------- | ------------------------------------------------------------------ |
| `config`  | Interactive first-time setup (LLM keys, display toggle)            |
| `start`   | Start agent as background daemon (auto-tails logs)                 |
| `stop`    | Graceful shutdown via SIGTERM                                      |
| `restart` | Stop then start the agent                                          |
| `status`  | Check if agent is running (PID, socket, config)                    |
| `logs`    | Tail the agent log file                                            |
| `service` | Systemd user service management (enable/disable/start/stop/status) |
| `build`   | cmake + make (Release mode)                                        |
| `clean`   | Remove build/ directory                                            |

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
| cap_im_qq         | `components/claw_capabilities/cap_im_qq/` — QQ Bot              | compiled, working                   |
| cap_im_tg         | `components/claw_capabilities/cap_im_tg/` — Telegram Bot        | compiled, working                   |
| cap_im_feishu     | `components/claw_capabilities/cap_im_feishu/` — Feishu Bot      | compiled, working                   |
| cap_im_wechat     | `components/claw_capabilities/cap_im_wechat/` — WeChat Bot      | compiled, working                   |
| cap_files         | `components/claw_capabilities/cap_files/` — file ops             | compiled, working                   |
| cap_session_mgr   | `components/claw_capabilities/cap_session_mgr/` — sessions       | compiled, working                   |
| cap_skill_mgr     | `components/claw_capabilities/cap_skill_mgr/` — skill activation | compiled, working                   |
| cap_router_mgr    | `components/claw_capabilities/cap_router_mgr/` — router rules    | compiled, working                   |
| cap_scheduler     | `components/claw_capabilities/cap_scheduler/` — cron             | compiled, working                   |
| cap_system        | `components/claw_capabilities/cap_system/` — device info         | compiled, working (real host data)  |
| cap_time          | `components/claw_capabilities/cap_time/` — SNTP time             | compiled, working                   |
| cap_web_search    | `components/claw_capabilities/cap_web_search/` — search          | compiled, working                   |
| cap_llm_inspect   | `components/claw_capabilities/cap_llm_inspect/` — image analysis | compiled, working                   |
| cap_cli           | `components/claw_capabilities/cap_cli/` — run CLI commands       | compiled, working (8 allowed cmds)  |
| cap_mcp_client    | `components/claw_capabilities/cap_mcp_client/` — MCP client    | compiled, working (3 tools)         |
| cap_mcp_server    | `components/claw_capabilities/cap_mcp_server/` — MCP server    | compiled, working (lifecycle)       |
| app_claw + CLI    | `components/common/app_claw/` — app glue + CLI REPL              | compiled, working                   |

### Our sim_hal layer

```
sim_hal/
├── include/
│   ├── argtable3/            # Minimal argtable3 stub header
│   │   └── argtable3.h
│   ├── esp/                  # ESP-IDF stub headers
│   │   ├── esp_err.h         # Error codes + ESP_ERROR_CHECK
│   │   ├── esp_log.h         # printf-based logging
│   │   ├── esp_console.h     # Console API + esp_console_split_argv
│   │   ├── esp_heap_caps.h   # → /proc/meminfo (real host memory)
│   │   ├── esp_system.h      # → /proc/meminfo, /proc/cpuinfo, sysconf
│   │   ├── esp_chip_info.h   # → /proc/cpuinfo (real CPU model + core count)
│   │   ├── esp_random.h      # → rand()
│   │   ├── esp_timer.h       # → clock_gettime(CLOCK_MONOTONIC)
│   │   ├── esp_check.h       # ESP_RETURN_ON_ERROR etc macros
│   │   ├── esp_event.h       # event loop stub
│   │   ├── esp_netif.h       # → getifaddrs() (real host IP/netmask/gateway)
│   │   ├── esp_netif_sntp.h  # SNTP no-op stubs
│   │   ├── esp_crt_bundle.h  # TLS cert no-op
│   │   ├── esp_http_client.h # HTTP client stubs
│   │   ├── esp_wifi.h        # → /proc/net/wireless (real WiFi detection)
│   │   ├── nvs_flash.h       # NVS → JSON file
│   │   ├── nvs.h             # NVS key-value API
│   │   ├── gpio_stub.h       # GPIO no-ops
│   │   ├── i2c_stub.h        # I2C no-ops
│   │   ├── spi_stub.h        # SPI no-ops
│   │   ├── esp_http_server.h # HTTP server stub
│   │   ├── esp_mcp_engine.h  # MCP SDK stub
│   │   ├── esp_mcp_mgr.h     # MCP manager stub
│   │   ├── esp_mcp_property.h# MCP property stub
│   │   ├── esp_mcp_tool.h    # MCP tool stub
│   │   ├── esp_mcp_data.h    # MCP data stub
│   │   ├── esp_mcp_completion.h# MCP completion stub
│   │   ├── esp_mcp_prompt.h  # MCP prompt stub
│   │   ├── esp_mcp_resource.h# MCP resource stub
│   │   ├── mdns.h            # mDNS no-op stub
│   │   └── adc_stub.h        # ADC no-ops
│   ├── lwip/                 # lwIP stub headers
│   │   └── ip_addr.h         # IP address stub
│   └── freertos/             # FreeRTOS API stub headers
│       ├── FreeRTOS.h        # Types + all API declarations + runtime stats
│       ├── task.h → FreeRTOS.h
│       ├── queue.h → FreeRTOS.h
│       ├── semphr.h → FreeRTOS.h
│       ├── projdefs.h        # pdPASS/pdFAIL
│       └── portmacro.h       # portTICK_PERIOD_MS, dynamic portNUM_PROCESSORS
├── freertos_shim.c            # FreeRTOS → pthread + CPU runtime stats (/proc/stat)
├── console_unix.c             # esp_console → Unix socket + esp_console_split_argv
├── nvs_stub.c                 # NVS flash init stub
├── nvs.c                      # Full NVS key-value store (cJSON)
├── http_curl.c                # LLM HTTP transport (libcurl)
├── esp_http_client.c          # HTTP client (libcurl-backed)
├── esp_websocket_client.c     # WebSocket client (libcurl-backed)
├── display_sdl2.c             # Display adapter (SDL2)
├── camera_stub.c              # Test pattern generator
├── base64.c                   # Minimal base64 encode/decode
├── cJSON.c + cJSON.h          # cJSON v1.7.18
├── aes_stub.c                 # AES no-op stub
├── emote_stub.c               # Emote engine stub
├── esp_mcp_stubs.c            # MCP SDK no-op implementations
└── lv_font_stub.c             # LVGL font stub
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
| uxTaskGetNumberOfTasks             | /proc/self/task directory count + 1 (IDLE)     |
| uxTaskGetSystemState               | /proc/self/task/…/stat + /proc/stat CPU idle   |
| xEventGroup*                       | pthread_mutex_t + pthread_cond_t + uint32_t    |

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
mcp_client --discover|--list-tools|--call-tool  (argtable3 parsed)
mcp_server --status|--enable|--disable|--set-config  (argtable3 parsed)
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
| `SIMULATOR_BUILD`                    | enabled |
| `CONFIG_APP_CLAW_CAP_LUA=1`          | enabled |
| `CONFIG_APP_CLAW_CAP_IM_LOCAL=1`     | enabled |
| `CONFIG_APP_CLAW_CAP_IM_QQ=1`        | enabled |
| `CONFIG_APP_CLAW_CAP_IM_TG=1`        | enabled |
| `CONFIG_APP_CLAW_CAP_IM_FEISHU=1`    | enabled |
| `CONFIG_APP_CLAW_CAP_IM_WECHAT=1`    | enabled |
| `CONFIG_APP_CLAW_CAP_SESSION_MGR=1`  | enabled |
| `CONFIG_APP_CLAW_CAP_FILES=1`        | enabled |
| `CONFIG_APP_CLAW_CAP_SKILL_MGR=1`    | enabled |
| `CONFIG_APP_CLAW_CAP_ROUTER_MGR=1`   | enabled |
| `CONFIG_APP_CLAW_CAP_LLM_INSPECT=1`  | enabled |
| `CONFIG_APP_CLAW_CAP_WEB_SEARCH=1`   | enabled |
| `CONFIG_APP_CLAW_CAP_TIME=1`         | enabled |
| `CONFIG_APP_CLAW_CAP_SCHEDULER=1`    | enabled |
| `CONFIG_APP_CLAW_CAP_SYSTEM=1`       | enabled |
| `CONFIG_APP_CLAW_CAP_CLI=1`          | enabled |
| `CONFIG_APP_CLAW_CAP_MCP_CLIENT=1`   | enabled |
| `CONFIG_APP_CLAW_CAP_MCP_SERVER=1`   | enabled |
| `CONFIG_APP_CLAW_MEMORY_MODE_FULL=1` | enabled |
| `CONFIG_APP_CLAW_ENABLE_CLI=1`       | enabled |
| `CONFIG_APP_CLAW_ENABLE_EMOTE=1`     | enabled |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT=1`  | enabled |
| `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=1` | enabled |
| `CONFIG_FREERTOS_USE_TRACE_FACILITY=1` | enabled |
| `CONFIG_APP_CLAW_LUA_MODULE_DISPLAY=1` | enabled |
| `CONFIG_APP_CLAW_LUA_MODULE_BOARD_MANAGER=1` | enabled |

## Verified at runtime

All 19 capability groups register (~72 capabilities), CLI REPL serves 14 commands over Unix socket:

```
[I] [app_capabilities] Register QQ cap ok (groups=1, caps=4)
[I] [app_capabilities] Register Feishu cap ok (groups=2, caps=8)
[I] [app_capabilities] Register Telegram cap ok (groups=3, caps=12)
[I] [app_capabilities] Register WeChat cap ok (groups=4, caps=15)
[I] [app_capabilities] Register local / Web IM cap ok (groups=5, caps=17)
[I] [app_capabilities] Register files cap ok (groups=6, caps=23)
[I] [app_capabilities] Register scheduler cap ok (groups=7, caps=34)
[I] [cap_lua_rt] Lua runtime ready
[I] [app_capabilities] Register Lua cap ok (groups=8, caps=42)
[I] [app_capabilities] Register MCP client cap ok (groups=9, caps=45)
[I] [app_capabilities] Register MCP server cap ok (groups=10, caps=46)
[I] [app_capabilities] Register skill cap ok (groups=11, caps=51)
[I] [app_capabilities] Register system cap ok (groups=12, caps=57)
[I] [app_capabilities] Register claw_memory group ok (groups=13, caps=62)
[I] [app_capabilities] Register time cap ok (groups=14, caps=63)
[I] [app_capabilities] Register LLM inspect cap ok (groups=15, caps=64)
[I] [app_capabilities] Register web search cap ok (groups=16, caps=65)
[I] [app_capabilities] Register router manager cap ok (groups=17, caps=71)
[I] [app_capabilities] Register session manager cap ok (groups=18, caps=72)
[I] [cap_mcp_srv] MCP server ready: http://esp-claw.local:18791/mcp_server
[I] [desktop_main] cap_cli registered with 8 allowed commands (group 19)
[I] [console_unix] Unix socket ready at ~/.esp-agent/agent.sock
```

Real host data passthrough (all via `/proc` and POSIX APIs):
- **CPU**: model name from `/proc/cpuinfo`, core count from `sysconf`, usage from `/proc/stat`
- **Memory**: total/free/available from `/proc/meminfo`
- **Network**: IP/netmask/gateway from `getifaddrs()`, WiFi SSID/RSSI from `/proc/net/wireless`
- **Uptime**: `clock_gettime(CLOCK_MONOTONIC)` (microseconds since boot)

### `cap call` JSON syntax

The `cap call <name> <json>` command parses its third argument as JSON. Always pass a JSON object, even for no-arg capabilities:

```bash
# Capabilities without arguments — pass empty object
esp-agent 'cap call get_system_info {}'
esp-agent 'cap call get_ip_address {}'
esp-agent 'cap call get_memory_info {}'
esp-agent 'cap call memory_list {}'

# Capabilities with arguments — pass real JSON
esp-agent 'cap call scheduler_add {"cron":"0 */2 * * *","action":"..."}'
```

Note: wrap the full command in single quotes to prevent shell expansion of `{}` and preserve the JSON string exactly.

## Test Checklist

- [X] FreeRTOS shim (tasks, queues, semaphores, recursive mutex, binary sem, notifications)
- [X] FreeRTOS runtime stats (uxTaskGetSystemState, /proc/stat CPU usage passthrough)
- [X] NVS storage (JSON file read/write)
- [X] Event router (startup event published/processed)
- [X] Memory init (root dir, extract disabled without LLM)
- [X] Skills init (3 skills loaded, auto-created on first run)
- [X] All 19 capability groups registered (~72 capabilities)
- [X] Unix socket CLI (help, ask, cap, auto, lua, event_router, skill, session)
- [X] esp-agent CLI tool (config/start/stop/status/logs/ask/display/build/clean)
- [X] config.json read + env var overrides
- [X] Auto-create ~/.esp-agent/ on first run
- [X] LLM API call (set api_key + model in config.json)
- [X] Real host data: CPU model/cores/usage, memory, IP/netmask/gateway, uptime
- [X] Real WiFi detection (/proc/net/wireless, reports "disconnected" on WSL2)
- [X] cap_cli run_cli_command (8 allowed commands, allowlist enforcement)
- [X] cap_scheduler cron store/trigger (JSON-backed, NVS persistence)
- [ ] cap_im_local serves web UI on localhost
- [ ] cap_lua runs Lua scripts
- [ ] Skills auto-load into LLM context
- [ ] Memory store/recall in LLM session
- [ ] Display toggle (SDL2 window on/off via config.json)
