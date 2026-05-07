---
{
  "name": "cap_screenshot",
  "description": "Capture a JPEG screenshot of the current display (emote or Lua rendering screen) and save to the screenshots directory.",
  "metadata": {
    "cap_groups": [
      "cap_screenshot"
    ],
    "manage_mode": "readonly"
  }
}
---

# Screenshot

Use this capability to capture the current screen display as a JPEG image file.

## When to use
- The user asks to take a screenshot, capture the screen, or save the display.
- You need to share what's currently shown on screen with the user.
- You want to record the current display state for debugging or reference.

## Available capability
- `screenshot`: capture a JPEG screenshot of the current display.

## Calling rules
- Call `screenshot` directly. Input must be a JSON object:

```json
{
  "filename": "optional_name.jpg",
  "quality": 85
}
```

- `filename` (optional): custom output filename. If omitted, a timestamp-based name like `screenshot_20260101_120000.jpg` is used.
- `quality` (optional): JPEG quality 1-100, default 85. Higher values produce larger files with better quality.
- The screenshot captures whatever is currently displayed — both the emote animation screen and Lua script rendering windows are supported.

## Output shape
- On success: `screenshot saved to <full-path> (quality=<n>)`
- On failure: `screenshot failed: <error>`

## Examples

Take a screenshot with default settings:
```json
{}
```

Take a named screenshot at high quality:
```json
{
  "filename": "game_state.jpg",
  "quality": 95
}
```

## Notes
- Files are saved to the `screenshots/` directory under the data directory (`~/.crush-claw/screenshots/`).
- The screenshot captures the raw display content at the current LCD resolution.
- This capability is also available from Lua scripts via `require("capability").call("screenshot", ...)`.
