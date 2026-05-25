#include "preprocess.h"
#include <math.h>

namespace preprocess {

float Biquad::step(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}
void Biquad::reset() { z1 = z2 = 0; }

namespace {
    // 6 biquad LPF (mot cho moi truc), 3 biquad HPF cho accel (tach gravity)
    Biquad g_lpf[6];
    Biquad g_hpf[3];

    // Tinh he so Butterworth bac 2 dang biquad cho fs/fc cho truoc.
    // Tham khao: Robert Bristow-Johnson Audio EQ Cookbook.
    void designLowpass(Biquad& b, float fs, float fc) {
        float w0 = 2.0f * (float)M_PI * fc / fs;
        float cosw = cosf(w0), sinw = sinf(w0);
        float Q = 0.7071f;
        float alpha = sinw / (2.0f * Q);
        float a0 = 1.0f + alpha;
        b.b0 = ((1.0f - cosw) * 0.5f) / a0;
        b.b1 = (1.0f - cosw) / a0;
        b.b2 = b.b0;
        b.a1 = (-2.0f * cosw) / a0;
        b.a2 = (1.0f - alpha) / a0;
        b.reset();
    }
    void designHighpass(Biquad& b, float fs, float fc) {
        float w0 = 2.0f * (float)M_PI * fc / fs;
        float cosw = cosf(w0), sinw = sinf(w0);
        float Q = 0.7071f;
        float alpha = sinw / (2.0f * Q);
        float a0 = 1.0f + alpha;
        b.b0 = ((1.0f + cosw) * 0.5f) / a0;
        b.b1 = -(1.0f + cosw) / a0;
        b.b2 = b.b0;
        b.a1 = (-2.0f * cosw) / a0;
        b.a2 = (1.0f - alpha) / a0;
        b.reset();
    }

    // Ghi nho gravity vector da tach ra (de tinh linear accel)
    float g_gravity[3] = {0, 0, GRAVITY_MS2};
}

void initFilters() {
    for (int i = 0; i < 6; ++i) designLowpass(g_lpf[i], (float)SAMPLE_RATE_HZ, LPF_CUTOFF_HZ);
    for (int i = 0; i < 3; ++i) designHighpass(g_hpf[i], (float)SAMPLE_RATE_HZ, HPF_CUTOFF_HZ);
}

void applyLowPass(ImuSample& s) {
    s.ax = g_lpf[0].step(s.ax);
    s.ay = g_lpf[1].step(s.ay);
    s.az = g_lpf[2].step(s.az);
    s.gx = g_lpf[3].step(s.gx);
    s.gy = g_lpf[4].step(s.gy);
    s.gz = g_lpf[5].step(s.gz);
}

void applyGravityHPF(ImuSample& s) {
    // HPF cua accel = linear accel; (1 - HPF) cua accel = gravity.
    // Ta luu gravity de step detector dung lai.
    float lin_ax = g_hpf[0].step(s.ax);
    float lin_ay = g_hpf[1].step(s.ay);
    float lin_az = g_hpf[2].step(s.az);
    g_gravity[0] = s.ax - lin_ax;
    g_gravity[1] = s.ay - lin_ay;
    g_gravity[2] = s.az - lin_az;
    // Khong ghi de s.ax,ay,az - vi CNN co the muon ca gravity. Step detector goi accelNormLinear() rieng.
}

void zscoreWindow(float* window) {
    constexpr int N = WINDOW_SIZE;
    constexpr int A = NUM_AXES;
    for (int a = 0; a < A; ++a) {
        float mean = 0, sq = 0;
        for (int i = 0; i < N; ++i) mean += window[i * A + a];
        mean /= N;
        for (int i = 0; i < N; ++i) { float d = window[i * A + a] - mean; sq += d * d; }
        float std = sqrtf(sq / N) + 1e-6f;
        for (int i = 0; i < N; ++i) window[i * A + a] = (window[i * A + a] - mean) / std;
    }
}

float accelNormLinear(const ImuSample& s) {
    float lx = s.ax - g_gravity[0];
    float ly = s.ay - g_gravity[1];
    float lz = s.az - g_gravity[2];
    return sqrtf(lx * lx + ly * ly + lz * lz);
}

} // namespace preprocess
