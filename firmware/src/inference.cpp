#include "inference.h"
#include <math.h>

// Trong giai doan dau (chua co model), inference se dung 1 heuristic don gian
// (variance cua accel norm) de dashboard van co data demo. Khi co model.h tu
// Edge Impulse hoac TFLite Micro, doi nhanh USE_TFLITE = 1 va include model_data.h.

#define USE_TFLITE 0

#if USE_TFLITE
  // TODO: include "model_data.h" da export tu Edge Impulse / TFLM
  // #include <TensorFlowLite_ESP32.h>
  // ... setup interpreter, tensor arena ~ 30KB ...
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
// Heuristic placeholder: dung variance va RMS cua accel norm de doan stand/walk/run.
// CHI dung de dashboard demo truoc khi co model that. Khong dat do chinh xac san xuat.
bool classify(const float* window, Result& out) {
    constexpr int N = WINDOW_SIZE;
    constexpr int A = NUM_AXES;
    float mean_ax = 0, mean_ay = 0, mean_az = 0;
    float sq = 0;
    for (int i = 0; i < N; ++i) {
        // window da z-score -> norm bi mat scale vat ly. De tien, ta su dung variance
        // cua truc Z (z-scored): voi walking dao dong manh ca 3 truc -> tong variance lon.
        mean_ax += window[i * A + 0];
        mean_ay += window[i * A + 1];
        mean_az += window[i * A + 2];
    }
    mean_ax /= N; mean_ay /= N; mean_az /= N;
    for (int i = 0; i < N; ++i) {
        float dx = window[i * A + 0] - mean_ax;
        float dy = window[i * A + 1] - mean_ay;
        float dz = window[i * A + 2] - mean_az;
        sq += dx * dx + dy * dy + dz * dz;
    }
    float rms = sqrtf(sq / N);

    uint8_t cls;
    if      (rms < 0.4f) cls = ACT_STAND;
    else if (rms < 1.4f) cls = ACT_WALK;
    else if (rms < 2.2f) cls = ACT_STAIRS;
    else                 cls = ACT_RUN;

    for (int i = 0; i < ACT_NUM_CLASSES; ++i) out.probs[i] = 0.05f;
    out.probs[cls] = 0.85f;
    out.cls = cls;
    out.confidence = out.probs[cls];
    return true;
}
#else
bool classify(const float* window, Result& out) {
    // TODO: copy window vao input tensor, Invoke(), doc output, ap softmax
    return false;
}
#endif

} // namespace inference
