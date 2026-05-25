"""
Tinh bias accel/gyro va scale accel cho MPU6050.
Quy trinh:
  - Sample dat ESP32 lan luot tren 6 mat: +X up, -X up, +Y up, -Y up, +Z up, -Z up
  - Mat moi mat dat yen 5 giay; script sa thu data tu MQTT topic /raw
  - Tinh: bias accel = trung binh 6 mat
          scale accel = sao cho ||accel sau sua|| = g
          bias gyro = trung binh chung khi yen
Sau khi co ket qua, in ra serial command de ESP32 nhan va luu vao NVS, hoac in
file calibration.json de nguoi dung tu copy ra dau muon dung.

Chay:
  python calibrate_mpu.py --broker 127.0.0.1 --device dev01 --out calib.json
"""
import argparse
import json
import statistics
import sys
import time
from collections import defaultdict

import paho.mqtt.client as mqtt

ORIENTATIONS = ["+X up", "-X up", "+Y up", "-Y up", "+Z up", "-Z up"]
SAMPLES_PER_FACE = 300   # ~3s @ 100Hz
GRAVITY = 9.80665


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--broker", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="dev01")
    ap.add_argument("--out", default="calib.json")
    args = ap.parse_args()

    buf = defaultdict(list)
    current = {"face": None}

    def on_connect(c, u, f, rc, p):
        c.subscribe(f"usth/pdr/{args.device}/raw")
        print("[MQTT] subscribed raw stream")

    def on_message(c, u, m):
        face = current["face"]
        if face is None:
            return
        try:
            d = json.loads(m.payload)
            buf[face].append((d["ax"], d["ay"], d["az"], d["gx"], d["gy"], d["gz"]))
        except Exception:
            pass

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="calib")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, 30)
    client.loop_start()

    print("Dam bao da bat raw stream:  mosquitto_pub -h", args.broker,
          "-t usth/pdr/cmd/raw_on -m on")
    input("Nhan Enter de bat dau...")

    for face in ORIENTATIONS:
        input(f"\n>> Dat ESP32 voi '{face}', giu yen, roi nhan Enter...")
        current["face"] = face
        time.sleep(0.5)
        buf[face].clear()
        while len(buf[face]) < SAMPLES_PER_FACE:
            time.sleep(0.1)
            print(f"  thu duoc {len(buf[face])}/{SAMPLES_PER_FACE}", end="\r")
        current["face"] = None
        print(f"  OK ({len(buf[face])} mau)            ")

    client.loop_stop()
    client.disconnect()

    # Tinh accel bias = trung binh tren tat ca 6 mat (gravity bi triet tieu vi 6 mat
    # chia deu cho 3 truc).
    all_a = [(ax, ay, az) for face in ORIENTATIONS for (ax, ay, az, *_g) in buf[face]]
    acc_bias = [statistics.mean(v[i] for v in all_a) for i in range(3)]
    # Gravity-1g reference cho moi truc tu cap mat doi xung
    # +X up:  ax sau bias ~ +g
    # -X up:  ax sau bias ~ -g
    # scale_x = g / ((mean_+X_ax - bias_x) - (mean_-X_ax - bias_x)) / 2
    def face_mean(face, idx):
        return statistics.mean(v[idx] for v in buf[face])
    scale = [0, 0, 0]
    for i, (pos, neg) in enumerate(zip(("+X up", "+Y up", "+Z up"),
                                       ("-X up", "-Y up", "-Z up"))):
        p = face_mean(pos, i) - acc_bias[i]
        n = face_mean(neg, i) - acc_bias[i]
        scale[i] = GRAVITY / ((p - n) / 2.0) if (p - n) != 0 else 1.0

    # Gyro bias = trung binh tren tat ca 6 mat (giu yen -> gyro nen ~ 0)
    all_g = [(gx, gy, gz) for face in ORIENTATIONS for (*_a, gx, gy, gz) in buf[face]]
    gyro_bias = [statistics.mean(v[i] for v in all_g) for i in range(3)]

    result = {
        "acc_bias": acc_bias,
        "acc_scale": scale,
        "gyro_bias": gyro_bias,
        "n_samples_per_face": SAMPLES_PER_FACE,
    }
    with open(args.out, "w") as f:
        json.dump(result, f, indent=2)
    print(f"\n[OK] saved -> {args.out}")
    print(json.dumps(result, indent=2))
    print("\nLuu y: hien tai firmware chua ho tro nap calibration qua MQTT.")
    print("Nguoi dung nen copy 3 mang nay vao tools/flash_calib serial hoac chinh tay")
    print("trong main.cpp tam thoi (TODO: them serial command nap calib).")


if __name__ == "__main__":
    main()
