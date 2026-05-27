#include <Arduino.h>
#include "config.h"
#include "imu.h"
#include "preprocess.h"
#include "pdr.h"
#include "inference.h"
#include "mqtt_client.h"
#include "calibration.h"
#include "storage.h"
#include "display.h"
#include "i2c_bus.h"

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

    // Alt paths: tinh "duong gia" cho pose_raw va pose_calib song song.
    //   raw   = tich phan gyro_z THUC (chua tru bias calib), no EMA
    //   calib = tich phan gyro_z DA TRU BIAS calib, no EMA dong
    // Step events va stride lay tu pipeline full -> 3 path co cung step rate,
    // chi khac heading -> hien thi su khac biet do drift heading.
    struct AltPath { float heading_deg; float x_m; float y_m; };
    AltPath g_alt_raw{};
    AltPath g_alt_calib{};

    // Lap lai ham serializeJson nhe khong can - giao thiep MQTT o core 0 qua queue
    QueueHandle_t g_pub_queue = nullptr;

    enum PubKind : uint8_t { PUB_ACTIVITY, PUB_STEP, PUB_POSE, PUB_POSE_RAW, PUB_POSE_CALIB, PUB_RAW };
    struct PubMsg {
        PubKind kind;
        union {
            inference::Result act;
            pdr::State pdr_s;
            struct { float x, y, heading_deg; } alt;
            struct { uint32_t ts; float ax, ay, az, gx, gy, gz; } raw;
        };
    };

    void enqueuePub(const PubMsg& m) {
        if (g_pub_queue) xQueueSend(g_pub_queue, &m, 0);
    }

    void resetAllPaths() {
        pdr::reset();
        g_alt_raw   = AltPath{};
        g_alt_calib = AltPath{};
    }

    void onMqttCmd(const char* topic, const char* payload) {
        Serial.printf("[CMD] %s -> %s\n", topic, payload);
        if (strstr(topic, "/reset")) {
            resetAllPaths();
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
        ImuSample s, s_raw;
        const float dt = 1.0f / (float)SAMPLE_RATE_HZ;
        const float RAD2DEG = 57.29577951308232f;
        for (;;) {
            vTaskDelayUntil(&last, period);
            if (!imu::read(s, &s_raw)) continue;
            preprocess::applyLowPass(s);
            preprocess::applyGravityHPF(s);

            // Cap nhat alt headings (truoc khi tinh step de xoay ngay luc step)
            //   raw:   gyro_z THUC, khong tru bias - drift nhanh nhat
            //   calib: gyro_z da tru bias calib tinh - drift cham hon
            //   full:  pdr xu ly + EMA dong - drift cham nhat
            g_alt_raw.heading_deg   += s_raw.gz * RAD2DEG * dt;
            g_alt_calib.heading_deg += s.gz     * RAD2DEG * dt;
            while (g_alt_raw.heading_deg < 0)    g_alt_raw.heading_deg += 360.0f;
            while (g_alt_raw.heading_deg >= 360) g_alt_raw.heading_deg -= 360.0f;
            while (g_alt_calib.heading_deg < 0)    g_alt_calib.heading_deg += 360.0f;
            while (g_alt_calib.heading_deg >= 360) g_alt_calib.heading_deg -= 360.0f;

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
            g_last_state = st;   // copy snapshot cho display task / status topic
            if (st.step_event) {
                // Tien alt paths theo HEADING RIENG cua minh - cung stride
                float stride = st.last_stride_m;
                float hr = g_alt_raw.heading_deg / RAD2DEG;
                float hc = g_alt_calib.heading_deg / RAD2DEG;
                g_alt_raw.x_m   += stride * sinf(hr);
                g_alt_raw.y_m   += stride * cosf(hr);
                g_alt_calib.x_m += stride * sinf(hc);
                g_alt_calib.y_m += stride * cosf(hc);

                PubMsg m{}; m.kind = PUB_STEP; m.pdr_s = st;
                enqueuePub(m);
            }

            // Inference moi HOP samples khi du window
            if (g_filled >= WINDOW_SIZE && g_hop_counter >= WINDOW_HOP) {
                g_hop_counter = 0;

#if MILESTONE_LEVEL <= 2
                // Milestone 2: bypass inference, gia "walk" voi confidence 0.9.
                // PDR van chay (step counter) - de quan sat tren dashboard.
                inference::Result r = { ACT_WALK, 0.9f, {0.05f, 0.85f, 0.05f, 0.05f} };
                pdr::setActivity(ACT_WALK);
                PubMsg m{}; m.kind = PUB_ACTIVITY; m.act = r; enqueuePub(m);
                PubMsg p{}; p.kind = PUB_POSE;     p.pdr_s = st; enqueuePub(p);
                PubMsg pc{}; pc.kind = PUB_POSE_CALIB;
                pc.alt.x = g_alt_calib.x_m; pc.alt.y = g_alt_calib.y_m;
                pc.alt.heading_deg = g_alt_calib.heading_deg;
                enqueuePub(pc);
                PubMsg pr{}; pr.kind = PUB_POSE_RAW;
                pr.alt.x = g_alt_raw.x_m; pr.alt.y = g_alt_raw.y_m;
                pr.alt.heading_deg = g_alt_raw.heading_deg;
                enqueuePub(pr);
                g_last_result = r;
#else
                static float work[WINDOW_SIZE * NUM_AXES];
                // Sao chep window dang sliding ra dang contig (read_idx tinh tu g_write_idx)
                for (int i = 0; i < WINDOW_SIZE; ++i) {
                    int src = ((g_write_idx + i) % WINDOW_SIZE) * NUM_AXES;
                    int dst = i * NUM_AXES;
                    for (int a = 0; a < NUM_AXES; ++a) work[dst + a] = g_window[src + a];
                }
                // KHONG z-score o day - inference::classify() tu xu ly tuy USE_TFLITE.

                inference::Result r;
                if (inference::classify(work, r)) {
                    g_last_result = r;
                    pdr::setActivity(r.cls);
                    PubMsg m{}; m.kind = PUB_ACTIVITY; m.act = r;
                    enqueuePub(m);

                    PubMsg p{}; p.kind = PUB_POSE; p.pdr_s = st;
                    enqueuePub(p);

                    // 2 path phu - chi publish khi pipeline full publish (1 Hz)
                    PubMsg pc{}; pc.kind = PUB_POSE_CALIB;
                    pc.alt.x = g_alt_calib.x_m;
                    pc.alt.y = g_alt_calib.y_m;
                    pc.alt.heading_deg = g_alt_calib.heading_deg;
                    enqueuePub(pc);

                    PubMsg pr{}; pr.kind = PUB_POSE_RAW;
                    pr.alt.x = g_alt_raw.x_m;
                    pr.alt.y = g_alt_raw.y_m;
                    pr.alt.heading_deg = g_alt_raw.heading_deg;
                    enqueuePub(pr);
                }
#endif
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

    // Task display OLED (core 0, priority thap)
    void displayTask(void*) {
        for (;;) {
            const char* name = ACTIVITY_NAMES[g_last_result.cls];
            display::update(name, g_last_result.confidence, g_last_state);
            vTaskDelay(pdMS_TO_TICKS(OLED_REFRESH_MS));
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
                    case PUB_ACTIVITY:   mqttc::publishActivity(m.act); break;
                    case PUB_STEP:       mqttc::publishStep(m.pdr_s);   break;
                    case PUB_POSE:       mqttc::publishPose(m.pdr_s);   break;
                    case PUB_POSE_CALIB: mqttc::publishPoseAt(MQTT_TOPIC_POSE_CALIB, m.alt.x, m.alt.y, m.alt.heading_deg); break;
                    case PUB_POSE_RAW:   mqttc::publishPoseAt(MQTT_TOPIC_POSE_RAW,   m.alt.x, m.alt.y, m.alt.heading_deg); break;
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

    i2c_bus::begin();   // mutex truoc khi co bat cu task nao cham vao Wire

    if (!imu::begin()) {
        Serial.println("[FATAL] IMU init failed - check wiring (SDA=21 SCL=22)");
    }

    calibration::Data cal;

    cal.acc_bias[0]  =  0.4922122367123288;
    cal.acc_bias[1]  =  0.3514496893150685;
    cal.acc_bias[2]  =  0.5198670405479452;
    cal.acc_scale[0] =  24.684351696952486;
    cal.acc_scale[1] =  134.5428105915762;
    cal.acc_scale[2] =  0.4990798737874918;
    cal.gyro_bias[0] =  -0.016543828493150683;
    cal.gyro_bias[1] =  0.07364579726027397;
    cal.gyro_bias[2] =  -0.01200866496080274;

    if (calibration::load(cal)) {
        Serial.println("[CAL] loaded from NVS");
    } else {
        Serial.println("[CAL] NVS trong");
#if CALIB_METHOD == 1
        // Teacher-style: tu chay stationary calib khi NVS trong
        if (calibration::stationaryCalib()) {
            calibration::load(cal);
        }
#elif CALIB_METHOD == 2
        Serial.println("[CAL] CALIB_METHOD=2 -> skip, dung default");
#else
        Serial.println("[CAL] CALIB_METHOD=0 -> manual mode. Hard-code calib trong main.cpp:");
        Serial.println("[CAL]   cal.acc_bias[]  = {...};   cal.acc_scale[] = {...};");
        Serial.println("[CAL]   cal.gyro_bias[] = {...};   calibration::save(cal);");
#endif
    }
    calibration::printSummary(cal);
    imu::applyCalibration(cal.acc_bias, cal.acc_scale, cal.gyro_bias);

    preprocess::initFilters();
    pdr::begin();
    inference::begin();

    g_pub_queue = xQueueCreate(64, sizeof(PubMsg));

#if MILESTONE_LEVEL <= 2
    Serial.println("[BRINGUP] MILESTONE_LEVEL <= 2 -> bypass inference, enable raw stream");
    mqttc::enableRawStream(true);
#endif

    // OLED khoi tao sau MPU vi dung cung Wire (MPU goi Wire.begin truoc)
    display::begin();

    xTaskCreatePinnedToCore(sensorTask, "sensor", 8192, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(mqttTask,   "mqtt",   8192, nullptr, 3, nullptr, 0);

    if (display::isReady()) {
        xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 2, nullptr, 0);
    }
}

void loop() {
    // Tat ca xu ly o 2 task FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
}
