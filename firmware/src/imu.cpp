#include "imu.h"
#include "config.h"
#include "i2c_bus.h"
#include <Wire.h>
#include <MPU6050.h>

namespace {
    MPU6050 mpu;
    float g_acc_bias[3]   = {0, 0, 0};
    float g_acc_scale[3]  = {1, 1, 1};
    float g_gyro_bias[3]  = {0, 0, 0};

    // MPU6050 default LSB/g va LSB/dps voi range +/-2g, +/-250dps:
    constexpr float ACC_LSB_PER_G = 16384.0f;
    constexpr float GYRO_LSB_PER_DPS = 131.0f;
    constexpr float DEG2RAD = 0.017453292519943295f;
}

namespace imu {

bool begin() {
    Wire.begin();
    Wire.setClock(400000);
    mpu.initialize();
    if (!mpu.testConnection()) {
        Serial.println("[IMU] MPU6050 connection FAILED");
        return false;
    }
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
    mpu.setDLPFMode(3);                 // ~44 Hz accel, 42 Hz gyro -> chong alias @ 100Hz
    mpu.setRate(9);                     // 1kHz / (1+9) = 100Hz
    Serial.println("[IMU] MPU6050 OK");
    return true;
}

void applyCalibration(const float acc_bias[3], const float acc_scale[3], const float gyro_bias[3]) {
    for (int i = 0; i < 3; ++i) {
        g_acc_bias[i]  = acc_bias[i];
        g_acc_scale[i] = acc_scale[i] != 0 ? acc_scale[i] : 1.0f;
        g_gyro_bias[i] = gyro_bias[i];
    }
}

bool read(ImuSample& s, ImuSample* raw_out) {
    int16_t ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    if (!i2c_bus::lock(20)) return false;   // OLED dang refresh, bo qua sample nay
    mpu.getMotion6(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw, &gz_raw);
    i2c_bus::unlock();

    float ax = (ax_raw / ACC_LSB_PER_G) * GRAVITY_MS2;
    float ay = (ay_raw / ACC_LSB_PER_G) * GRAVITY_MS2;
    float az = (az_raw / ACC_LSB_PER_G) * GRAVITY_MS2;
    float gx = (gx_raw / GYRO_LSB_PER_DPS) * DEG2RAD;
    float gy = (gy_raw / GYRO_LSB_PER_DPS) * DEG2RAD;
    float gz = (gz_raw / GYRO_LSB_PER_DPS) * DEG2RAD;

    uint32_t now = millis();
    if (raw_out) {
        raw_out->ts_ms = now;
        raw_out->ax = ax; raw_out->ay = ay; raw_out->az = az;
        raw_out->gx = gx; raw_out->gy = gy; raw_out->gz = gz;
    }

    // ap calibration
    s.ax = (ax - g_acc_bias[0]) * g_acc_scale[0];
    s.ay = (ay - g_acc_bias[1]) * g_acc_scale[1];
    s.az = (az - g_acc_bias[2]) * g_acc_scale[2];
    s.gx = gx - g_gyro_bias[0];
    s.gy = gy - g_gyro_bias[1];
    s.gz = gz - g_gyro_bias[2];
    s.ts_ms = now;
    return true;
}

} // namespace imu
