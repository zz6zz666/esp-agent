# Task: Enable cap_mcp_client and cap_mcp_server

## Context

We are enabling the last 2 remaining capability groups in the esp-claw desktop simulator.
All changes go in `CMakeLists.txt` + `sim_hal/` stubs **only** — never modify code under `esp-claw/`.

Current state: 16/20 capability dirs compiled, 13 groups registered (~54 capabilities).

## Reference material

### MCP SDK headers (provided)
Extracted to: `tmp_mcp_sdk/` (project root)

```
include/
├── esp_mcp_engine.h      # esp_mcp_t, esp_mcp_create, esp_mcp_add_tool, esp_mcp_destroy
├── esp_mcp_mgr.h         # esp_mcp_mgr_handle_t, esp_mcp_mgr_config_t, esp_mcp_transport_http_server,
│                          #   esp_mcp_mgr_init/start/stop/deinit/register_endpoint
├── esp_mcp_property.h    # esp_mcp_property_t, esp_mcp_property_list_t, esp_mcp_value_t,
│                          #   esp_mcp_value_create_string/bool, esp_mcp_property_create_with_string,
│                          #   esp_mcp_property_list_get_property_string
├── esp_mcp_tool.h        # esp_mcp_tool_t, esp_mcp_tool_callback_t, esp_mcp_tool_create,
│                          #   esp_mcp_tool_add_property
├── esp_mcp_data.h        # (included by esp_mcp_tool.h)
├── esp_mcp_completion.h  # (included by esp_mcp_engine.h)
├── esp_mcp_prompt.h      # (included by esp_mcp_engine.h)
└── esp_mcp_resource.h    # (included by esp_mcp_engine.h)
```

### ESP-IDF headers NOT provided (need to infer from usage or ESP-IDF docs)

These are from ESP-IDF itself. Create stubs based on how cap_mcp_* source files call them:

**mdns.h** — needed by both cap_mcp_client AND cap_mcp_server:
- `mdns_init()` → esp_err_t
- `mdns_hostname_set(const char *)` → esp_err_t
- `mdns_instance_name_set(const char *)` → esp_err_t
- `mdns_query_ptr(const char *service, const char *proto, uint32_t timeout, int max_results, mdns_result_t **results)` → esp_err_t
- `mdns_query_results_free(mdns_result_t *)` → void
- `mdns_service_add(const char *instance, const char *service, const char *proto, uint16_t port, mdns_txt_item_t *txt, size_t txt_count)` → esp_err_t
- `mdns_service_port_set(const char *service, const char *proto, uint16_t port)` → esp_err_t
- `mdns_service_txt_set(const char *service, const char *proto, mdns_txt_item_t *txt, size_t txt_count)` → esp_err_t
- `mdns_service_remove(const char *service, const char *proto)` → esp_err_t
- Type: `mdns_result_t` — struct with fields: `next` (pointer to next), `addr` (pointer to mdns_ip_addr_t), `txt` (array of mdns_txt_item_t), `txt_count` (size_t)
- Type: `mdns_ip_addr_t` — struct with fields: `next` (pointer to next), `addr` (ip_addr_t, by value)
- Type: `mdns_txt_item_t` — struct with fields: `key` (const char*), `value` (const char*)

**lwip/ip_addr.h** — needed by cap_mcp_client discover core:
- `ip_addr_t` — opaque type (used with ipaddr_ntoa_r)
- `ipaddr_ntoa_r(const ip_addr_t *, char *buf, size_t len)` → char*

**esp_http_server.h** — needed by cap_mcp_server:
- `httpd_config_t` — struct with fields: `server_port` (uint16_t), `ctrl_port` (uint16_t), `max_uri_handlers` (uint16_t), `stack_size` (uint32_t)
- `HTTPD_DEFAULT_CONFIG()` — convenience macro initializing httpd_config_t

## Implementation plan

### Step 1: cap_mcp_client (medium — ~3 new stubs)

Create these new stub headers:

1. **`sim_hal/include/lwip/ip_addr.h`**
   - `ip_addr_t` typedef (opaque struct)
   - `ipaddr_ntoa_r()` declaration (no-op returning NULL)

2. **`sim_hal/include/esp/mdns.h`** (shared with cap_mcp_server)
   - All mdns_* function declarations listed above
   - `mdns_result_t`, `mdns_ip_addr_t`, `mdns_txt_item_t` types
   - All return ESP_OK/ESP_ERR_NOT_FOUND (no real mDNS on desktop)

Add to CMakeLists.txt:
- `CONFIG_APP_CLAW_CAP_MCP_CLIENT=1` to compile definitions
- `cap_mcp_client.c`, `cap_mcp_discover_core.c`, `cmd_cap_mcp_client.c` to ESPCLAW_SRCS
- Add `cap_mcp_client/include` to COMMON_INCLUDES
- Add `cap_mcp_server/include` to COMMON_INCLUDES (cap_mcp_client includes cap_mcp_server.h)
- Add `lwip` include path to COMMON_INCLUDES

