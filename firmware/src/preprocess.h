#pragma once
#include "imu.h"
#include "config.h"

namespace preprocess {
    // IIR Butterworth bac 2 (1 truc). Co biquad cho moi truc.
    struct Biquad {
        float b0, b1, b2, a1, a2;
        float z1, z2;
        float step(float x);
        void reset();
    };

    void initFilters();
    void applyLowPass(ImuSample& s);     // chinh sua tai cho cua s.ax..gz
    void applyGravityHPF(ImuSample& s);  // tach gravity (chi accel)

    // Z-score per axis tren toan window [WINDOW_SIZE][NUM_AXES]
    // out[i*NUM_AXES + a] = (window[i*NUM_AXES + a] - mean_a) / (std_a + eps)
    void zscoreWindow(float* window);

    // Tinh accel norm da bo gravity, dung cho step detection
    float accelNormLinear(const ImuSample& s);
}
