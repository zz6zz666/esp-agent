---
{  "name": "weather",
  "description": "Weather and forecast queries using web search and time context.",
  "metadata": {    "cap_groups": ["cap_time","cap_web_search"],
    "manage_mode": "readonly"
  }
}
---
# Weather 澶╂皵

Get current weather and forecasts through direct capabilities. Support weather, forecast, temperature, 澶╂皵, 棰勬姤, 娓╁害 queries.

## When to use
When the user asks about weather, temperature, forecasts, 澶╂皵, 娓╁害, or 棰勬姤.

## How to use
1. Call `get_current_time` with `{}`
2. Call `web_search` with `{"query":"weather in [city] today"}`
3. Extract temperature, conditions, and forecast from results
4. Present in a concise, friendly format

## Example
User: "What's the weather in Tokyo?"
-> `get_current_time` with `{}`
-> `web_search` with `{"query":"weather Tokyo today February 2026"}`
-> "Tokyo: 8C, partly cloudy. High 12C, low 4C. Light wind from the north."
