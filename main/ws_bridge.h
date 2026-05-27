// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Wire the WS rx dispatcher + outbound event_bus pushes. Must run
// AFTER ws_server_init() and event_bus_init().
void ws_bridge_install();
