#include "i2c_bus.h"

namespace i2c_bus {

namespace {
    SemaphoreHandle_t g_mutex = nullptr;
}

void begin() {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
}

bool lock(uint32_t timeout_ms) {
    if (!g_mutex) begin();
    return xSemaphoreTake(g_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock() {
    if (g_mutex) xSemaphoreGive(g_mutex);
}

} // namespace i2c_bus
