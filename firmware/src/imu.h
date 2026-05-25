#pragma once
#include <Arduino.h>

struct ImuSample {
    uint32_t ts_ms;
    float ax, ay, az;   // m/s^2, da tru bias va scale
    float gx, gy, gz;   // rad/s, da tru bias
};

namespace imu {
    bool begin();
    bool read(ImuSample& s);
    void applyCalibration(const float acc_bias[3], const float acc_scale[3], const float gyro_bias[3]);
}
