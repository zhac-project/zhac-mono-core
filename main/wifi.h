// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>

// Bring up WiFi based on NVS-saved STA credentials.
//   - Credentials present → start in APSTA mode (AP stays up as a
//     captive fallback while STA tries to connect).
//   - No credentials       → AP-only.
void wifi_start();

// Persist STA credentials in NVS namespace "wifi". Empty SSID clears.
// Caller is responsible for restarting WiFi (or rebooting) to apply.
bool wifi_save_sta(const char* ssid, const char* password);

// Read currently-saved STA SSID. Returns false if not set.
bool wifi_load_sta_ssid(char* out_ssid, size_t cap);

// Snapshot of current WiFi state for /api/wifi reporting.
struct WifiStatus {
    bool sta_configured;
    bool sta_connected;
    char sta_ssid[33];
    char sta_ip[16];        // dotted quad or empty
    int8_t sta_rssi;        // 0 if not connected
    char ap_ssid[33];
};
void wifi_get_status(WifiStatus* out);

// Active scan for nearby APs (blocking). Fills out[0..max); returns the count
// (<= max, <= 20 internal cap). Requires STA enabled — in AP-only mode the
// driver may return 0.
struct WifiScanAp { char ssid[33]; int8_t rssi; uint8_t authmode; };
int wifi_scan(WifiScanAp* out, int max);

// Clear stored STA credentials and reboot (factory wifi reset → AP-only).
void wifi_forget_and_reboot();
