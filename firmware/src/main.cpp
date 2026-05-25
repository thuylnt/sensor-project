#include <Arduino.h>
#include "config.h"
#include "imu.h"
#include "preprocess.h"
#include "pdr.h"
#include "inference.h"
#include "mqtt_client.h"
#include "calibration.h"
#include "storage.h"

// ===========================================================================
// USTH Sensor Project - ESP32 HAR + PDR-lite
// ---------------------------------------------------------------------------
// 2 task FreeRTOS:
//   - Core 1: sampling + preprocess + buffer window + inference + PDR
//   - Core 0: MQTT/WiFi loop (PubSubClient khong dung trong task khac)
// ===========================================================================

namespace {
    // Ring buffer cua window dang slide: WINDOW_SIZE samples x NUM_AXES
    float g_window[WINDOW_SIZE * NUM_AXES];
    int   g_write_idx = 0;
    int   g_filled = 0;
    int   g_hop_counter = 0;

    inference::Result g_last_result{};
    pdr::State g_last_state{};

    // Lap lai ham serializeJson nhe khong can - giao thiep MQTT o core 0 qua queue
    QueueHandle_t g_pub_queue = nullptr;

    enum PubKind : uint8_t { PUB_ACTIVITY, PUB_STEP, PUB_POSE, PUB_RAW };
    struct PubMsg {
        PubKind kind;
        union {
            inference::Result act;
            pdr::State pdr_s;
            struct { uint32_t ts; float ax, ay, az, gx, gy, gz; } raw;
        };
    };

    void enqueuePub(const PubMsg& m) {
        if (g_pub_queue) xQueueSend(g_pub_queue, &m, 0);
    }

    void onMqttCmd(const char* topic, const char* payload) {
        Serial.printf("[CMD] %s -> %s\n", topic, payload);
        if (strstr(topic, "/reset")) {
            pdr::reset();
        } else if (strstr(topic, "/raw_on")) {
            mqttc::enableRawStream(true);
        } else if (strstr(topic, "/raw_off")) {
            mqttc::enableRawStream(false);
        } else if (strstr(topic, "/calib_clear")) {
            calibration::clear();
            Serial.println("[CMD] calibration cleared, restart to take effect");
        } else if (strstr(topic, "/log_clear")) {
            storage::clearAll();
            Serial.println("[CMD] offline log cleared");
        } else if (strstr(topic, "/log_status")) {
            Serial.printf("[FS] records=%d used=%u/%u\n",
                          storage::recordCount(),
                          (unsigned)storage::bytesUsed(),
                          (unsigned)storage::bytesTotal());
        }
    }

    // Task sampling + inference (core 1)
    void sensorTask(void*) {
        const TickType_t period = pdMS_TO_TICKS(1000 / SAMPLE_RATE_HZ);
        TickType_t last = xTaskGetTickCount();
        ImuSample s;
        for (;;) {
            vTaskDelayUntil(&last, period);
            if (!imu::read(s)) continue;
            preprocess::applyLowPass(s);
            preprocess::applyGravityHPF(s);

            // Day vao window
            int base = g_write_idx * NUM_AXES;
            g_window[base + 0] = s.ax; g_window[base + 1] = s.ay; g_window[base + 2] = s.az;
            g_window[base + 3] = s.gx; g_window[base + 4] = s.gy; g_window[base + 5] = s.gz;
            g_write_idx = (g_write_idx + 1) % WINDOW_SIZE;
            if (g_filled < WINDOW_SIZE) g_filled++;
            g_hop_counter++;

            // PDR moi sample
            float lin_norm = preprocess::accelNormLinear(s);
            const pdr::State& st = pdr::update(s, lin_norm);
            if (st.step_event) {
                PubMsg m{}; m.kind = PUB_STEP; m.pdr_s = st;
                enqueuePub(m);
            }

            // Inference moi HOP samples khi du window
            if (g_filled >= WINDOW_SIZE && g_hop_counter >= WINDOW_HOP) {
                g_hop_counter = 0;
                static float work[WINDOW_SIZE * NUM_AXES];
                // Sao chep window dang sliding ra dang contig (read_idx tinh tu g_write_idx)
                for (int i = 0; i < WINDOW_SIZE; ++i) {
                    int src = ((g_write_idx + i) % WINDOW_SIZE) * NUM_AXES;
                    int dst = i * NUM_AXES;
                    for (int a = 0; a < NUM_AXES; ++a) work[dst + a] = g_window[src + a];
                }
                preprocess::zscoreWindow(work);

                inference::Result r;
                if (inference::classify(work, r)) {
                    g_last_result = r;
                    pdr::setActivity(r.cls);
                    PubMsg m{}; m.kind = PUB_ACTIVITY; m.act = r;
                    enqueuePub(m);

                    PubMsg p{}; p.kind = PUB_POSE; p.pdr_s = st;
                    enqueuePub(p);
                }
            }

            // Raw stream
            if (mqttc::rawStreamEnabled()) {
                PubMsg m{}; m.kind = PUB_RAW;
                m.raw.ts = s.ts_ms;
                m.raw.ax = s.ax; m.raw.ay = s.ay; m.raw.az = s.az;
                m.raw.gx = s.gx; m.raw.gy = s.gy; m.raw.gz = s.gz;
                enqueuePub(m);
            }
        }
    }

    // Task MQTT (core 0)
    void mqttTask(void*) {
        mqttc::begin(onMqttCmd);
        PubMsg m;
        for (;;) {
            mqttc::loop();
            while (xQueueReceive(g_pub_queue, &m, 0) == pdTRUE) {
                switch (m.kind) {
                    case PUB_ACTIVITY: mqttc::publishActivity(m.act); break;
                    case PUB_STEP:     mqttc::publishStep(m.pdr_s);   break;
                    case PUB_POSE:     mqttc::publishPose(m.pdr_s);   break;
                    case PUB_RAW:
                        mqttc::publishRaw(m.raw.ts, m.raw.ax, m.raw.ay, m.raw.az, m.raw.gx, m.raw.gy, m.raw.gz);
                        break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== USTH PDR firmware ===");

    if (!imu::begin()) {
        Serial.println("[FATAL] IMU init failed - check wiring (SDA=21 SCL=22)");
    }

    calibration::Data cal;
    if (calibration::load(cal)) {
        Serial.println("[CAL] loaded from NVS");
    } else {
        Serial.println("[CAL] no calibration in NVS, using defaults (run tools/calibrate_mpu.py)");
    }
    calibration::printSummary(cal);
    imu::applyCalibration(cal.acc_bias, cal.acc_scale, cal.gyro_bias);

    preprocess::initFilters();
    pdr::begin();
    inference::begin();

    g_pub_queue = xQueueCreate(64, sizeof(PubMsg));

    xTaskCreatePinnedToCore(sensorTask, "sensor", 8192, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(mqttTask,   "mqtt",   8192, nullptr, 3, nullptr, 0);
}

void loop() {
    // Tat ca xu ly o 2 task FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
}