Check `app_capabilities.c` and `app_claw.c` for `#if CONFIG_APP_CLAW_CAP_MCP_CLIENT` guards:
- If init calls exist, they auto-activate
- If not, add manual init in `main_desktop.c` (like cap_cli)

### Step 2: cap_mcp_server (hard — ~6 new stubs)

Create these new stub headers (under `sim_hal/include/esp/`):

1. **`esp_mcp_engine.h`** — stub with all types/functions from MCP SDK header
   - `esp_mcp_t` opaque typedef (uint32_t handle)
   - `esp_mcp_create()`, `esp_mcp_add_tool()`, `esp_mcp_destroy()`
   - Include guards that reference the real types

2. **`esp_mcp_tool.h`** — stub
   - `esp_mcp_tool_t` opaque typedef
   - `esp_mcp_tool_callback_t` function pointer typedef
   - `esp_mcp_tool_create()`, `esp_mcp_tool_add_property()`

3. **`esp_mcp_property.h`** — stub
   - `esp_mcp_property_t`, `esp_mcp_property_list_t`, `esp_mcp_value_t` opaque typedefs
   - Create/getter functions

4. **`esp_mcp_data.h`** — stub (included by esp_mcp_tool.h, may be empty)

5. **`esp_mcp_mgr.h`** — stub
   - `esp_mcp_mgr_handle_t` (uint32_t)
   - `esp_mcp_mgr_config_t` struct
   - `esp_mcp_transport_http_server` enum value
   - All mgr functions

6. **`esp_mcp_completion.h`** — stub (included by esp_mcp_engine.h)
7. **`esp_mcp_prompt.h`** — stub (included by esp_mcp_engine.h)
8. **`esp_mcp_resource.h`** — stub (included by esp_mcp_engine.h)

**`esp_http_server.h`** — stub:
   - `httpd_config_t` struct
   - `HTTPD_DEFAULT_CONFIG()` macro

Add to CMakeLists.txt:
- `CONFIG_APP_CLAW_CAP_MCP_SERVER=1`
- `cap_mcp_server.c`, `cmd_cap_mcp_server.c` to ESPCLAW_SRCS
- Include dirs added

Check `app_capabilities.c` / `app_claw.c` for init guards.

### Key insight for stub design

All stub functions should return `ESP_OK` (success) by default. The MCP server init/start flow needs to succeed for the capability group to register. No-ops are fine — we don't need a real MCP server on desktop, we just need compilation and capability registration to succeed.

For mDNS: `mdns_query_ptr()` should return `ESP_ERR_NOT_FOUND` (no services discovered — desktop doesn't have real mDNS). This is gracefully handled by cap_mcp_client.

## Files to create/modify

| File | Action |
|------|--------|
| `CMakeLists.txt` | Add 2 CONFIG defines + source files + include paths |
| `main_desktop.c` | Add manual init if not in app_capabilities.c |
| `sim_hal/include/lwip/ip_addr.h` | **CREATE** — lwIP IP address stub |
| `sim_hal/include/esp/mdns.h` | **CREATE** — mDNS stub (~11 functions, 3 types) |
| `sim_hal/include/esp/esp_mcp_engine.h` | **CREATE** — MCP engine stub |
| `sim_hal/include/esp/esp_mcp_mgr.h` | **CREATE** — MCP manager stub |
| `sim_hal/include/esp/esp_mcp_property.h` | **CREATE** — MCP property stub |
| `sim_hal/include/esp/esp_mcp_tool.h` | **CREATE** — MCP tool stub |
| `sim_hal/include/esp/esp_mcp_data.h` | **CREATE** — MCP data stub |
| `sim_hal/include/esp/esp_mcp_completion.h` | **CREATE** — MCP completion stub |
| `sim_hal/include/esp/esp_mcp_prompt.h` | **CREATE** — MCP prompt stub |
| `sim_hal/include/esp/esp_mcp_resource.h` | **CREATE** — MCP resource stub |
| `sim_hal/include/esp/esp_http_server.h` | **CREATE** — HTTP server stub |

## Verification

After each step:
1. `./crush-claw build` — compile cleanly
2. `./crush-claw start && ./crush-claw 'cap call get_system_info {}'` — check logs for new groups
3. `./crush-claw cap list` — verify new caps appear

Expected final: **+2 groups, +6 caps** (cap_mcp_client: 3 tools, cap_mcp_server: 3 tools).
After both: **15 groups, ~60 capabilities**.

---

Reference: MCP SDK headers at `tmp_mcp_sdk/include/`.
