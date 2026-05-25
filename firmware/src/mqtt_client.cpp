#include "mqtt_client.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

namespace mqttc {

namespace {
    WiFiClient g_net;
    PubSubClient g_mqtt(g_net);
    CmdHandler g_cmd_cb = nullptr;
    bool g_raw_stream = false;
    uint32_t g_last_status_ms = 0;
    uint32_t g_last_drain_ms = 0;
    char g_buf[256];

    // Route 1 publish: gui MQTT neu connect, neu khong thi append vao LittleFS.
    // Topic raw KHONG store offline (qua nang flash).
    bool route(const char* topic, const char* payload, size_t len, bool allow_store = true) {
        if (g_mqtt.connected()) {
            return g_mqtt.publish(topic, (const uint8_t*)payload, len, false);
        }
        if (!allow_store) return false;
        return storage::append(topic, payload, len);
    }

    // Replay tat ca record offline ra MQTT khi vua reconnect.
    void drainIfNeeded() {
        if (!g_mqtt.connected()) return;
        if (millis() - g_last_drain_ms < 5000) return;
        g_last_drain_ms = millis();
        int n = storage::recordCount();
        if (n <= 0) return;
        Serial.printf("[FS] draining %d offline records...\n", n);
        storage::drain([&](const char* topic, const char* payload, size_t len) -> bool {
            return g_mqtt.publish(topic, (const uint8_t*)payload, len, false);
        }, /*max=*/200);   // batch 200 record/loop, tranh block qua lau
    }

    void onMessage(char* topic, byte* payload, unsigned int len) {
        if (!g_cmd_cb) return;
        if (len >= sizeof(g_buf)) len = sizeof(g_buf) - 1;
        memcpy(g_buf, payload, len);
        g_buf[len] = '\0';
        g_cmd_cb(topic, g_buf);
    }

    void wifiConnect() {
        Serial.printf("[NET] WiFi connecting to %s ", WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
            delay(250); Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) Serial.printf(" OK %s\n", WiFi.localIP().toString().c_str());
        else Serial.println(" FAIL");
    }

    bool mqttConnect() {
        if (WiFi.status() != WL_CONNECTED) wifiConnect();
        if (WiFi.status() != WL_CONNECTED) return false;
        g_mqtt.setServer(MQTT_HOST, MQTT_PORT);
        g_mqtt.setCallback(onMessage);
        g_mqtt.setKeepAlive(MQTT_KEEPALIVE);
        Serial.printf("[MQTT] connect to %s:%d as %s ", MQTT_HOST, MQTT_PORT, DEVICE_ID);
        if (g_mqtt.connect(DEVICE_ID)) {
            Serial.println("OK");
            g_mqtt.subscribe(MQTT_TOPIC_CMD);
            return true;
        }
        Serial.printf("FAIL rc=%d\n", g_mqtt.state());
        return false;
    }
}

bool begin(CmdHandler cmd_cb) {
    g_cmd_cb = cmd_cb;
    storage::begin();
    wifiConnect();
    return mqttConnect();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) { wifiConnect(); return; }
    if (!g_mqtt.connected()) { mqttConnect(); return; }
    g_mqtt.loop();
    drainIfNeeded();

    if (millis() - g_last_status_ms > 10000) {
        publishStatus();
        g_last_status_ms = millis();
    }
}

bool isConnected() { return g_mqtt.connected(); }

void enableRawStream(bool on) { g_raw_stream = on; }
bool rawStreamEnabled() { return g_raw_stream; }

void publishActivity(const inference::Result& r) {
    JsonDocument doc;
    doc["ts"] = (uint64_t)millis();
    doc["class"] = ACTIVITY_NAMES[r.cls];
    doc["confidence"] = r.confidence;
    char buf[160];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    route(MQTT_TOPIC_ACTIVITY, buf, n);
}

void publishStep(const pdr::State& s) {
    JsonDocument doc;
    doc["ts"] = (uint64_t)millis();
    doc["count"] = s.step_count;
    doc["cadence"] = s.cadence_spm;
    doc["stride"] = s.last_stride_m;
    char buf[160];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    route(MQTT_TOPIC_STEP, buf, n);
}

void publishPose(const pdr::State& s) {
    JsonDocument doc;
    doc["ts"] = (uint64_t)millis();
    doc["x"] = s.x_m;
    doc["y"] = s.y_m;
    doc["heading_deg"] = s.heading_deg;
    char buf[160];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    route(MQTT_TOPIC_POSE, buf, n);
}

void publishStatus() {
    JsonDocument doc;
    doc["ts"] = (uint64_t)millis();
    doc["uptime_s"] = millis() / 1000;
    doc["rssi"] = WiFi.RSSI();
    doc["free_heap"] = (uint32_t)ESP.getFreeHeap();
    doc["raw_stream"] = g_raw_stream;
    doc["offline_records"] = storage::recordCount();
    doc["fs_used"] = (uint32_t)storage::bytesUsed();
    char buf[220];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    // status retained khi online; offline thi khong can store (chi la heartbeat)
    if (g_mqtt.connected()) g_mqtt.publish(MQTT_TOPIC_STATUS, (const uint8_t*)buf, n, true);
}

void publishRaw(uint32_t ts_ms, float ax, float ay, float az, float gx, float gy, float gz) {
    if (!g_raw_stream) return;
    JsonDocument doc;
    doc["ts"] = (uint64_t)ts_ms;
    doc["ax"] = ax; doc["ay"] = ay; doc["az"] = az;
    doc["gx"] = gx; doc["gy"] = gy; doc["gz"] = gz;
    char buf[200];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    // Raw KHONG luu offline (qua nang flash) - chi publish khi online
    route(MQTT_TOPIC_RAW, buf, n, /*allow_store=*/false);
}

} // namespace mqttc
