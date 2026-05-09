---
{
  "name": "cap_emote_text",
  "description": "Change the text displayed on the emote screen and persist the setting to config.json so it survives restarts.",
  "metadata": {
    "cap_groups": [
      "cap_emote_text"
    ],
    "manage_mode": "readonly"
  }
}
---

# Emote Text

Use this capability to change the text shown on the emote animation screen. The change is immediately visible and automatically saved to `config.json`, so the text persists across agent restarts.

## When to use
- The user asks to change the message on the display screen.
- You want to set a custom greeting, status, or label on the emote screen.
- The current text needs to be replaced with something new.

## Available capability
- `emote_set_text`: set the emote screen text.

## Calling rules
- Call `emote_set_text` directly. Input must be a JSON object:

```json
{
  "text": "Your custom text here"
}
```

- `text` (required): the text to display on the emote screen. Max 64 bytes.
- The change is applied immediately (the custom text is pushed to the emote label and display is refreshed).
- The text is automatically saved to `display.emote_text` in `config.json` so it will be used again after a restart.
- Emoji characters (⭐🎉💖 etc.) are fully supported via SDL2_ttf font rendering with Noto Color Emoji / Segoe UI Emoji fonts.

## Output shape
- On success: `emote text updated to: <text>`
- On failure: `missing "text" field` or `invalid JSON`

## Examples

Set a welcome message:
```json
{
  "text": "Welcome back!"
}
```

Clear the custom text (revert to default "Wi-Fi connected"):
```json
{
  "text": ""
}
```

## Notes
- The text appears in the status label area on the emote screen (replaces "Wi-Fi connected").
- An empty string `""` clears the custom text and restores the default "Wi-Fi connected" message.
- Full emoji support via SDL2_ttf (Noto Color Emoji / Segoe UI Emoji). CJK text also supported.
- The text is rendered at 16px centered in a 240x40 area at the top of the emote display.
