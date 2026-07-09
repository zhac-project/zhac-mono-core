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
> automatically — see *Divergence* below. It is published for transparency and
> development, but is **not a supported release target yet** — not intended for
> production flashing.

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
# clone the sibling repos next to this one (the build resolves them by path)
for r in embedded-zhc zhac-components zhac-main-core zhac-net-core www-spa; do
  git clone https://github.com/zhac-project/$r.git ../$r
done

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

A **port-drift alarm** keeps this honest: `tools/check-port-drift.sh` (run in CI
by `.github/workflows/port-drift.yml`, including a weekly schedule) fails when a
tracked net-core gateway source has changed since the last re-port. When it
fires, follow the re-port checklist in [`tools/PORTING.md`](tools/PORTING.md) and
re-baseline. This is the interim strategy; a full extraction into a shared
gateway component is only warranted if mono-core becomes a supported release
target.

Note that the device/rule logic itself is **not** forked: mono-core consumes the
same `zhac-components` (`simple_rules`, `zap_store`, `hap_*`, …) and `embedded-zhc`
as the dual-chip cores via `EXTRA_COMPONENT_DIRS`, so shared-component work (e.g.
the `%value%` rule-action expressions) reaches mono-core automatically. Only the
`main/` gateway layer above is hand-ported.

### Known divergences

Some net-core changes are deliberately held back rather than mirrored, and are
tracked here:

- **REST auth (deferred).** net-core is *secure-by-default*: every REST handler is
  gated by `check_auth()` (`s3_internal.h`) alongside the WebSocket handshake, a
  per-IP failure lockout, and a `token_is_hex32` bootstrap seed (net-core
  `3e06adb`). mono-core's REST API is **currently unauthenticated** — its only
  token path is the WebSocket `token.rotate` command in `ws_bridge.cpp`
  (`sys_state.cpp`'s `ws_server_set_api_token` calls are vestigial). Porting the
  gate is a multi-file security change, deferred while mono-core remains an
  experimental parity branch. It **must** be added before mono-core is exposed on
  an untrusted network. Tracked 2026-07-09.

## License

AGPL-3.0-or-later. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). "ZHAC" and
associated marks are not licensed for reuse — forks must rebrand.
