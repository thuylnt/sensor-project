#pragma once
#include <Arduino.h>

namespace calibration {
    // Default-construct = identity (bias 0, scale 1). Dung de detect "empty hardcode".
    struct Data {
        float acc_bias[3]  = {0.0f, 0.0f, 0.0f};
        float acc_scale[3] = {1.0f, 1.0f, 1.0f};
        float gyro_bias[3] = {0.0f, 0.0f, 0.0f};
    };

    bool load(Data& out);                  // tra ve true neu da co calib trong NVS
    bool save(const Data& d);
    void clear();

    // True neu d giong gia tri "identity" (chua duoc fill bat ky truong nao).
    // Dung de fallback NVS khi hardcode trong main.cpp bi bo trong.
    bool isDefault(const Data& d);

    // In ra serial mot ban tom tat de tools/calibrate_mpu.py parse va doi chieu
    void printSummary(const Data& d);

    // === TEACHER-STYLE STATIONARY CALIBRATION ===
    // Code goc cua thay: MPU6050_light::calcOffsets().
    // Gia dinh thiet bi nam YEN tren mat phang voi truc Z huong len.
    // Lay trung binh N mau lam bias. Scale luon = 1.0 (khong tinh).
    // Bias Z accel = mean(az) - g  (vi luc yen z phai bang 1g).
    //
    // Kem hon 6-face calib o cho:
    //   - Khong calib scale -> sai so do chinh xac cua chip khong loai bo
    //   - Phu thuoc tu the (Z phai up) -> calib sai neu lat sai mat
    // Loi hon:
    //   - Khong can lat 6 lan
    //   - Tu dong, khong can input nguoi
    //
    // GOI TRUOC khi tao FreeRTOS task (vi doc IMU truc tiep).
    // Tra ve true neu thanh cong + save NVS.
    bool stationaryCalib(int n_samples = 1000);
}
