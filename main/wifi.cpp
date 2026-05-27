// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// wifi.cpp — WiFi bring-up with AP-fallback STA mode.
//
// Behaviour:
//   - Boot reads `wifi/ssid` and `wifi/pwd` from NVS.
//   - If SSID is present  → APSTA mode: AP stays up as fallback while
//     STA tries to connect. AP visible until the user re-flashes or
//     posts an empty SSID to clear credentials.
//   - If SSID is missing  → AP-only. User connects to ZHAC-MONO and
//     posts /api/wifi with their home SSID.
//
// Auto-reconnect on disconnect is wired through esp_event so a router
// reboot or short outage recovers on its own without RTOS work in
// this file.
#include "wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "wifi";

static esp_netif_t* s_netif_ap  = nullptr;
static esp_netif_t* s_netif_sta = nullptr;
static bool         s_sta_connected = false;

static void on_wifi_event(void* /*arg*/, esp_event_base_t base,
                          int32_t id, void* /*data*/) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start — connecting…");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected — retrying in 5 s");
        s_sta_connected = false;
        // Cheap retry; firmware-level loop is fine for DIY use.
        // A production setup would back off exponentially.
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_sta_connected = true;
        ESP_LOGI(TAG, "STA got IP");
    }
}

static bool load_creds(char* ssid, size_t ssid_cap,
                       char* pwd,  size_t pwd_cap) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssid_cap;
    esp_err_t e = nvs_get_str(h, "ssid", ssid, &sl);
    if (e != ESP_OK || sl <= 1) { nvs_close(h); return false; }
    size_t pl = pwd_cap;
    if (nvs_get_str(h, "pwd", pwd, &pl) != ESP_OK) { pwd[0] = '\0'; }
    nvs_close(h);
    return true;
}

void wifi_start() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &on_wifi_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &on_wifi_event, nullptr));

    // AP config (always — used as captive fallback or sole mode).
    wifi_config_t ap_cfg{};
    std::strcpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), "ZHAC-MONO");
    ap_cfg.ap.ssid_len       = 9;
    ap_cfg.ap.channel        = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;

    char ssid[33] = {0};
    char pwd[65]  = {0};
    bool have_sta = load_creds(ssid, sizeof(ssid), pwd, sizeof(pwd));

    if (have_sta) {
        ESP_LOGI(TAG, "STA creds present, starting APSTA (AP = fallback)");
        wifi_config_t sta_cfg{};
        snprintf(reinterpret_cast<char*>(sta_cfg.sta.ssid),
                 sizeof(sta_cfg.sta.ssid), "%s", ssid);
        snprintf(reinterpret_cast<char*>(sta_cfg.sta.password),
                 sizeof(sta_cfg.sta.password), "%s", pwd);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,  &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    } else {
        ESP_LOGI(TAG, "no STA creds, AP-only");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    uint8_t mac[6]{};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    ESP_LOGI(TAG, "AP up: SSID=ZHAC-MONO open, ch=6, "
                  "BSSID=%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "default AP IP is 192.168.4.1 — visit http://192.168.4.1/");
}

bool wifi_save_sta(const char* ssid, const char* password) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = true;
    if (!ssid || ssid[0] == '\0') {
        // Empty → clear both.
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pwd");
    } else {
        if (nvs_set_str(h, "ssid", ssid) != ESP_OK) ok = false;
        if (nvs_set_str(h, "pwd", password ? password : "") != ESP_OK)
            ok = false;
    }
    if (ok) nvs_commit(h);
    nvs_close(h);
    return ok;
}

bool wifi_load_sta_ssid(char* out_ssid, size_t cap) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = cap;
    esp_err_t e = nvs_get_str(h, "ssid", out_ssid, &l);
    nvs_close(h);
    return e == ESP_OK && out_ssid[0] != '\0';
}

void wifi_get_status(WifiStatus* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    char ssid[33] = {0};
    out->sta_configured = wifi_load_sta_ssid(ssid, sizeof(ssid));
    snprintf(out->sta_ssid, sizeof(out->sta_ssid), "%s", ssid);
    out->sta_connected = s_sta_connected;
    if (s_sta_connected && s_netif_sta) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(s_netif_sta, &ip) == ESP_OK) {
            snprintf(out->sta_ip, sizeof(out->sta_ip), IPSTR,
                     IP2STR(&ip.ip));
        }
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) out->sta_rssi = ap.rssi;
    }
    std::strcpy(out->ap_ssid, "ZHAC-MONO");
}

int wifi_scan(WifiScanAp* out, int max) {
    if (!out || max <= 0) return 0;
    wifi_scan_config_t cfg{};                       // active scan, all channels
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK)  // blocking
        return 0;
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;
    static wifi_ap_record_t recs[20];
    uint16_t got = num > 20 ? 20 : num;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK) return 0;
    int n = 0;
    for (uint16_t i = 0; i < got && n < max; i++) {
        if (recs[i].ssid[0] == '\0') continue;      // skip hidden
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", (const char*)recs[i].ssid);
        out[n].rssi     = recs[i].rssi;
        out[n].authmode = recs[i].authmode;
        n++;
    }
    return n;
}

void wifi_forget_and_reboot() {
    wifi_save_sta("", "");                  // empty SSID clears creds
    ESP_LOGI(TAG, "wifi creds cleared — rebooting to AP-only");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
