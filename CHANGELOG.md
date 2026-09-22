<!--
SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# Changelog

All notable changes to `zhac-mono-core` are recorded here, starting 2026-09-18; earlier work is
in the git history and the README's "Known divergences". Format follows the other ZHAC repos:
an `## [Unreleased]` section accumulates work, and its contents become the release-tag

### Added

- Boot log prints `int-heap after <step>` per init step (as the wired core does), so an exhausted internal heap is visible before a late task such as the MQTT client fails to start.
- **`diag.tasks` + a Tasks card on the Diag page**: every task with its CPU share over the last
  five seconds, the core it is pinned to, priority and stack headroom, so "core 0 sits at 25 %"
  gets a name. `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y` supplies the core.

- **Home Assistant discovery** (`ha_glue.cpp`): the same bridge the wired build has, with
  the Settings toggle and `ha_discovery` / `ha_prefix` in status. Enabling it also gives this
  build an MQTT receive path: before, nothing set the client's rx callback, so `Mqtt#` rule
  triggers and Lua `on_mqtt` never fired on the single-chip build.

### Changed

- **Rule pushes come from the rule engine, not the transport.** `rule.added` / `rule.updated`
  / `rule.deleted` are built from the `RULE_CHANGED` event, so a rule created, edited, toggled
  or deleted over REST or by a backup restore now updates open Rules pages and the cloud relay;
  before, only WebSocket edits did.

- **Device rename, delete and permit join go through `device_cmd`** (zhac-components). Rename
  now validates names (quotes used to blank the Devices page) and reloads the rule engine's
  name table; delete asks the device to leave and hides it (soft) or wipes everything (hard),
  where before it only dropped the pool entry and never sent a leave request.

### Fixed

- MQTT published nothing after "connected" unless Home Assistant discovery was on: device updates only went through the HA bridge. Every update is now also published on `<root>/devices/<IEEE>/state` as `{"ieee","attrs":{key:value}}`, as the dual-chip S3 does.
- Saving a Lua script from the web UI failed with "Unsupported" (HTTP 405): the SPA and net-core save with `POST /api/scripts/<name>`, this port accepted only `PUT`. POST without `/run` or `/check` now saves.
- `groups_store` brought up to net-core's version: one recursive store mutex (created at boot via `grp_store_init()`), `grp_create()` allocates the id and saves under the lock (two concurrent creators could take the same slot), `group.list` holds the lock across its shared buffer. The port had carried the pre-lock copy.
- `zap_store_flush_now` registered as a shutdown handler (as on the P4); renaming a device now also updates its Home Assistant discovery name (the changed-hook only reloaded rules).
- Rules were never stored: `main.cpp` never called `rule_store_init()` / `rule_store_flush_init()`, so every rule save failed silently behind a "Rule saved" toast. Both calls added before `simple_rules_init()`, plus the shutdown flush. Found on the wired S31, same code here; not yet run on mono hardware.
- **`TaskEventBus` no longer burns a fifth of core 0 while idle**: the pump sleeps until a
  publish instead of polling every 20 ms (shared `event_bus_pump_run`).

- **String attributes showed the previous push's JSON text** (first seen on an Aqara
  WXKG01LM button: `action` read `{"event":"attr.changed",...`). The shadow's string field
  holds up to 48 bytes with no terminator; the WebSocket push and the device state built the
  JSON straight from it, so ArduinoJson read on into the stack. Every site copies into a
  terminated buffer now.

- **MQTT settings survive a reboot and the client starts on its own.** The setters this
  build used did not persist, nothing loaded the settings at boot, and nothing started the
  client when Wi-Fi got an address, so a broker configured in Settings was gone after the next
  reboot. The shared `mqtt_gw_cfg` handler (zhac-components) now loads and arms at boot, the
  client connects on IP, and status reports `mqtt_enabled` / `mqtt_broker` / `mqtt_client_id`.

