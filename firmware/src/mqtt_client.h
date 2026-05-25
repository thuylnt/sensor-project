#pragma once
#include <Arduino.h>
#include "pdr.h"
#include "inference.h"

namespace mqttc {
    using CmdHandler = void (*)(const char* topic, const char* payload);

    bool begin(CmdHandler cmd_cb);
    void loop();
    bool isConnected();

    void publishActivity(const inference::Result& r);
    void publishStep(const pdr::State& s);
    void publishPose(const pdr::State& s);
    void publishStatus();
    void publishRaw(uint32_t ts_ms, float ax, float ay, float az, float gx, float gy, float gz);

    void enableRawStream(bool on);
    bool rawStreamEnabled();
}
