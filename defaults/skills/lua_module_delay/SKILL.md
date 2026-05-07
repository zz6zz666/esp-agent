---
{  "name": "lua_module_delay",
  "description": "Wall-clock sleep (delay_ms/delay_us) and frame-pacing (frame_sync).",
  "metadata": {    "cap_groups": ["cap_lua"],
    "manage_mode": "readonly"
  }
}
---
# Lua Delay

This skill describes how to correctly use delay and frame pacing when writing
Lua scripts.

## How to call
- Import it with `local delay = require("delay")`
- Call `delay.delay_ms(ms)` to sleep for a number of milliseconds
- Call `delay.delay_us(us)` for short microsecond delays
- Call `delay.frame_sync(target_ms)` for frame-rate pacing (render loops)
- **`ms`, `us`, `target_ms` must be integers**
- Negative values are accepted but clamped to `0`
- `delay_us(us)` is a busy-wait intended for short hardware timing only
- `delay_us(us)` accepts `0..1000000`; use `delay_ms(ms)` for longer waits

## frame_sync — frame-rate pacing (render loops)

`delay.frame_sync(target_ms)` guarantees that **exactly** `target_ms`
milliseconds elapse between consecutive calls, regardless of how long the
rendering code between calls takes.

### How it works
- **First call** records the reference time and returns immediately.
- **Every subsequent call** computes the time elapsed since the previous
  `frame_sync`, then sleeps only for the **remaining** time needed to hit
  the `target_ms` gap.

### When to use
- Any loop that repeatedly calls `display` module drawing functions.
- Select `frame_sync` when you want a fixed frame rate for animation.

### When NOT to use
- Pure wait/idle without rendering between calls — use `delay_ms` instead.
- Communicating with external hardware (polling, debounce) — use
  `delay_ms` or `delay_us`.

### Example

```lua
local delay = require("delay")
delay.delay_ms(500)
delay.delay_us(200)
```

### Render loop with frame_sync (recommended)

```lua
local delay   = require("delay")
local display = require("display")

local count = 15
while count > 0 do
    display.clear(0x0000)
    display.print(40, 100, "Countdown: " .. count)
    display.print(16, 200, "Press ESC to exit")
    delay.frame_sync(50)   -- ensures exactly 50ms per frame
    count = count - 1
end
```
