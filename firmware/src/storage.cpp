#include "storage.h"
#include <LittleFS.h>

namespace storage {

namespace {
    constexpr const char* LOG_PATH = "/log.jsonl";
    bool g_ready = false;

    bool ensureFs() {
        if (g_ready) return true;
        if (!LittleFS.begin(true)) {  // format on fail
            Serial.println("[FS] LittleFS mount FAIL");
            return false;
        }
        g_ready = true;
        Serial.printf("[FS] LittleFS OK used=%u total=%u\n",
                      (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
        return true;
    }
}

bool begin() { return ensureFs(); }

bool append(const char* topic, const char* payload, size_t len) {
    if (!ensureFs()) return false;
    // Bao ve flash khoi day 100%, ngung append khi con < 10KB
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < 10 * 1024) {
        Serial.println("[FS] full, drop record");
        return false;
    }
    File f = LittleFS.open(LOG_PATH, FILE_APPEND);
    if (!f) return false;
    f.write((const uint8_t*)topic, strlen(topic));
    f.write('\t');
    f.write((const uint8_t*)payload, len);
    f.write('\n');
    f.close();
    return true;
}

int drain(ReplayCb cb, int max_records) {
    if (!ensureFs()) return -1;
    if (!LittleFS.exists(LOG_PATH)) return 0;

    File f = LittleFS.open(LOG_PATH, FILE_READ);
    if (!f) return -1;

    int ok = 0;
    String line;
    while (f.available()) {
        line = f.readStringUntil('\n');
        if (line.isEmpty()) continue;
        int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        String topic = line.substring(0, tab);
        String payload = line.substring(tab + 1);
        if (!cb(topic.c_str(), payload.c_str(), payload.length())) break;
        ok++;
        if (max_records > 0 && ok >= max_records) break;
    }
    bool all_done = !f.available();
    f.close();

    // Neu replay het toan bo file thi xoa cho gon.
    // Neu chua het (vi giao thuc roi giua chung) thi giu nguyen file de
    // lan sau tiep tuc - se replay lai cac record da OK. Co the chap nhan duplicate
    // hoac viet co chi muc offset; o day uu tien don gian.
    if (all_done) {
        LittleFS.remove(LOG_PATH);
        Serial.printf("[FS] drained %d records, log cleared\n", ok);
    } else {
        Serial.printf("[FS] partial drain %d, file giu lai\n", ok);
    }
    return ok;
}

size_t bytesUsed() { return ensureFs() ? LittleFS.usedBytes() : 0; }
size_t bytesTotal() { return ensureFs() ? LittleFS.totalBytes() : 0; }

int recordCount() {
    if (!ensureFs() || !LittleFS.exists(LOG_PATH)) return 0;
    File f = LittleFS.open(LOG_PATH, FILE_READ);
    if (!f) return 0;
    int n = 0;
    while (f.available()) {
        if (f.read() == '\n') n++;
    }
    f.close();
    return n;
}

void clearAll() {
    if (!ensureFs()) return;
    if (LittleFS.exists(LOG_PATH)) LittleFS.remove(LOG_PATH);
}

} // namespace storage
