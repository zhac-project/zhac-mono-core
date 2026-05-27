// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// zhac-mono-core / main.cpp — Phase 1 boot.
//
// Phase 1 goal: prove the mono_bridge shim links cleanly by pulling
// the hap_master + hap_slave overrides into the build graph, calling
// their init, and registering a logging callback on each side so the
// dispatch rails are exercised end-to-end before Phase 2 wires them
// to real services.
//
// Subsystem init (WiFi, HTTP/SPA, Zigbee, Lua, rules, MQTT, WS) lands
// in the next phase per PLAN.md.

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

// Bridge shim — both header families live in zhac-mono-core/components/
// and override the sibling implementations.
#include "hap_master.h"
#include "hap_slave.h"
#include "hap_protocol.h"

#include "wifi.h"
#include "ws_server.h"
#include "mqtt_gw.h"
#include "esp_http_server.h"
#include "api_status.h"
#include "api_devices.h"
#include "api_wifi.h"
#include "api_rules.h"
#include "api_scripts.h"
#include "spa_serve.h"
#include "ws_bridge.h"

#include "event_bus.h"
#include "zap_store.h"
#include "device_shadow.h"
#include "zhc_adapter.h"
#include "znp_driver.h"
#include "zigbee_mgr.h"
#include "zigbee_backend.h"
#include "lua_engine.h"
#include "simple_rules.h"
#include "device_options.h"
#include "sys_state.h"
#include "api_system.h"
#include "zigbee_diagnostics.h"
#include "api_groups.h"
#include "log_ring.h"
#include "api_remote.h"
#include "remote_client.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

extern "C" void lua_engine_rules_hook_install(void);
extern "C" void metrics_mqtt_publisher_start();   // metrics_mqtt.cpp

#include <cinttypes>

static const char* TAG = "zhac-mono";

static void log_chip_info() {
    esp_chip_info_t info{};
    esp_chip_info(&info);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    ESP_LOGI(TAG, "chip: %s, rev v%u.%u, %d cores, flash %" PRIu32 " MB",
             CONFIG_IDF_TARGET,
             info.revision / 100,
             info.revision % 100,
             info.cores,
             flash_size / (1024 * 1024));
}

static void log_psram_info() {
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM not initialized — mono build REQUIRES PSRAM");
        return;
    }
    size_t size = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM: initialized, %u MB", (unsigned)(size / (1024 * 1024)));
}

