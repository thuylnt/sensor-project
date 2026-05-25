#pragma once
#include "imu.h"
#include "config.h"

namespace pdr {
    struct State {
        uint32_t step_count;
        float cadence_spm;          // step per minute
        float heading_deg;          // 0..360, tich phan gyro Z
        float x_m, y_m;             // toa do tuong doi
        bool step_event;            // true neu sample hien tai vua phat hien step
        float last_stride_m;
    };

    void begin();
    void reset();
    void setActivity(uint8_t activity_class);   // dung de bat/tat ZUPT va step
    // Goi voi moi sample 100Hz (sau low-pass). Tra ve trang thai hien tai.
    const State& update(const ImuSample& s, float linear_accel_norm);
}
