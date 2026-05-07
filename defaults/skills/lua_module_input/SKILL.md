---
{  "name": "lua_module_input",
  "description": "Keyboard and mouse input for interactive Lua scripts.",
  "metadata": {    "cap_groups": ["cap_lua"],
    "manage_mode": "readonly"
  }
}
---
# Lua Input (Desktop Simulator)

Keyboard and mouse input for interactive Lua scripts 鈥?the desktop
equivalent of `touch` / `button` / `knob` on ESP32 hardware.

Import with `local input = require("input")`.

## Polling (non-blocking)

### `input.mouse_pos()`
Returns `x, y` 鈥?current mouse cursor position in logical display coords.

### `input.mouse_down([button])`
Returns `true` if the button is currently pressed.
- `button`: 1=left (default), 2=middle, 3=right

### `input.mouse_wheel()`
Returns accumulated scroll wheel ticks.

### `input.key_down(name_or_scancode)`
Returns `true` if the key is currently held.
- String form: `input.key_down("a")`, `input.key_down("Space")`
- Scancode form: `input.key_down(4)` (= SDL_SCANCODE_A)

### `input.modifiers()`
Returns an integer bitmask of active modifiers (Shift, Ctrl, Alt, etc.).

### `input.key_name(scancode)`
Returns the human-readable name for a scancode (e.g. 4 鈫?`"A"`, 40 鈫?`"Return"`).

## Event-based (blocking)

### `input.wait_event([timeout_ms])`
Blocks until an input event occurs or timeout expires. Returns a table:

```lua
{ type = "key_down",    key = "A", scancode = 4, mod = 0 }
{ type = "key_up",      key = "A", scancode = 4, mod = 0 }
{ type = "mouse_down",  x = 100, y = 200, button = 1 }
{ type = "mouse_up",    x = 100, y = 200, button = 1 }
{ type = "mouse_wheel", x = 100, y = 200, dy = 1 }
{ type = "text",        text = "浣犲ソ" }
```

Returns `nil` on timeout. Omit timeout_ms (-1) to wait forever.

### `input.poll_events()`
Returns an iterator that drains all queued events without blocking:

```lua
for evt in input.poll_events() do
    if evt.type == "key_down" then
        print("pressed: " .. evt.key)
    end
end
```

## Example: Interactive drawing

```lua
local display = require("display")
local input   = require("input")

display.clear(5, 3, 15)
display.present()

while true do
    local mx, my = input.mouse_pos()
    if input.mouse_down(1) then
        display.draw_pixel(mx, my, 255, 255, 0)
        display.present()
    end
    if input.key_down("Escape") then break end
    delay.delay_ms(16)  -- ~60 fps
end
display.deinit()
```