static void log_heap_info() {
    ESP_LOGI(TAG, "heap internal: free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "heap spiram:   free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "heap total:    free=%u (32-bit-only=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_32BIT));
}

// Smoke-test the bridge by registering trivial callbacks on each side
// and synthesising a frame in each direction. Real producers (zigbee
// reports, REST handlers) will issue these in Phase 2.
static void bridge_smoke_test() {
    hap_master_set_callback([](const HapFrame& f) {
        ESP_LOGI(TAG, "master cb fired: type=%u seq=%u",
                 (unsigned)f.type, f.seq);
    });
    hap_slave_set_callback([](const HapFrame& f) {
        ESP_LOGI(TAG, "slave  cb fired: type=%u seq=%u",
                 (unsigned)f.type, f.seq);
    });

    HapFrame probe{};
    probe.type        = HapMsgType::HEARTBEAT;
    probe.seq         = 1;
    probe.payload_len = 0;

    ESP_LOGI(TAG, "smoke: master_send (outbound → slave cb)");
    hap_master_send(probe);

    probe.seq = 2;
    ESP_LOGI(TAG, "smoke: slave_send  (inbound  → master cb)");
    hap_slave_send(probe);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "boot — zhac-mono-core (Phase 1 — shim + bridge)");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase — reformatting");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    // System flags + API-auth token (NVS-backed). Must follow nvs_flash_init.
    sys_state_init();

    log_ring_init();   // PSRAM log ring + esp_log vprintf hook (capture early)

    log_chip_info();
    log_psram_info();
    log_heap_info();

    hap_master_init();
    hap_slave_init();
    bridge_smoke_test();

    // ── Phase 2: Zigbee path ─────────────────────────────────────
    // Init order matters: event_bus first (other subsystems publish
    // into it), then NVS-backed stores (zap_store / device_shadow),
    // then the device-definition adapter, then the UART transport,
    // and finally zigbee_mgr which wires it all together. The
    // zigbee_backend glue registers send/configure callbacks on
    // zhc_adapter so the same dispatcher used in dual-chip ZHAC
    // works here too.
    event_bus_init();
    zap_store_init();
    zap_store_flush_init();
    device_shadow_init();
    zhac_adapter_init();
    zb_diag_init();   // unhandled-frame ring for GET /api/diagnostics/unhandled
    znp_driver_init();
    zigbee_backend_register();
    if (!zigbee_mgr_init()) {
        ESP_LOGE(TAG, "zigbee_mgr_init failed — Zigbee path inactive");
    } else {
        ESP_LOGI(TAG, "Zigbee path up (ZNP via UART)");
    }

    // Re-apply persisted per-device options (occupancy_timeout / debounce_ms
    // / throttle_ms) now that device_shadow + the device pool are up.
    device_options_restore_all();

    // ── Phase 3: rules + Lua ─────────────────────────────────────
    // simple_rules first (registers its NVS-backed store, allocates
    // s_rules cache); lua_engine next (spins up TaskLua but defers
    // script source load); the rules-hook glue wires
    // simple_rules' `script.run` action to push runs onto the Lua
    // scheduler. Loading scripts is deferred to after the network
    // and HTTP stack are up so scripts that touch them at top level
    // don't race the subsystem they depend on (same fix pattern that
    // resolved the dual-chip boot crash).
    simple_rules_init();
    const bool lua_ok = lua_engine_init();
    if (!lua_ok) {
        ESP_LOGW(TAG, "lua_engine_init returned false — scripts disabled");
    }
    lua_engine_rules_hook_install();

    wifi_start();

    // Mount SPA partition before httpd routes so the catchall finds
    // index.html. Empty partition is fine — handler returns 404 and
    // the API endpoints still work.
    spa_mount();

    // ── Phase 4: WS + MQTT ───────────────────────────────────────
    // ws_server owns the httpd; register the placeholder "/" against
    // its handle until the SPA mount and REST handlers land.
    ws_server_init();
    httpd_handle_t hd = ws_server_get_handle();
    if (hd) {
        static const char* kAlivePage =
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>zhac-mono-core</title></head><body>"
            "<h1>ZHAC mono-core</h1>"
            "<p>Phase 4 — WS server + MQTT up. SPA + REST pending.</p>"
            "</body></html>";
        httpd_uri_t root{};
        root.uri    = "/";
        root.method = HTTP_GET;
        root.handler = [](httpd_req_t* req) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, kAlivePage, HTTPD_RESP_USE_STRLEN);
        };
        httpd_register_uri_handler(hd, &root);
        ESP_LOGI(TAG, "HTTP / handler registered on ws_server httpd");
        api_status_register(hd);
        api_devices_register(hd);
        api_wifi_register(hd);
        api_rules_register(hd);
        api_scripts_register(hd);
        api_system_register(hd);
        api_groups_register(hd);
        api_remote_register(hd);
        // SPA catchall must register LAST so its `/*` pattern doesn't
        // shadow the specific /api/* routes (esp_http_server with
        // wildcard match falls back to the catchall only when no
        // earlier handler claims the URI).
        spa_register(hd);
    }
    // WS RX dispatcher + outbound event-bus push subscriptions.
    ws_bridge_install();

#ifdef CONFIG_ZHAC_REMOTE_CLIENT_ENABLE
    // Feed WiFi up/down into the remote client's state machine (bit positions
    // must match EVB_WIFI_UP=1<<2 / EVB_WIFI_DOWN=1<<3 in remote_client.cpp).
    extern EventGroupHandle_t s_remote_evt;   // owned by remote_client.cpp
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        [](void*, esp_event_base_t, int32_t, void*) {
            if (s_remote_evt) xEventGroupSetBits(s_remote_evt, 1 << 2);
        }, nullptr);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
        [](void*, esp_event_base_t, int32_t, void*) {
            if (s_remote_evt) xEventGroupSetBits(s_remote_evt, 1 << 3);
        }, nullptr);
#endif
    remote_client_init();   // no-op stub when remote disabled; reads NVS + self-enables

    // mqtt_gw is config-gated — if no broker URL is provisioned (NVS),
    // mqtt_gw_start logs and idles until mqtt_gw_configure() is called
    // from the REST handler. Mono boots cleanly either way.
    mqtt_gw_init();
    mqtt_gw_start();
    metrics_mqtt_publisher_start();   // metrics_mqtt.cpp (no-op if exporter off)

    if (lua_ok) {
        // Safe to load now: every subsystem a script might call into
        // (zigbee_mgr, simple_rules, http stack, …) is initialised.
        lua_engine_load_all();
        ESP_LOGI(TAG, "lua_engine: scripts loaded");
    }

    int tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive (tick=%d, uptime=%" PRId64 " s)",
                 ++tick, esp_timer_get_time() / 1000000);
        if ((tick % 6) == 0) log_heap_info();
    }
}
