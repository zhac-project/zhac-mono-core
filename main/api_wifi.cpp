// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// api_wifi.cpp — WiFi REST. Net-core scheme:
//   GET  /api/wifi/status      → status snapshot (superset of net-core +
//                                 mono fields)
//   GET  /api/wifi/scan        → {"networks":[{ssid,rssi,auth}]}
//   POST /api/wifi/connect     → {"ssid","password"} persist + reboot
//   POST /api/wifi/disconnect  → clear creds + reboot (AP-only)
// Legacy aliases GET/POST /api/wifi map to status/connect.
//
// Reboot-to-apply: hot-rotating an APSTA STA config without disrupting AP
// clients is finicky, so credential changes reboot (response flushed first).
#include "api_wifi.h"

#include <cstring>

#include "ArduinoJson.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "wifi.h"

static const char* TAG = "api_wifi";

static const char* auth_str(uint8_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:            return "open";
        case WIFI_AUTH_WEP:             return "wep";
        case WIFI_AUTH_WPA_PSK:         return "wpa";
        case WIFI_AUTH_WPA2_PSK:        return "wpa2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "wpa2";
        case WIFI_AUTH_WPA3_PSK:        return "wpa3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "wpa3";
        case WIFI_AUTH_ENTERPRISE:      return "enterprise";
        default:                        return "other";
    }
}

static esp_err_t handle_wifi_status(httpd_req_t* req) {
    WifiStatus st{};
    wifi_get_status(&st);

    JsonDocument doc;
    // net-core-shaped keys (what the shared SPA reads)
    doc["mode"] = st.sta_connected ? "sta" : "ap";
    doc["ssid"] = st.sta_connected ? st.sta_ssid : st.ap_ssid;
    doc["ip"]   = st.sta_ip;
    doc["rssi"] = st.sta_rssi;
    // mono superset
    doc["sta_configured"] = st.sta_configured;
    doc["sta_connected"]  = st.sta_connected;
    doc["sta_ssid"]       = st.sta_ssid;
    doc["sta_ip"]         = st.sta_ip;
    doc["sta_rssi"]       = st.sta_rssi;
    doc["ap_ssid"]        = st.ap_ssid;

    char buf[320];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t handle_wifi_scan(httpd_req_t* req) {
    WifiScanAp aps[20];
    int n = wifi_scan(aps, 20);
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = aps[i].ssid;
        o["rssi"] = aps[i].rssi;
        o["auth"] = auth_str(aps[i].authmode);
    }
    char buf[1024];
    size_t bn = serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, bn);
}

static esp_err_t handle_wifi_connect(httpd_req_t* req) {
    char body[160];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    body[n] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, body, n)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    const char* ssid = doc["ssid"]     | (const char*)"";
    const char* pwd  = doc["password"] | (const char*)"";

    if (!wifi_save_sta(ssid, pwd)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs write failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ssid[0]
        ? "{\"ok\":true,\"saved\":true,\"reboot\":\"in 2s\"}"
        : "{\"ok\":true,\"cleared\":true,\"reboot\":\"in 2s\"}");
    ESP_LOGI(TAG, "rebooting to apply WiFi change…");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handle_wifi_disconnect(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"reboot\":\"in 1s\"}");
    ESP_LOGI(TAG, "wifi disconnect — clearing creds + reboot");
    wifi_forget_and_reboot();   // delays then esp_restart
    return ESP_OK;
}

bool api_wifi_register(httpd_handle_t hd) {
    if (!hd) return false;
    struct { const char* uri; httpd_method_t m; esp_err_t (*h)(httpd_req_t*); } routes[] = {
        { "/api/wifi",            HTTP_GET,  handle_wifi_status     },  // legacy alias
        { "/api/wifi",            HTTP_POST, handle_wifi_connect    },  // legacy alias
        { "/api/wifi/status",     HTTP_GET,  handle_wifi_status     },
        { "/api/wifi/scan",       HTTP_GET,  handle_wifi_scan       },
        { "/api/wifi/connect",    HTTP_POST, handle_wifi_connect    },
        { "/api/wifi/disconnect", HTTP_POST, handle_wifi_disconnect },
    };
    httpd_uri_t u{};
    for (auto& r : routes) {
        u.uri = r.uri; u.method = r.m; u.handler = r.h;
        httpd_register_uri_handler(hd, &u);
    }
    ESP_LOGI(TAG, "wifi routes registered (/api/wifi[/status|scan|connect|disconnect])");
    return true;
}
