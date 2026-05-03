# ESP32-Compatible Components

This directory contains extension components for the esp-claw agent framework
that follow ESP-IDF conventions. Components here **must** compile on both:

1. **Real ESP32 hardware** — using the standard ESP-IDF build system
2. **Linux desktop simulator** — using the sim_hal runtime environment (via CMake)

## Conventions

Each component follows the standard ESP-IDF layout:

```
components/<name>/
├── include/          # Public API headers
├── src/              # Implementation files
├── CMakeLists.txt    # ESP-IDF component build (idf_component_register)
└── README.md         # Component description
```

## Compatibility Rules

| Feature Type                    | ESP32 | Desktop | Required Stub |
| ------------------------------- | ----- | ------- | ------------- |
| Pure logic / algorithms         | yes   | yes     | none          |
| Network (WiFi/TCP)              | yes   | yes     | POSIX sockets |
| File I/O                        | yes   | yes     | POSIX files   |
| GPIO / I2C / SPI / ADC          | yes   | **no**  | N/A           |
| Flash / NVS                     | yes   | yes     | sim_hal/nvs   |
| Bluetooth                       | yes   | **no**  | N/A           |
| Camera / Display                | yes   | partial | sim_hal       |

If a component requires hardware peripherals not available on desktop,
use `#if !defined(SIMULATOR_BUILD)` guards or provide a `Kconfig` option
to disable the hardware-dependent portions.

## Adding a New Component

1. Create the directory: `components/<name>/`
2. Add `include/` and `src/` subdirectories
3. Write a `CMakeLists.txt` using `idf_component_register()`
4. Add the component to the root `CMakeLists.txt` component search path
5. Register capabilities via `claw_cap_register_group()` in the component init
