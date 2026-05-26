#include "display.h"
#include "i2c_bus.h"
#include <Wire.h>
#include <WiFi.h>

#if OLED_ENABLED
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#endif

namespace display {

#if OLED_ENABLED

namespace {
    Adafruit_SSD1306 g_oled(128, 64, &Wire, -1);   // -1 = khong dung pin RESET
    bool g_ready = false;

    void println_kv(const char* fmt, ...) {
        char buf[32];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_oled.println(buf);
    }
}

bool begin() {
    if (!i2c_bus::lock(200)) {
        Serial.println("[OLED] cannot acquire i2c mutex");
        return false;
    }
    bool ok = g_oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (ok) {
        g_oled.clearDisplay();
        g_oled.setTextSize(1);
        g_oled.setTextColor(SSD1306_WHITE);
        g_oled.setCursor(0, 0);
        g_oled.println("USTH PDR");
        g_oled.println("Booting...");
        g_oled.display();
    }
    i2c_bus::unlock();
    if (!ok) {
        Serial.printf("[OLED] SSD1306 init FAIL @ 0x%02x\n", OLED_I2C_ADDR);
        return false;
    }
    g_ready = true;
    Serial.println("[OLED] SSD1306 ready");
    return true;
}

bool isReady() { return g_ready; }

void update(const char* activity_name, float confidence, const pdr::State& pdr) {
    if (!g_ready) return;
    if (!i2c_bus::lock(50)) return;   // skip frame neu sensor task dang giu

    g_oled.clearDisplay();
    g_oled.setCursor(0, 0);

    // Line 0: ACT
    println_kv("ACT %-6s %.2f", activity_name, confidence);

    // Line 1: Steps + cadence
    println_kv("Steps %-4lu Cad %3.0f", (unsigned long)pdr.step_count, pdr.cadence_spm);

    // Line 2: position
    println_kv("X %5.1f Y %5.1f", pdr.x_m, pdr.y_m);

    // Line 3: heading
    println_kv("Hdg %5.1f deg", pdr.heading_deg);

    // Line 4: WiFi
    if (WiFi.status() == WL_CONNECTED) {
        println_kv("WiFi %-4d dBm", (int)WiFi.RSSI());
    } else {
        g_oled.println("WiFi --");
    }

    // Line 5: uptime
    uint32_t up = millis() / 1000;
    println_kv("Up %lu:%02lu:%02lu",
               (unsigned long)(up / 3600),
               (unsigned long)((up % 3600) / 60),
               (unsigned long)(up % 60));

    g_oled.display();
    i2c_bus::unlock();
}

#else // OLED_ENABLED == 0

bool begin() { Serial.println("[OLED] disabled in config"); return false; }
bool isReady() { return false; }
void update(const char*, float, const pdr::State&) {}

#endif

} // namespace display
