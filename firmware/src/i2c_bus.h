#pragma once
#include <Arduino.h>

// Mutex chia se bus I2C giua nhieu task FreeRTOS.
// MPU6050 va SSD1306 OLED dung chung Wire -> can serialize transaction.

namespace i2c_bus {
    void begin();                                    // tao mutex (idempotent)
    bool lock(uint32_t timeout_ms = 100);            // tra ve false neu timeout
    void unlock();
}
