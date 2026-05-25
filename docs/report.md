# Báo cáo đồ án: Hệ thống định vị dự phòng ESP32 + MPU6050 + TinyML

Sinh viên: <Họ tên>  
Mã SV: <MSSV>  
Môn: Cảm biến, USTH  
Ngày: <YYYY-MM-DD>

## 1. Đặt vấn đề
- Nhu cầu định vị dự phòng khi mất GPS (đi bộ trong nhà, dưới hầm…).
- Ràng buộc: chỉ có ESP32 + MPU6050 (6-axis, không magnetometer).
- Mục tiêu: nhận diện hoạt động (HAR) trên thiết bị + ước lượng quỹ đạo tương đối, hiển thị dashboard.

## 2. Cơ sở lý thuyết
### 2.1 MPU6050 và giới hạn 6-axis
- Tích phân kép gia tốc → vị trí drift bậc 2 thời gian.
- Heading từ gyro Z drift tuyến tính, ~6–15° / 5 phút khi không có magnetometer.
→ Không thể định vị tuyệt đối; phải dùng PDR tương đối + có chiến lược giảm drift.

### 2.2 PDR (Pedestrian Dead Reckoning)
- Step detection (peak detect trên gia tốc tuyến tính), Weinberg stride model.
- ZUPT khi đứng yên.
- Heading tích phân gyro Z + cập nhật bias EMA khi state = STAND.

### 2.3 HAR với 1D-CNN
- Cửa sổ 2s, overlap 50% → 1 inference/giây.
- Kiến trúc nhẹ ~10K params, int8 quantization → vừa ESP32 không PSRAM.

## 3. Thiết kế hệ thống
(Sơ đồ kiến trúc – xem README.md)

### 3.1 Pipeline tín hiệu trên ESP32
- Sampling 100 Hz, MPU6050 DLPF cutoff ~44 Hz chống aliasing.
- Low-pass Butterworth bậc 2 cutoff 10 Hz cho HAR.
- High-pass IIR cutoff 0.3 Hz tách gravity → linear accel.
- Z-score per-window per-axis trước khi đưa vào CNN.

### 3.2 Kiến trúc model
- Conv1D(16, k=9, s=2) → BN → MaxPool(2)
- Conv1D(32, k=5) → BN → MaxPool(2)
- Conv1D(32, k=3) → GAP → Dense(32) → Dropout(0.3) → Dense(4, softmax)

### 3.3 Server stack
- Mosquitto broker.
- Telegraf consume MQTT → InfluxDB.
- Grafana auto-provision dashboard time-series.
- Node-RED dashboard quỹ đạo 2D + nút reset.

## 4. Thu thập dữ liệu
- 3–5 người, 2–3 phiên khác ngày, 2 vị trí gắn (thắt lưng + túi quần).
- Mục tiêu **30–60 phút/class sau khi cửa sổ hóa**.
- Tool: `tools/collect_label.py` (gán nhãn realtime qua phím).
- Class: stand / walk / run / stairs.

## 5. Kết quả
| Metric | Train | Val |
|---|---|---|
| Accuracy | <điền sau> | <điền sau> |
| F1 macro | <điền sau> | <điền sau> |

Confusion matrix: (chèn ảnh)  
Inference time on-device: <ms>  
Tensor arena: <KB>  

### PDR closure error
- Đường thử: hình chữ nhật 10 m × 5 m, 3 vòng (90 m).
- Closure error trung bình: <m> ≈ <%> tổng quãng đường.

## 6. Demo
- Hình chụp Grafana, Node-RED.
- Link video: <điền sau>.

## 7. Hạn chế và hướng phát triển
- Không có magnetometer → drift heading nhanh. Đề xuất: thay MPU9250.
- Class fall detection bị bỏ do thiếu dữ liệu.
- Có thể tích hợp WiFi RSSI fingerprinting để định vị tuyệt đối.

## 8. Tài liệu tham khảo
- Weinberg, H. (2002). *Using the ADXL202 in pedometer and personal navigation applications*.
- Anguita, D. et al. (2013). *A Public Domain Dataset for Human Activity Recognition Using Smartphones*. ESANN.
- TensorFlow Lite for Microcontrollers – Pete Warden, Daniel Situnayake (O'Reilly).
