# Desktop-Only Components

This directory contains components that are **exclusively** available in the
Linux desktop simulator environment. These components provide tools and
capabilities that leverage the host OS and are not intended for ESP32 hardware.

## Purpose

The desktop simulator provides a full Linux environment. Desktop components
exploit this to offer capabilities that would be impractical or impossible
on a microcontroller:

- **Host command execution** — run Linux binaries and capture output
- **Filesystem access** — read/write arbitrary host files (sandboxed)
- **Process management** — spawn, monitor, and signal host processes
- **Desktop notifications** — integrate with host notification system
- **System monitoring** — CPU, memory, disk, network statistics
- **Browser integration** — open URLs, interact with host browser

## Conventions

```
components/desktop/
├── include/
│   └── component_desktop.h   # Desktop component namespace declaration
├── src/                       # Implementation files (future)
├── CMakeLists.txt             # Desktop component build
└── README.md                  # This file
```

## Safety Note

Desktop components run with the same privileges as the agent process.
All host command execution and filesystem access must be **explicitly
authorized** by the LLM capability system (via `CLAW_CAP_FLAG_RESTRICTED`).

## Relationship with sim_hal

- `sim_hal/` — provides the **runtime environment** (ESP-IDF API stubs, FreeRTOS shim)
- `components/desktop/` — provides **extension functionality** (tools/capabilities) that
  uses the runtime environment

Desktop components depend on `sim_hal` but `sim_hal` does not depend on them.
