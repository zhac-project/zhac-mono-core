// SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ha_glue.cpp — see ha_glue.h.
#include "ha_glue.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "ArduinoJson.h"
#include "device_cmd.h"
#include "device_cmd_json.h"
#include "device_shadow.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "event_bus.h"
#include "ha_bridge.h"
#include "mqtt_gw.h"
#include "sdkconfig.h"
#include "zap_common.h"
#include "zhc_adapter.h"
#include "zigbee_pool.h"

static const char* TAG = "ha_glue";

namespace {

bool value_json(uint8_t type, int32_t int_val, const char* str_val, char* out, size_t cap) {
    switch (type) {
        case VAL_BOOL:  return snprintf(out, cap, "%d", int_val ? 1 : 0) > 0;
        case VAL_INT:   return snprintf(out, cap, "%ld", (long)int_val) > 0;
        case VAL_FLOAT: return ha::x100_to_json(out, cap, int_val) > 0;   // stored x100
        case VAL_STR: {
            JsonDocument d;
            d.set(str_val);
            const size_t n = serializeJson(d, out, cap);
            return n > 0 && n < cap;
        }
        default: return false;
    }
}

// Build one device's snapshot outside the pool lock (publishing can block on
// the broker), then hand it to ha_bridge.
bool with_device(uint64_t ieee, HaDeviceCb cb, void* ctx) {
    ZapDevice dev{};
    if (!zigbee_pool_snapshot(ieee, &dev) || zap_dev_is_removed(&dev)) return false;

    char vendor[32] = {}, model[32] = {};
    zhac_adapter_resolve_labels(dev.model_id, dev.manufacturer_name,
                                vendor, sizeof(vendor), model, sizeof(model));
    constexpr size_t kExp = 2048, kAttrs = 1536;
    char* exposes = static_cast<char*>(heap_caps_malloc(kExp, MALLOC_CAP_SPIRAM));
    char* attrs   = static_cast<char*>(heap_caps_malloc(kAttrs, MALLOC_CAP_SPIRAM));
    if (!exposes || !attrs) { heap_caps_free(exposes); heap_caps_free(attrs); return false; }
    if (zhac_adapter_build_exposes_json(ieee, dev.model_id, dev.manufacturer_name,
                                        exposes, kExp) == 0) {
        snprintf(exposes, kExp, "[]");
    }
    ShadowAttr sa[32];
    const uint8_t nsa = device_shadow_get_attrs(ieee, sa, 32);
    JsonDocument a;
    JsonObject obj = a.to<JsonObject>();
    for (uint8_t j = 0; j < nsa; j++) {
        char v[64];
        char s[ATTR_STR_MAX + 1] = {};                 // str_val need not end in NUL
        if (sa[j].val_type == VAL_STR) memcpy(s, sa[j].str_val, ATTR_STR_MAX);
        if (sa[j].key[0] == '_' || !value_json(sa[j].val_type, sa[j].int_val, s, v, sizeof(v)))
            continue;
        obj[sa[j].key] = serialized(v);
    }
    if (serializeJson(a, attrs, kAttrs) >= kAttrs) snprintf(attrs, kAttrs, "{}");

    const HaDeviceSnapshot snap{ieee, dev.friendly_name, vendor[0] ? vendor : dev.manufacturer_name,
                                model[0] ? model : dev.model_id, exposes, attrs,
                                ha_battery_powered(dev.power_source)};
    cb(snap, ctx);
    heap_caps_free(exposes);
    heap_caps_free(attrs);
    return true;
}

void for_each_device(HaDeviceCb cb, void* ctx) {
    // Copy the addresses first; with_device() snapshots each one again.
    uint64_t* ieees = static_cast<uint64_t*>(heap_caps_malloc(sizeof(uint64_t) * ZAP_MAX_DEVICES,
                                                              MALLOC_CAP_SPIRAM));
    if (!ieees) return;
    uint16_t n = 0;
    zigbee_pool_lock();
    ZapDevice* pool = pool_all();
    const uint16_t cnt = pool_count();
    for (uint16_t i = 0; pool && i < cnt && n < ZAP_MAX_DEVICES; i++) {
        if (!zap_dev_is_removed(&pool[i])) ieees[n++] = pool[i].ieee_addr;
    }
    zigbee_pool_unlock();
    for (uint16_t i = 0; i < n; i++) with_device(ieees[i], cb, ctx);
    heap_caps_free(ieees);
}

bool set_attr(uint64_t ieee, const char* key, const char* value_json_text) {
    JsonDocument v;
    if (deserializeJson(v, value_json_text)) return false;
    DevCmdValue val;
    if (!device_cmd_value_from_json(v.as<JsonVariantConst>(), &val)) return false;
    const DevCmdResult r = device_cmd_set_attr(ieee, 0, key, &val);
    if (r != DEVCMD_OK) ESP_LOGW(TAG, "HA command %s=%s: %s", key, value_json_text, device_cmd_result_str(r));
    return r == DEVCMD_OK;
}

const HaBridgePlatform kPlatform = {
    "ZHAC single-chip (" CONFIG_IDF_TARGET ")",
    for_each_device,
    with_device,
    set_attr,
};

void on_attr(const Event& e) {
    const auto& z = *reinterpret_cast<const ZclAttrEvent*>(e.data);
    char v[64];
    char s[ATTR_STR_MAX + 1] = {};
    if (z.val_type == VAL_STR) memcpy(s, z.str_val, ATTR_STR_MAX);
    if (!value_json(z.val_type, z.int_val, s, v, sizeof(v))) return;
    ha_bridge_publish_state(z.ieee, z.key, v);   // Home Assistant topics, only while discovery is on
    // Plain per-device state topic, discovery or not -- what the dual-chip S3
    // publishes from every HAP update: <root>/devices/<IEEE>/state with
    // {"ieee":"0x..","attrs":{key:value}}. Without it a broker showed
    // "connected" and then nothing unless Home Assistant discovery was on.
    if (z.key[0] == '_' || !mqtt_gw_is_connected()) return;
    char suffix[48], topic[96], payload[160];
    snprintf(suffix, sizeof(suffix), "devices/%016llX/state", (unsigned long long)z.ieee);
    if (mqtt_gw_format_topic(topic, sizeof(topic), suffix) <= 0) return;
    const int n = snprintf(payload, sizeof(payload), "{\"ieee\":\"0x%016llX\",\"attrs\":{\"%s\":%s}}",
                           (unsigned long long)z.ieee, z.key, v);
    if (n > 0 && (size_t)n < sizeof(payload)) mqtt_gw_publish(topic, payload, (size_t)n, 0, false);
}

// Everything under <root>/# that is not a Home Assistant command goes to the
// rule engine (Mqtt# triggers) and Lua (zhac.on_mqtt). Our own state echoes
// are skipped. Before this file nothing set the rx callback on this build, so
// inbound MQTT never reached the rules at all.
void on_mqtt_rx(const char* topic, int topic_len, const char* data, int data_len) {
    if (ha_bridge_on_mqtt_rx(topic, topic_len, data, data_len)) return;
    char own[48];
    const int ol = snprintf(own, sizeof(own), "%s/devices/", mqtt_gw_get_root_topic());
    if (ol > 0 && topic_len >= ol && strncmp(topic, own, (size_t)ol) == 0) return;
    Event ev{};
    ev.type = EventType::MQTT_MSG;
    auto& m = *reinterpret_cast<MqttMsgEvent*>(ev.data);
    const size_t tn = (size_t)topic_len < sizeof(m.topic) - 1 ? (size_t)topic_len : sizeof(m.topic) - 1;
    const size_t pn = (size_t)(data_len > 0 ? data_len : 0) < sizeof(m.payload) - 1
                          ? (size_t)(data_len > 0 ? data_len : 0) : sizeof(m.payload) - 1;
    memcpy(m.topic, topic, tn);
    m.topic[tn] = '\0';
    if (pn) memcpy(m.payload, data, pn);
    m.payload[pn] = '\0';
    event_bus_publish(ev);
}

}  // namespace

void ha_glue_start() {
    mqtt_gw_set_rx_callback(on_mqtt_rx);
    char sub[48];
    snprintf(sub, sizeof(sub), "%s/#", mqtt_gw_get_root_topic());
    mqtt_gw_subscribe(sub, 0);

    ha_bridge_init(&kPlatform);
    event_bus_subscribe(EventType::ZCL_ATTR, on_attr);
    event_bus_subscribe(EventType::SHADOW_OPTIMISTIC, on_attr);   // commands to no-report devices
    event_bus_subscribe(EventType::DEVICE_JOIN, [](const Event& e) {
        uint64_t ieee = 0;
        memcpy(&ieee, e.data, sizeof(ieee));
        ha_bridge_device_changed(ieee);
    });
    event_bus_subscribe(EventType::DEVICE_LEAVE, [](const Event& e) {
        uint64_t ieee = 0;
        memcpy(&ieee, e.data, sizeof(ieee));
        ha_bridge_device_removed(ieee);
    });
}
