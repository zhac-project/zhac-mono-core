// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// api_status.cpp — `GET /api/status` runtime snapshot.
//
// Single-chip: every field net-core split across S3 + the P4 `s_p4_*`
// heartbeat cache is read directly here (same chip). No HAP roundtrip.
#include "api_status.h"

#include <cinttypes>
#include <cstdio>

#include "ArduinoJson.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_gw.h"
#include "sdkconfig.h"
#include "sys_state.h"
#include "ws_server.h"
#include "zigbee_mgr.h"
#include "zigbee_pool.h"
#include "log_ring.h"

static const char* TAG = "api_status";

static esp_err_t handle_get_status(httpd_req_t* req) {
    JsonDocument doc;

    // Chip / build
    esp_chip_info_t info{};
    esp_chip_info(&info);
    doc["target"]   = CONFIG_IDF_TARGET;
    doc["cores"]    = info.cores;
    doc["revision"] = info.revision;
    const esp_app_desc_t* app = esp_app_get_description();
    if (app) doc["fw"] = app->version;

    doc["uptime_s"] = (uint32_t)(esp_timer_get_time() / 1000000);

    // Heap (internal + PSRAM: free / min / largest block)
    doc["heap_internal_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    doc["heap_internal_min"]  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    doc["heap_internal_blk"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    doc["heap_total_free"]    = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    if (esp_psram_is_initialized()) {
        doc["psram_size"] = (uint32_t)esp_psram_get_size();
        doc["psram_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        doc["psram_min"]  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        doc["psram_blk"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    } else {
        doc["psram_size"] = 0;
    }

    // WiFi
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        const char* m = "off";
        switch (mode) {
            case WIFI_MODE_STA:   m = "sta";   break;
            case WIFI_MODE_AP:    m = "ap";    break;
            case WIFI_MODE_APSTA: m = "apsta"; break;
            default: break;
        }
        doc["wifi_mode"] = m;
    }
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ipi{};
    if (sta && esp_netif_get_ip_info(sta, &ipi) == ESP_OK && ipi.ip.addr) {
        uint32_t a = ipi.ip.addr;
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                 (unsigned)(a & 0xff), (unsigned)((a >> 8) & 0xff),
                 (unsigned)((a >> 16) & 0xff), (unsigned)((a >> 24) & 0xff));
        doc["ip"] = ip;
    }
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) doc["rssi"] = ap.rssi;

    // Zigbee + devices
    doc["zigbee_ok"]    = !zigbee_mgr_crashed();
    doc["device_count"] = pool_count_active();

    // MQTT
    doc["mqtt_connected"]  = mqtt_gw_is_connected();
    doc["mqtt_active"]     = mqtt_gw_is_active();
    doc["mqtt_root_topic"] = mqtt_gw_get_root_topic();

    // WS + system flags
    doc["ws_clients"]      = ws_server_client_count();
    doc["metrics_enabled"] = sys_metrics_enabled();
    doc["ap_disabled"]     = sys_ap_disabled();
    doc["auth_enabled"]    = sys_auth_enabled();
    doc["log_mqtt_enabled"] = log_sinks_get_mqtt_enabled();
    doc["log_ws_enabled"]   = log_sinks_get_ws_enabled();
#ifdef CONFIG_ZHAC_REMOTE_CLIENT_ENABLE
    doc["remote_available"] = true;
#else
    doc["remote_available"] = false;
#endif

    char buf[1024];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

bool api_status_register(httpd_handle_t hd) {
    if (!hd) return false;
    httpd_uri_t u{};
    u.uri     = "/api/status";
    u.method  = HTTP_GET;
    u.handler = handle_get_status;
    esp_err_t e = httpd_register_uri_handler(hd, &u);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "register failed: %s", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "GET /api/status registered");
    return true;
}
