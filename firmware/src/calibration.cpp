#include "calibration.h"
#include "config.h"
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

} // namespace calibration
