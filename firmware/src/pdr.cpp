#include "pdr.h"
#include <math.h>

namespace pdr {

namespace {
    State g_state{};
    uint32_t g_last_step_ms = 0;
    uint32_t g_last_reset_ms = 0;
    uint8_t  g_activity = ACT_STAND;

    // Tich luy cho stride length Weinberg (a_max - a_min) trong cua so giua 2 step
    float g_a_max = -1e9f, g_a_min = 1e9f;

    // EMA bias gyro Z khi dung yen
    float g_gz_bias_ema = 0.0f;
    constexpr float EMA_ALPHA = 0.01f;

    // Theo doi accel norm de bat peak
    float g_prev_norm = 0.0f;
    bool g_rising = false;
    float g_peak = 0.0f;

    constexpr float RAD2DEG = 57.29577951308232f;
}

void begin() { reset(); g_last_reset_ms = millis(); }

void reset() {
    g_state = State{};
    g_state.heading_deg = 0;
    g_a_max = -1e9f; g_a_min = 1e9f;
    g_prev_norm = 0; g_rising = false; g_peak = 0;
    g_last_step_ms = 0;
}

void setActivity(uint8_t activity_class) { g_activity = activity_class; }

const State& update(const ImuSample& s, float linear_accel_norm) {
    const float dt = 1.0f / (float)SAMPLE_RATE_HZ;

    // 1) Cap nhat gyro Z bias khi STAND (ZUPT-like)
    if (g_activity == ACT_STAND) {
        g_gz_bias_ema = (1 - EMA_ALPHA) * g_gz_bias_ema + EMA_ALPHA * s.gz;
    }
    float gz_corr = s.gz - g_gz_bias_ema;

    // 2) Tich phan heading theo dt
    g_state.heading_deg += gz_corr * RAD2DEG * dt;
    while (g_state.heading_deg < 0)    g_state.heading_deg += 360.0f;
    while (g_state.heading_deg >= 360) g_state.heading_deg -= 360.0f;

    g_state.step_event = false;

    // 3) Step detection: chi khi WALK / RUN / STAIRS
    bool can_step = (g_activity == ACT_WALK || g_activity == ACT_RUN || g_activity == ACT_STAIRS);
    if (can_step) {
        float n = linear_accel_norm;
        if (n > g_a_max) g_a_max = n;
        if (n < g_a_min) g_a_min = n;

        // Rising edge
        if (!g_rising && n > g_prev_norm && n > STEP_ACCEL_THRESHOLD) {
            g_rising = true;
            g_peak = n;
        } else if (g_rising) {
            if (n > g_peak) g_peak = n;
            // Falling edge -> ket thuc peak
            if (n < g_prev_norm) {
                uint32_t now = millis();
                if (now - g_last_step_ms >= STEP_MIN_INTERVAL_MS) {
                    // Weinberg stride
                    float amp = g_a_max - g_a_min;
                    if (amp < 0.1f) amp = 0.1f;
                    float stride = WEINBERG_K * powf(amp, 0.25f);

                    // Forward step theo heading
                    float h_rad = g_state.heading_deg / RAD2DEG;
                    g_state.x_m += stride * sinf(h_rad);
                    g_state.y_m += stride * cosf(h_rad);
                    g_state.step_count += 1;
                    g_state.last_stride_m = stride;
                    g_state.step_event = true;

                    if (g_last_step_ms != 0) {
                        float dt_step = (now - g_last_step_ms) / 1000.0f;
                        if (dt_step > 0) g_state.cadence_spm = 60.0f / dt_step;
                    }
                    g_last_step_ms = now;

                    // Reset cua so Weinberg cho step ke tiep
                    g_a_max = -1e9f; g_a_min = 1e9f;
                }
                g_rising = false;
                g_peak = 0;
            }
        }
        g_prev_norm = n;
    } else {
        // Khong di -> reset trang thai peak de tranh bat nham
        g_rising = false; g_peak = 0; g_prev_norm = linear_accel_norm;
        g_a_max = -1e9f; g_a_min = 1e9f;
    }

    // 4) Auto-reset quy dao sau N phut de tranh drift hien thi
    if (HEADING_RESET_AFTER_MS > 0 && (millis() - g_last_reset_ms) > HEADING_RESET_AFTER_MS) {
        reset();
        g_last_reset_ms = millis();
    }

    return g_state;
}

} // namespace pdr
