---
{  "name": "lua_module_capability",
  "description": "Call agent capabilities from Lua scripts.",
  "metadata": {    "cap_groups": ["cap_lua"],
    "manage_mode": "readonly"
  }
}
---
# Lua Capability

This skill describes how to call registered capabilities directly from Lua.

> **Critical:** `capability.call` from Lua is restricted to a fixed allowlist of 17 capabilities. Calling any other capability (including `read_file`, `write_file`, `delete_file`, all IM sends, `memory_store`, `run_cli_command`, etc.) will error with `"cap '<name>' not allowed from Lua"`. For file operations in Lua, use `require("storage")` instead. For everything else, call the capabilities directly from outside Lua (agent tools).

## Allowed Capabilities
Only these 17 capability IDs can be called from Lua:

| Capability | Purpose |
|---|---|
| `get_system_info` | System information |
| `get_ip_address` | Network IP address |
| `get_memory_info` | Memory statistics |
| `get_cpu_info` | CPU information |
| `get_wifi_info` | WiFi status |
| `memory_list` | List memory entries |
| `memory_get` | Get memory entry |
| `memory_search` | Search memory |
| `memory_count` | Count memory entries |
| `memory_stats` | Memory statistics |
| `get_time_info` | Current time |
| `lua_list_scripts` | List Lua scripts |
| `lua_run_script` | Run script synchronously |
| `lua_list_async_jobs` | List async jobs |
| `lua_get_async_job` | Get async job details |
| `screenshot` | Capture JPEG screenshot |

## How to call
- Import it with `local capability = require("capability")`
- Main API: `ok, out, err = capability.call(name, payload[, opts])`. ALWAYS follow the 3-element pattern, `ok, out, err`, to receive the result, for example `local ok, xxx_out, err = capability.call("xxx", ...)`.
- `name` must match one of the allowed capability IDs listed above, for example `get_system_info` or `get_time_info`. IM capabilities (`qq_send_message`, `tg_send_message`, etc.) are blocked from Lua and will error.

## API

### `capability.call(name, payload[, opts])`
- Inputs:
  - `name`: required `string`, capability name or id
  - `payload`: optional `nil | table | string`
  - `opts`: optional `table`
- Output:
  - success: `true, output_string, nil`
  - failure: `false, output_string|nil, error_string`

## Payload rules
- `nil` becomes `{}`.
- A Lua `table` is serialized to compact JSON.
- A `string` must already be valid JSON and is passed through unchanged.
- The payload keys must match the target capability schema exactly. For example, `memory_get` expects `key`, not `query`.

## Supported `opts`
- `session_id: string`
- `channel: string`
- `chat_id: string`
- `source_cap: string`

If an `opts` field is missing, the module tries to inherit the same field from global `args`.

## IM capability mapping (BLOCKED from Lua sandbox)

> All IM capabilities below are blocked by the Lua sandbox allowlist. Calling them from Lua will error. Use agent-side IM tools directly instead.

- QQ text/image/file:
  - `qq_send_message` with required `chat_id` and `message`
  - `qq_send_image` with required `chat_id`, `path`, and optional `caption`
  - `qq_send_file` with required `chat_id`, `path`, and optional `caption`
- Telegram text/image/file:
  - `tg_send_message` with required `chat_id` and `message`
  - `tg_send_image` with required `chat_id`, `path`, and optional `caption`
  - `tg_send_file` with required `chat_id`, `path`, and optional `caption`
- WeChat text/image:
  - `wechat_send_message` with `message`
  - `wechat_send_image` with `path` and optional `caption`
- Always pass `chat_id` explicitly for `qq`, `tg`, and `wechat` calls.
- Prefer putting `chat_id` in `opts` for `qq` and `tg`, and in `payload` for `wechat`.

## If you want to call other generic capabilities
- Use activate_skill to activate the needed capability and learn what is the schema of the input arguments; what is the schema of the return values.
- Then choose the proper input arguments and call the capability with `capability.call`, and use the correct way to parse the results.

## Examples

> All examples below use capabilities that ARE allowed from Lua. The IM send examples previously shown here are blocked by the sandbox and will error at runtime.

### Get system info
```lua
local capability = require("capability")

local ok, out, err = capability.call("get_system_info", {})
print(ok, out, err)
```

### Get time
```lua
local capability = require("capability")

local ok, out, err = capability.call("get_time_info", {})
print(ok, out, err)
```

### Run another Lua script
```lua
local capability = require("capability")

local ok, out, err = capability.call("lua_run_script", {
  path = "builtin/test/basic_system_info.lua"
})
print(ok, out, err)
```

### Get memory info
```lua
local capability = require("capability")

local ok, out, err = capability.call("get_memory_info", {})
print(ok, out, err)
```

### Memory operations
```lua
local capability = require("capability")

local ok1, out1, err1 = capability.call("memory_list", {})
local ok2, out2, err2 = capability.call("memory_get", { key = "user_profile" })
local ok3, out3, err3 = capability.call("memory_search", { query = "preferences" })
print(ok1, ok2, ok3)
```
