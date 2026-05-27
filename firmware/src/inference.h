#pragma once
#include <Arduino.h>
#include "config.h"

namespace inference {
    struct Result {
        uint8_t cls;
        float confidence;
        float probs[ACT_NUM_CLASSES];
    };

    bool begin();
    // Window: [WINDOW_SIZE][NUM_AXES] o don vi THUC (m/s^2, rad/s), CHUA z-score.
    // classify() se tinh variance cho heuristic, va z-score (in-place) cho TFLM.
    bool classify(float* window, Result& out);
}
