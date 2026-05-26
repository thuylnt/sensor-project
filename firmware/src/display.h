#pragma once
#include "config.h"
#include "pdr.h"

// Hien thi trang thai he thong len OLED SSD1306 128x64.
// Layout (font size 1, 6x8 px):
//   Line 0:  ACT walk  0.92
//   Line 1:  Steps 1234  Cad 108
//   Line 2:  X  12.3  Y -5.7
//   Line 3:  Hdg 245 deg
//   Line 4:  WiFi -55 dBm
//   Line 5:  Up 0:12:34

namespace display {
    bool begin();   // tra ve false neu OLED_ENABLED=0 hoac OLED khong response
    void update(const char* activity_name, float confidence, const pdr::State& pdr);
    bool isReady();
}
