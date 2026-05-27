#include "calibration.h"
#include "config.h"
#include "imu.h"
#include <Preferences.h>

namespace calibration {

namespace {
    Preferences prefs;
}

bool load(Data& out) {
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    size_t n1 = prefs.getBytes(NVS_KEY_ACC_BIAS, out.acc_bias, sizeof(out.acc_bias));
    size_t n2 = prefs.getBytes(NVS_KEY_ACC_SCALE, out.acc_scale, sizeof(out.acc_scale));
    size_t n3 = prefs.getBytes(NVS_KEY_GYRO_BIAS, out.gyro_bias, sizeof(out.gyro_bias));
    prefs.end();
    bool ok = (n1 == sizeof(out.acc_bias) && n2 == sizeof(out.acc_scale) && n3 == sizeof(out.gyro_bias));
    if (!ok) {
        for (int i = 0; i < 3; ++i) { out.acc_bias[i] = 0; out.acc_scale[i] = 1; out.gyro_bias[i] = 0; }
    }
    return ok;
}

bool save(const Data& d) {
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    prefs.putBytes(NVS_KEY_ACC_BIAS, d.acc_bias, sizeof(d.acc_bias));
    prefs.putBytes(NVS_KEY_ACC_SCALE, d.acc_scale, sizeof(d.acc_scale));
    prefs.putBytes(NVS_KEY_GYRO_BIAS, d.gyro_bias, sizeof(d.gyro_bias));
    prefs.end();
    return true;
}

void clear() {
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.clear();
    prefs.end();
}

void printSummary(const Data& d) {
    Serial.printf("[CAL] acc_bias  = [%.4f %.4f %.4f] m/s^2\n", d.acc_bias[0], d.acc_bias[1], d.acc_bias[2]);
    Serial.printf("[CAL] acc_scale = [%.4f %.4f %.4f]\n", d.acc_scale[0], d.acc_scale[1], d.acc_scale[2]);
    Serial.printf("[CAL] gyro_bias = [%.6f %.6f %.6f] rad/s\n", d.gyro_bias[0], d.gyro_bias[1], d.gyro_bias[2]);
}

bool stationaryCalib(int n_samples) {
    Serial.println("\n[CAL] === TEACHER-STYLE STATIONARY CALIBRATION ===");
    Serial.println("[CAL] Dat ESP32 nam YEN tren mat phang, mat chip huong LEN.");
    Serial.println("[CAL] Bat dau sau 3 giay...");
    for (int i = 3; i > 0; --i) { Serial.printf("[CAL] %d...\n", i); delay(1000); }

    // Tat ap calib hien tai de doc gia tri raw thuan tuy
    float zero[3] = {0, 0, 0};
    float one[3]  = {1, 1, 1};
    imu::applyCalibration(zero, one, zero);

    Serial.printf("[CAL] Sampling %d readings (~%.1f giay)...\n",
                  n_samples, (float)n_samples / SAMPLE_RATE_HZ);
    double sa[3] = {0, 0, 0}, sg[3] = {0, 0, 0};
    ImuSample s;
    int got = 0;
    uint32_t t0 = millis();
    while (got < n_samples && millis() - t0 < 20000) {
        if (imu::read(s)) {
            sa[0] += s.ax; sa[1] += s.ay; sa[2] += s.az;
            sg[0] += s.gx; sg[1] += s.gy; sg[2] += s.gz;
            got++;
        }
        delay(2);
    }
    if (got == 0) {
        Serial.println("[CAL] ERR: khong doc duoc IMU - huy");
        return false;
    }

    Data d{};
    d.acc_bias[0]  = (float)(sa[0] / got);
    d.acc_bias[1]  = (float)(sa[1] / got);
    d.acc_bias[2]  = (float)(sa[2] / got) - GRAVITY_MS2;  // Z up => +1g khi yen
    d.acc_scale[0] = d.acc_scale[1] = d.acc_scale[2] = 1.0f;
    d.gyro_bias[0] = (float)(sg[0] / got);
    d.gyro_bias[1] = (float)(sg[1] / got);
    d.gyro_bias[2] = (float)(sg[2] / got);

    Serial.printf("[CAL] Da lay %d mau.\n", got);
    printSummary(d);
    if (!save(d)) {
        Serial.println("[CAL] ERR: save NVS FAIL");
        return false;
    }
    imu::applyCalibration(d.acc_bias, d.acc_scale, d.gyro_bias);
    Serial.println("[CAL] Da luu NVS + ap dung. Lan sau khong can chay lai.");
    return true;
}

} // namespace calibration
