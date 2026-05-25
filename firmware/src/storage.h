#pragma once
#include <Arduino.h>
#include <functional>

// Store-and-forward dung LittleFS tren flash internal ESP32.
// Khi MQTT khong ket noi, cac topic se duoc append vao /log.jsonl theo format:
//   <topic>\t<json_payload>\n
// Khi MQTT reconnect, ham drain() doc lai tung dong va replay qua callback.

namespace storage {
    bool begin();
    bool append(const char* topic, const char* payload, size_t len);

    using ReplayCb = std::function<bool(const char* topic, const char* payload, size_t len)>;
    // Goi cb cho moi record. Neu cb tra ve true (publish thanh cong),
    // record do duoc danh dau hoan thanh; sau khi xong toan bo, file bi xoa.
    // Tra ve so record da replay thanh cong, hoac -1 neu file khong ton tai.
    int drain(ReplayCb cb, int max_records = 0);

    size_t bytesUsed();
    size_t bytesTotal();
    int recordCount();
    void clearAll();
}
