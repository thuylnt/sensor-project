#pragma once
#include <Arduino.h>

namespace calibration {
    struct Data {
        float acc_bias[3];   // m/s^2
        float acc_scale[3];  // unitless, mac dinh 1.0
        float gyro_bias[3];  // rad/s
    };

    bool load(Data& out);                  // tra ve true neu da co calib trong NVS
    bool save(const Data& d);
    void clear();

    // In ra serial mot ban tom tat de tools/calibrate_mpu.py parse va doi chieu
    void printSummary(const Data& d);
}
