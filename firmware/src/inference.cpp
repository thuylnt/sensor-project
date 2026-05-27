#include "inference.h"
#include "preprocess.h"
#include <math.h>

// Trong giai doan dau (chua co model), inference dung heuristic don gian
// (variance cua accel) de dashboard van co data demo. Khi co model.h tu
// Edge Impulse hoac TFLite Micro, doi nhanh USE_TFLITE = 1 va include model_data.h.

#define USE_TFLITE 0

#if USE_TFLITE
  // TODO: include "model_data.h" da export tu Edge Impulse / TFLM
  // #include <TensorFlowLite_ESP32.h>
#endif

namespace inference {

bool begin() {
#if USE_TFLITE
    // TODO: khoi tao interpreter, allocate tensor arena
#endif
    Serial.println("[INF] inference module ready (heuristic fallback)");
    return true;
}

#if !USE_TFLITE
// Heuristic placeholder: phan loai dua tren variance cua accel.
// Input: window CHUA z-score (don vi m/s^2 thuc).
// CHI dung cho dashboard demo, khong dat do chinh xac san xuat.
bool classify(float* window, Result& out) {
    constexpr int N = WINDOW_SIZE;
    constexpr int A = NUM_AXES;

    // Tinh variance cua 3 truc accel (axes 0,1,2) tren window THUC (chua z-score)
    float mean[3] = {0, 0, 0};
    for (int a = 0; a < 3; ++a) {
        for (int i = 0; i < N; ++i) mean[a] += window[i * A + a];
        mean[a] /= N;
    }
    float var = 0;
    for (int a = 0; a < 3; ++a) {
        for (int i = 0; i < N; ++i) {
            float d = window[i * A + a] - mean[a];
            var += d * d;
        }
    }
    // RMS theo m/s^2 (tong variance 3 truc)
    float accel_rms = sqrtf(var / N);

    uint8_t cls;
    if      (accel_rms < 0.6f)  cls = ACT_STAND;
    else if (accel_rms < 3.0f)  cls = ACT_WALK;
    else if (accel_rms < 6.0f)  cls = ACT_STAIRS;
    else                        cls = ACT_RUN;

    for (int i = 0; i < ACT_NUM_CLASSES; ++i) out.probs[i] = 0.05f;
    out.probs[cls] = 0.85f;
    out.cls = cls;
    out.confidence = out.probs[cls];

#if USE_TFLITE
    // Sau day se z-score window de chuan bi cho TFLM model
    preprocess::zscoreWindow(window);
#endif
    return true;
}
#else
bool classify(float* window, Result& out) {
    // TODO: copy window vao input tensor, Invoke(), doc output, ap softmax
    // Truoc do, z-score:
    preprocess::zscoreWindow(window);
    return false;
}
#endif

} // namespace inference
