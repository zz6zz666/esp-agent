# Lua Script Runner

Run Lua scripts for automation, data processing, display rendering, and
hardware interaction.

## Available modules (simulator)

The following Lua modules are built-in and always available:

| Module | Purpose | Skill doc |
|--------|---------|-----------|
| `display` | Draw text, shapes, images on screen | `lua_module_display.md` |
| `timer` | Wall-clock sleep (`timer.sleep_ms(ms)`) | *(inline)* |
| `storage` | Read/write/list/delete files | `lua_module_storage.md` |
| `system` | Device info, reboot, free heap | `lua_module_system.md` |
| `delay` | Microsecond-precision delays | `lua_module_delay.md` |
| `event_publisher` | Publish events to the agent bus | `lua_module_event_publisher.md` |

## Usage

Place Lua scripts in the scripts directory and call `run_lua` with the
script name.  The display window will switch to the script's output and
remain visible until the script calls `display.deinit()`.

## Writing display scripts

Use `require("display")` — do NOT use `lvgl` or `lvgl.xxx`.  The `display`
module provides `draw_text`, `fill_rect`, `draw_circle`, `draw_pixel`,
`draw_line`, `draw_bitmap`, `draw_png_file`, `draw_jpeg_file`, and many more.

### Minimal example

```lua
local display = require("display")
display.clear(5, 3, 15)
display.draw_text(20, 20, "Hello from ESP-Claw!", {r=255, g=255, b=255, font_size=24})
display.present()
-- Keep visible for 10 seconds
timer.sleep_ms(10000)
display.deinit()
```

See `lua_module_display.md` for the full API reference.
