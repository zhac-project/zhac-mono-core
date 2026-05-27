# SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Phase 0: a single main.cpp that brings the board up, prints heap +
# PSRAM stats, then idles. No subsystems wired yet — the goal is to
# prove the toolchain, PSRAM detection, and partition table all work
# before pulling sibling components into the build graph.
idf_component_register(
    SRCS
        "main.cpp"
        "wifi.cpp"
        "api_status.cpp"
        "api_devices.cpp"
        "device_options.cpp"
        "sys_state.cpp"
        "api_system.cpp"
        "groups_store.cpp"
        "api_groups.cpp"
        "log_ring.cpp"
        "metrics_mqtt.cpp"
        "api_remote.cpp"
        "api_wifi.cpp"
        "api_rules.cpp"
        "api_scripts.cpp"
        "spa_serve.cpp"
        "ws_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_system
        esp_app_format
        esp_psram
        esp_timer
        nvs_flash
        spi_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        hap_master
        hap_slave
        hap_protocol
        mono_bridge
        event_bus
        metrics
        zap_store
        device_shadow
        zhc_adapter
        znp_driver
        zigbee_mgr
        zigbee_backend
        lua_engine
        lua_cjson
        simple_rules
        rule_store
        cron_parser
        ws_server
        remote_client
        mqtt
        mqtt_gw
        arduinojson
        spiffs
)