- **Adding a device works from the web UI.** The Devices page opens the join window over
  WebSocket (`zigbee.permit_join`) and polls `zigbee.permit_join.status`; this build only had
  the REST route, so the Add Device panel never opened the network. Both verbs now exist and
  share the deadline with `POST /api/permit_join`.

- **Every attribute write goes through `device_cmd`** (zhac-components): REST, WebSocket,
  MQTT / Home Assistant commands and collection fan-out now share one implementation, so they
  accept the same value types, answer with the same words, and all mirror the command into the
  shadow (collection fan-out and REST did not before).

- **Storage faults no longer erase the owner's data.** When the NVS partition cannot be
  initialised at boot (no free pages, format version change) the hub used to erase it
  silently: devices, rules, names, passwords gone. It now boots locked and empty instead:
  sign-in forced on with a serial-only token, status `storage_error: true`, and Settings offers
  "Erase storage and restart" (`system.storage_reset`, WebSocket) so the erase happens only
  on the owner's word. Architecture review A3.
  Wi-Fi comes up without storage (AP mode) so the recovery page is reachable.

### Fixed

- **Architecture review quick fixes:** sign-in fails closed on a storage fault (serial token
  only, `503 storage_error`); `GET /api/devices` sends from a copy of the pool; the REST setter
  releases the pool lock before dispatch and accepts decimals.
annotation at `just release`.

## [Unreleased]

### Added

- **Time from the router.** When no time server is named, the hub asks the router for one
  (DHCP option 42) and uses it before `pool.ntp.org`; status reports it as `ntp_dhcp_server`
  and the Settings Time card says so. A hub on a network without internet access then keeps
  its clock across power cuts with no configuration. Rows of `device.list` now carry `name`, `vendor`, `model_id` and `known` like the other builds.

- **Sign-in, on by default.** The REST and WebSocket APIs were open to anyone on the network
  (the tracked divergence from the other builds). The wired build's `auth.cpp` is ported as
  is: API token on every route but status, auth and the web UI; first `auth` message on a
  WebSocket; admin password sign-in with the ten-minute first-claim window after power-on;
  per-address lockout; token rotation signs every other browser out. Status reports
  `auth_enabled`, `auth_setup_required`, `auth_setup_secs_left`. A hub upgraded from an
  earlier build asks for a password on its next visit (within ten minutes of power-on);
  Settings → Auth turns sign-in off for a lab bench.

- **A time server you can choose** (Settings, Time; `settings.set {"ntp_server"}`). A hub
  without internet access can use one on its own network, so its clock returns after a power
  cut without anyone opening the web UI. Empty means the public default, and status reports
  the server as `ntp_server`.
- **`time.set {epoch}` for a hub without internet access.** No board has a battery-backed
  clock, so an offline hub had no time and its schedules never ran. The web UI now hands over
  the browser's clock when status says `clock_set: false`. The hub takes it only while its own
  clock is unset, so a browser never moves a clock that SNTP has set. WebSocket only.

### Fixed

- **Decimal writes.** `device.attr.set` accepts `21.5` (it answered "value must be bool /
  number / string"); the converter scales it and the shadow mirrors it ×100.

- **The hub never set its clock, so a device's "last seen" always showed "—" and cron rules
  ran on a 1970 clock.** The S3 has no RTC and nothing started SNTP. The hub now starts it
  (pool.ntp.org, the same code as net-core's `wifi_mgr.cpp`) on its first Wi-Fi address.
  `last_seen` is written once the time is set, and each attribute event carries the time so
  the web UI updates "last seen" live. On a network without internet access the clock stays
  unset, as before.
- **Status reports `clock_set`** (WebSocket and REST), so the web UI can say when scheduled
  rules are waiting; the shared rule engine now holds them until the clock is set.
- **Decimal readings never reached the web UI.** Temperature, humidity and other readings the
  device library stores as value × 100 (`VAL_FLOAT`) were left out of `device.get`, the REST
  device detail and every `attr.changed` event, so those tiles stayed empty. All three now
  divide by 100 at the JSON boundary, as the other builds do; rules and Lua still see the raw
  × 100 integer.
