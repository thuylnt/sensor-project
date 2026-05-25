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
    // Window: [WINDOW_SIZE][NUM_AXES] da z-score. Tra ve true neu inference thanh cong.
    bool classify(const float* window, Result& out);
}
