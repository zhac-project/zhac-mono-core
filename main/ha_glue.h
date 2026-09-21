// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ha_glue — Home Assistant discovery on the single-chip build. The device
// data is local (pool + shadow), so the ha_bridge callbacks read it directly,
// the same way zhac-wired-core's mqtt_glue.cpp does.
#pragma once

// Sets the MQTT rx callback (HA commands first, everything else becomes an
// MQTT_MSG event for rules and Lua), subscribes <root>/#, starts ha_bridge.
// Call after mqtt_gw_start().
void ha_glue_start();
