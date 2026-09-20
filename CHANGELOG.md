<!--
SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# Changelog

All notable changes to `zhac-mono-core` are recorded here, starting 2026-09-18; earlier work is
in the git history and the README's "Known divergences". Format follows the other ZHAC repos:
an `## [Unreleased]` section accumulates work, and its contents become the release-tag
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
