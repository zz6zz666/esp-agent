# File Operations

List, read, write, copy, move, and delete files on the device.

> **Note:** These cap_files capabilities (`read_file`, `write_file`, `delete_file`, `copy_file`, `move_file`, `list_dir`) cannot be called from within Lua scripts via `capability.call` — they are blocked by the Lua sandbox. In Lua, use `require("storage")` for file I/O instead.

## Capabilities
- `read_file` — Read contents of a text file (max 32 KB)
- `write_file` — Create or overwrite a text file
- `delete_file` — Delete a file
- `copy_file` — Copy a file to a new location
- `move_file` — Move or rename a file
- `list_dir` — Recursively list files, optionally filtered by prefix

## Usage
Activate this skill when you need to work with files on the device filesystem.
