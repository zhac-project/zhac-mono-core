<!--
SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# zhac-mono-core

Single-chip **ESP32-S3** variant of the ZHAC Zigbee Home Automation Controller.

Where the shipping product splits work across two chips — an ESP32-P4 main core
(Zigbee coordinator + Lua engine) and an ESP32-S3 net core (WiFi / REST /
WebSocket / MQTT gateway + SPA) talking over an SPI/HAP link — this variant runs
**both roles on one ESP32-S3** and replaces the SPI/HAP transport with direct
in-process calls through a compile-time bridge (`components/mono_bridge/`).

> **Status: experimental / work in progress.** This is a parity port that
> tracks the dual-chip firmware. Its `main/` gateway layer is currently a fork
> of `zhac-net-core/main/`, so dual-chip fixes do **not** propagate here
> automatically — see *Divergence* below. It is not part of the `zhac-platform`
> meta-repo build and is not a supported release target yet.

## Architecture

- **Shared logic, not copied.** Device definitions (`embedded-zhc`), shared
  ESP-IDF components (`zhac-components`), and the SPA (`www-spa`) are consumed
  from their own repos via `EXTRA_COMPONENT_DIRS` — the same sources the
  dual-chip cores use.
- **Bridge shims.** `components/mono_bridge/` provides `hap_master`/`hap_slave`
  stand-ins so the gateway and engine layers link and call each other directly
  instead of framing HAP over SPI.
- **Lua on S3.** `components/lua_cjson/` is a CMake override that re-registers
  `zhac-main-core`'s vendored (MIT) lua_cjson unconditionally, because the
  sibling gates it behind `IDF_TARGET == esp32p4`.

## Building

Requires ESP-IDF v6.0 and the sibling ZHAC repos checked out next to this one:

```
zhac-workspace/
├── embedded-zhc/
├── zhac-components/
├── zhac-main-core/
├── zhac-net-core/
├── www-spa/
└── zhac-mono-core/   ← you are here
```

```sh
source <path-to-esp-idf-v6.0>/export.sh
export IDF_COMPONENT_OVERRIDE_PATH="$PWD/../zhac-components/components"
export EMBEDDED_ZHC_PATH="$PWD/../embedded-zhc"
( cd ../www-spa && npm ci && npm run build )   # produces dist/ embedded into SPIFFS
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The build resolves shared components through three fallback paths
(`$IDF_COMPONENT_OVERRIDE_PATH` → `../zhac-components/components` →
`../../components`); the sibling checkouts above are the required inputs.

## Divergence

`main/` is a hard fork of the net-core gateway (`ws_bridge.cpp`, `main.cpp`,
`api_system.cpp`, …). A change made to the dual-chip gateway must be re-ported
here by hand until that layer is extracted into a shared component. Treat the
dual-chip repos as the source of truth for gateway behaviour and mirror changes
deliberately.

## License

AGPL-3.0-or-later. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). "ZHAC" and
associated marks are not licensed for reuse — forks must rebrand.
