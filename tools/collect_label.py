"""
Thu data IMU tu ESP32 qua MQTT (topic usth/pdr/<dev>/raw) va ghi CSV co nhan.

Cach dung:
    1. Bat raw stream tren ESP32 bang cach publish:
       mosquitto_pub -h <broker> -t usth/pdr/cmd/raw_on -m on
    2. Chay script:
       python collect_label.py --broker 127.0.0.1 --device dev01 --out data/session1.csv
    3. Trong khi thu, bam phim:
       1 = stand, 2 = walk, 3 = run, 4 = stairs, SPACE = pause, q = quit

CSV cot: ts_ms,device,ax,ay,az,gx,gy,gz,label
"""
import argparse
import csv
import json
import sys
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt

LABEL_MAP = {"1": "stand", "2": "walk", "3": "run", "4": "stairs"}


class Labeler:
    def __init__(self):
        self.current = "stand"
        self.paused = False
        self._lock = threading.Lock()

    def set(self, label):
        with self._lock:
            self.current = label

    def toggle_pause(self):
        with self._lock:
            self.paused = not self.paused
            return self.paused

    def get(self):
        with self._lock:
            return None if self.paused else self.current


def keyboard_loop(labeler: Labeler, stop_event: threading.Event):
    """Doc phim tu stdin. Dung tren console, khong can quyen admin."""
    print("Phim: 1=stand 2=walk 3=run 4=stairs SPACE=pause q=quit")
    try:
        # Try tao mot 'getch' kha di Windows + POSIX
        if sys.platform == "win32":
            import msvcrt
            while not stop_event.is_set():
                if msvcrt.kbhit():
                    ch = msvcrt.getch().decode(errors="ignore")
                    handle_key(ch, labeler, stop_event)
                else:
                    time.sleep(0.05)
        else:
            import termios, tty, select
            fd = sys.stdin.fileno()
            old = termios.tcgetattr(fd)
            try:
                tty.setcbreak(fd)
                while not stop_event.is_set():
                    if select.select([sys.stdin], [], [], 0.05)[0]:
                        ch = sys.stdin.read(1)
                        handle_key(ch, labeler, stop_event)
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, old)
    except Exception as e:
        print(f"[KB] error: {e}", file=sys.stderr)


def handle_key(ch, labeler, stop_event):
    if ch == "q":
        stop_event.set()
    elif ch == " ":
        paused = labeler.toggle_pause()
        print(f"[LABEL] paused={paused}")
    elif ch in LABEL_MAP:
        labeler.set(LABEL_MAP[ch])
        print(f"[LABEL] -> {LABEL_MAP[ch]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--broker", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="dev01")
    ap.add_argument("--out", required=True, help="CSV path")
    args = ap.parse_args()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    labeler = Labeler()
    stop_event = threading.Event()

    f = out_path.open("w", newline="")
    writer = csv.writer(f)
    writer.writerow(["ts_ms", "device", "ax", "ay", "az", "gx", "gy", "gz", "label"])
    count = 0
    t0 = time.time()

    def on_connect(client, userdata, flags, reason_code, props):
        topic = f"usth/pdr/{args.device}/raw"
        client.subscribe(topic)
        print(f"[MQTT] connected, subscribed {topic}")

    def on_message(client, userdata, msg):
        nonlocal count
        label = labeler.get()
        if label is None:
            return
        try:
            d = json.loads(msg.payload)
            writer.writerow([d["ts"], args.device, d["ax"], d["ay"], d["az"],
                             d["gx"], d["gy"], d["gz"], label])
            count += 1
            if count % 100 == 0:
                rate = count / max(1, time.time() - t0)
                print(f"[CSV] rows={count} rate={rate:.1f}/s label={label}")
        except Exception as e:
            print(f"[MSG] error: {e}", file=sys.stderr)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="collect-label")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, keepalive=30)

    kb = threading.Thread(target=keyboard_loop, args=(labeler, stop_event), daemon=True)
    kb.start()

    try:
        while not stop_event.is_set():
            client.loop(timeout=0.2)
    finally:
        client.disconnect()
        f.close()
        print(f"[DONE] {count} rows -> {out_path}")


if __name__ == "__main__":
    main()
