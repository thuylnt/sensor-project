#pragma once

// =======================
// Cau hinh chung
// =======================

// === Bring-up mode (xem docs/Step-by-step.txt) ===
//   1 = simulator only (khong build firmware, dung simulate_device.py)
//   2 = device gui raw + activity gia "walk", KHONG inference, KHONG step thuc
//       -> dung de verify I2C + WiFi + MQTT + dashboard hien data
//   3 = full pipeline voi heuristic inference (variance-based, khong can model file)
//   4 = full pipeline voi TFLite int8 model that (USE_TFLITE=1 trong inference.cpp)
#define MILESTONE_LEVEL 3

// === Device ID xuat hien trong MQTT topic usth/pdr/<DEVICE_ID>/... ===
#define DEVICE_ID "dev01"

// === OLED SSD1306 128x64 tren cung I2C bus voi MPU6050 ===
// 1 = hien thi thong tin len OLED (~10% CPU core0, ~10% sample drop @ 100Hz)
// 0 = tat hoan toan, khong khoi tao OLED, khong tao display task
#define OLED_ENABLED 1
#define OLED_I2C_ADDR 0x3C
#define OLED_REFRESH_MS 250        // 4 Hz

// === WiFi ===
// Sua truoc khi flash. Khong commit credentials that vao git.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// === MQTT broker ===
#define MQTT_HOST "192.168.1.100"   // IP laptop chay docker-compose
#define MQTT_PORT 1883
#define MQTT_KEEPALIVE 30

// === Sampling ===
#define SAMPLE_RATE_HZ 100
#define WINDOW_SIZE 200             // 2s @ 100Hz
#define WINDOW_HOP 100              // 50% overlap -> 1 inference / 1s
#define NUM_AXES 6                  // ax, ay, az, gx, gy, gz

// === Tien xu ly ===
#define LPF_CUTOFF_HZ 10.0f         // low-pass cho HAR
#define HPF_CUTOFF_HZ 0.3f          // high-pass tach gravity
#define GRAVITY_MS2 9.80665f

// === HAR classes ===
enum ActivityClass : uint8_t {
    ACT_STAND = 0,
    ACT_WALK  = 1,
    ACT_RUN   = 2,
    ACT_STAIRS = 3,
    ACT_NUM_CLASSES = 4
};
static const char* const ACTIVITY_NAMES[ACT_NUM_CLASSES] = {"stand", "walk", "run", "stairs"};

// === PDR ===
#define STEP_MIN_INTERVAL_MS 250       // ngan double-count
#define STEP_ACCEL_THRESHOLD 1.2f      // m/s^2 tren accel norm da loc
#define WEINBERG_K 0.41f               // stride = K * (a_max - a_min)^(1/4)
#define HEADING_RESET_AFTER_MS 180000  // auto reset 3 phut

// === Calibration NVS keys ===
#define NVS_NAMESPACE "calib"
#define NVS_KEY_ACC_BIAS "acc_bias"
#define NVS_KEY_ACC_SCALE "acc_scale"
#define NVS_KEY_GYRO_BIAS "gyro_bias"

// === MQTT topic builder ===
#define MQTT_TOPIC_BASE "usth/pdr/" DEVICE_ID
#define MQTT_TOPIC_ACTIVITY MQTT_TOPIC_BASE "/activity"
#define MQTT_TOPIC_STEP     MQTT_TOPIC_BASE "/step"
#define MQTT_TOPIC_POSE     MQTT_TOPIC_BASE "/pose"
#define MQTT_TOPIC_STATUS   MQTT_TOPIC_BASE "/status"
#define MQTT_TOPIC_RAW      MQTT_TOPIC_BASE "/raw"
#define MQTT_TOPIC_CMD      "usth/pdr/cmd/+"
