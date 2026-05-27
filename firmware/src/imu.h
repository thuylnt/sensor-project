#pragma once
#include <Arduino.h>

struct ImuSample {
    uint32_t ts_ms;
    float ax, ay, az;   // m/s^2, da tru bias va scale
    float gx, gy, gz;   // rad/s, da tru bias
};

namespace imu {
    bool begin();
    // Doc 1 mau. Tham so raw_out (tuy chon): neu khac nullptr, fill bang gia tri
    // CHUA AP CALIB (chi co don vi chuyen sang SI). Dung cho pipeline pose_raw.
    bool read(ImuSample& calibrated, ImuSample* raw_out = nullptr);
    void applyCalibration(const float acc_bias[3], const float acc_scale[3], const float gyro_bias[3]);
}
