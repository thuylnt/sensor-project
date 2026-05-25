# Tools

## Cài đặt

```bash
python -m venv .venv
.venv\Scripts\activate          # PowerShell
pip install -r requirements.txt
```

## Quy trình

```
[ESP32] --raw via MQTT--> collect_label.py --> CSV
                                                 |
                                                 v
                                            train_har.py
                                                 |
                                                 v
                                         model_data.h --> firmware
```

## simulate_device.py — chạy demo không cần ESP32

Khi chưa ráp/flash thiết bị, dùng tool này giả lập 1 ESP32 ảo publish đúng các topic của firmware thật. Mở Node-RED → http://localhost:1880/ui và sẽ thấy quỹ đạo, activity, step count chạy như thật.

```bash
# Khởi động Mosquitto + Node-RED nếu chưa
docker compose up -d mosquitto nodered

# Chạy giả lập đi vòng hình chữ nhật 10x5m
python simulate_device.py --broker 127.0.0.1

# Đổi sang đi đường thẳng
python simulate_device.py --pattern line

# Random walk
python simulate_device.py --pattern random

# Thêm raw 100Hz để test phần collect_label.py / Telegraf
python simulate_device.py --raw

# Giả 2 thiết bị song song (mở 2 terminal)
python simulate_device.py --device dev01 --pattern square
python simulate_device.py --device dev02 --pattern random
```

Activity sẽ tự đổi theo chu kỳ stand → walk → run → walk → stairs (xem trên Node-RED). Bấm `Ctrl+C` để dừng.

### 1. Calibration (1 lần, sau khi ráp mạch)

```bash
mosquitto_pub -h <broker> -t usth/pdr/cmd/raw_on -m on
python calibrate_mpu.py --broker 127.0.0.1 --device dev01 --out calib.json
```

Làm theo hướng dẫn trên màn hình (lật ESP32 6 mặt).

> **TODO**: chưa có đường nạp calib qua MQTT vào NVS. Hiện tại cần copy thủ công vào `firmware/src/main.cpp` hoặc gửi qua serial. Có thể bổ sung trong giai đoạn polish.

### 2. Thu data

```bash
mosquitto_pub -h <broker> -t usth/pdr/cmd/raw_on -m on
python collect_label.py --broker 127.0.0.1 --device dev01 --out data/sess1.csv
```

Bấm `1/2/3/4` để gán nhãn realtime, `SPACE` để pause, `q` để dừng.

### 3. Train

```bash
python train_har.py --data "data/*.csv" --out model/ --epochs 40
```

Sau khi xong, copy `model/model_data.h` vào `firmware/src/model_data.h`, đổi `USE_TFLITE = 1` trong `inference.cpp`, thêm lib `tensorflow/TensorFlowLite_ESP32` vào `platformio.ini`, rồi flash lại.
