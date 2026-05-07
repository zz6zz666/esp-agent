# Lua Script Runner

Run Lua scripts for automation, data processing, and hardware interaction.

## Sandbox Restrictions

The Lua runtime runs in a hardened sandbox. These operations are **disabled** and will error:

- **`io.*`**, **`os.*`** — entire libraries erased. Use `require("storage")` for file I/O.
- **`dofile`**, **`loadfile`**, **`load`** — nil'd. Cannot load external code.
- **`debug.*`** — only `debug.traceback` survives.
- **`string.dump`** — bytecode generation disabled.
- **C `require()`** — C searchers removed. Only built-in Lua modules available.
- **`capability.call`** — restricted to 16 read-only capabilities. File I/O capabilities (`read_file`, `write_file`, etc.) and IM sends are blocked from Lua.

For file I/O in Lua: use `require("storage")` with `storage.read_file`, `storage.write_file`, etc. All paths stay within the sandbox.

For file I/O from outside Lua: use `read_file`, `write_file`, `delete_file`, `copy_file`, `move_file`, `list_dir` capabilities directly.

Memory budget: **10 MB per Lua state**.

## Capabilities
- Execute Lua scripts with `run_lua` capability
- Scripts can interact with device hardware and sensors
- Async job support for long-running scripts

## Usage
Place Lua scripts in the scripts directory and call `run_lua` with the script name.
