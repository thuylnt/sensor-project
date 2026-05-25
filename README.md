# USTH Sensor Project – HAR + PDR-lite trên ESP32 + MPU6050

Đồ án môn học: hệ thống "định vị dự phòng" gồm Human Activity Recognition (HAR) chạy on-device (TinyML) + đếm bước + ước lượng quỹ đạo tương đối, đẩy dữ liệu lên Mosquitto → InfluxDB và hiển thị trên Grafana + Node-RED.

## Cấu trúc

```
firmware/   # Code ESP32 (PlatformIO + Arduino-ESP32)
tools/      # Script Python: thu data có nhãn, calib MPU6050, train CNN, export model
server/     # docker-compose: Mosquitto + InfluxDB + Telegraf + Grafana + Node-RED
docs/       # Báo cáo + tài liệu
```

## Quickstart

### 1. Bật server stack
```bash
cd server
docker compose up -d
```
- Grafana: http://localhost:3000 (admin/admin)
- Node-RED: http://localhost:1880
- InfluxDB: http://localhost:8086 (admin / usth-sensor-2026)

### 2. Sửa cấu hình firmware
Mở `firmware/src/config.h`, sửa:
- `WIFI_SSID`, `WIFI_PASSWORD`
- `MQTT_HOST` = IP laptop chạy docker-compose

### 3. Build & flash firmware
```bash
cd firmware
pio run -e esp32dev -t upload
pio device monitor -b 115200
```
Mặc định firmware dùng **heuristic placeholder** thay cho model thật để dashboard có data demo ngay khi flash. Sau khi train model:
- copy `tools/model/model_data.h` → `firmware/src/model_data.h`
- đặt `USE_TFLITE = 1` trong `inference.cpp`
- bỏ comment lib `TensorFlowLite_ESP32` trong `platformio.ini`
- flash lại

### 4. Calibrate MPU6050 (1 lần)
```bash
cd tools
pip install -r requirements.txt
mosquitto_pub -h <broker> -t usth/pdr/cmd/raw_on -m on
python calibrate_mpu.py --broker 127.0.0.1 --device dev01 --out calib.json
```

### 5. Thu data và train
Xem `tools/README.md`.

### 6. Demo
- Cầm ESP32, đứng yên/đi/chạy → Grafana hiện activity timeline, Node-RED hiện quỹ đạo 2D.
- Nút "Reset quỹ đạo" trên Node-RED dashboard publishes `usth/pdr/cmd/reset` → firmware reset PDR.

## Kiến trúc

```
[MPU6050] -- I2C 400k --> [ESP32 core1: sampling 100Hz, preprocess, inference, PDR]
                                      \_ FreeRTOS queue _/
                          [ESP32 core0: WiFi + MQTT publish]
                                            |
                                            v MQTT 1883
                            [Mosquitto] -> [Telegraf] -> [InfluxDB 2.x]
                                  |                              |
                                  v                              v
                            [Node-RED]                       [Grafana]
                        (quỹ đạo 2D, control)         (time-series chart)
```

## Topic MQTT

| Topic | Tần suất | Payload |
|---|---|---|
| `usth/pdr/<dev>/activity` | 1 Hz | `{ts, class, confidence}` |
| `usth/pdr/<dev>/step`     | mỗi step | `{ts, count, cadence, stride}` |
| `usth/pdr/<dev>/pose`     | 1 Hz | `{ts, x, y, heading_deg}` |
| `usth/pdr/<dev>/status`   | 0.1 Hz retained | `{ts, uptime_s, rssi, free_heap, raw_stream}` |
| `usth/pdr/<dev>/raw`      | 100 Hz (chỉ khi collect) | `{ts, ax,ay,az,gx,gy,gz}` |
| `usth/pdr/cmd/+`          | command in | `reset` / `raw_on` / `raw_off` / `calib_clear` |

## Tham khảo

Plan đầy đủ: `C:\Users\lenghiemthanhthuy\.claude\plans\t-i-c-n-l-m-1-velvet-lighthouse.md`
