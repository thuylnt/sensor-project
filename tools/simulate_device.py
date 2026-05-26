"""
Gia lap thiet bi ESP32 publish MQTT giong nhu firmware that.
Dung de test Node-RED dashboard / Grafana khi chua co phan cung.

Publish cac topic:
  usth/pdr/<dev>/activity   1 Hz   {ts, class, confidence}
  usth/pdr/<dev>/pose       1 Hz   {ts, x, y, heading_deg}
  usth/pdr/<dev>/step       moi step
  usth/pdr/<dev>/status     0.1 Hz retained
  usth/pdr/<dev>/raw        100 Hz (chi khi --raw)

Quy dao mac dinh: di vong hinh chu nhat 10m x 5m, cadence ~110 step/min,
moi 30s lai chuyen activity (stand -> walk -> run -> walk -> stand) de
dashboard sinh dong.

Cach dung:
  python simulate_device.py --broker 127.0.0.1
  python simulate_device.py --broker 127.0.0.1 --device dev02 --pattern square --raw
  python simulate_device.py --pattern line --speed 1.5 --no-activity-cycle
  python simulate_device.py --pattern random
"""
import argparse
import json
import math
import random
import signal
import sys
import threading
import time

import paho.mqtt.client as mqtt


# ---------------------------------------------------------------------------
# Pattern generators: tra ve (dx, dy, heading_deg) bang cach yield tu generator
# Moi step la 1 buoc di, do dai = stride.
# ---------------------------------------------------------------------------

def pattern_square(width=10.0, height=5.0, stride=0.7):
    """Di nguoc chieu kim dong ho theo hinh chu nhat width x height."""
    legs = [(width,  0.0),    # +X
            (0.0,    height), # +Y
            (-width, 0.0),    # -X
            (0.0,   -height)] # -Y
    while True:
        for dx, dy in legs:
            length = math.hypot(dx, dy)
            n_steps = max(1, int(round(length / stride)))
            heading = math.degrees(math.atan2(dx, dy)) % 360   # canvas: y up, x right
            step_x = dx / n_steps
            step_y = dy / n_steps
            for _ in range(n_steps):
                yield step_x, step_y, heading


def pattern_line(length=20.0, stride=0.7):
    """Di lai di lai 1 doan thang truc Y."""
    while True:
        n_steps = max(1, int(round(length / stride)))
        for _ in range(n_steps):
            yield 0.0, stride, 0.0
        for _ in range(n_steps):
            yield 0.0, -stride, 180.0


def pattern_random(stride=0.7, turn_sigma_deg=15.0):
    """Random walk: moi step quay 1 goc nho ngau nhien."""
    heading = 0.0
    while True:
        heading = (heading + random.gauss(0.0, turn_sigma_deg)) % 360
        h_rad = math.radians(heading)
        yield stride * math.sin(h_rad), stride * math.cos(h_rad), heading


PATTERNS = {
    "square":  lambda: pattern_square(),
    "line":    lambda: pattern_line(),
    "random":  lambda: pattern_random(),
}


# ---------------------------------------------------------------------------
# Activity scheduler
# ---------------------------------------------------------------------------

ACTIVITY_CYCLE = [
    ("stand",  10.0),
    ("walk",   25.0),
    ("run",    15.0),
    ("walk",   20.0),
    ("stairs", 15.0),
    ("walk",   15.0),
]


def activity_at(t_elapsed, cycle):
    total = sum(d for _, d in cycle)
    t = t_elapsed % total
    acc = 0.0
    for name, dur in cycle:
        acc += dur
        if t < acc:
            return name
    return cycle[-1][0]


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--broker", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="dev01")
    ap.add_argument("--pattern", choices=PATTERNS.keys(), default="square")
    ap.add_argument("--cadence", type=float, default=110.0, help="step / phut khi walk")
    ap.add_argument("--speed", type=float, default=1.0, help="scale van toc")
    ap.add_argument("--raw", action="store_true", help="them topic raw 100Hz")
    ap.add_argument("--no-activity-cycle", action="store_true",
                    help="luon publish activity = walk")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--three-paths", action="store_true", default=True,
                    help="publish them pose_raw va pose_calib voi drift gia lap (mac dinh BAT)")
    ap.add_argument("--no-three-paths", dest="three_paths", action="store_false",
                    help="chi publish pose (full), khong gia drift")
    ap.add_argument("--drift-raw-deg", type=float, default=0.10,
                    help="heading drift moi step cho 'raw' (deg)")
    ap.add_argument("--drift-calib-deg", type=float, default=0.03,
                    help="heading drift moi step cho 'calib' (deg)")
    args = ap.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    base = f"usth/pdr/{args.device}"
    topic_act        = f"{base}/activity"
    topic_step       = f"{base}/step"
    topic_pose       = f"{base}/pose"           # full pipeline (ground truth)
    topic_pose_calib = f"{base}/pose_calib"     # with small drift
    topic_pose_raw   = f"{base}/pose_raw"       # with larger drift
    topic_status     = f"{base}/status"
    topic_raw        = f"{base}/raw"

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id=f"sim-{args.device}")
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()
    print(f"[SIM] connected {args.broker}:{args.port}, publishing as {args.device}")
    print(f"[SIM] pattern={args.pattern}  cadence={args.cadence} spm  raw={args.raw}")
    if args.three_paths:
        print(f"[SIM] three-paths: drift raw={args.drift_raw_deg}deg/step  calib={args.drift_calib_deg}deg/step")

    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *a: stop.set())

    def publish(topic, payload, retain=False):
        client.publish(topic, json.dumps(payload), qos=0, retain=retain)

    # State - 3 song song: full (ground truth), calib (drift nho), raw (drift lon)
    pos_x, pos_y = 0.0, 0.0
    heading = 0.0
    step_count = 0
    gen = PATTERNS[args.pattern]()
    t_start = time.time()
    # Drift accumulate per step (degrees, them vao true heading)
    drift_raw = 0.0
    drift_calib = 0.0
    pos_raw = [0.0, 0.0]
    pos_calib = [0.0, 0.0]

    # Tan suat
    base_step_interval = 60.0 / max(1e-3, args.cadence) / args.speed
    last_step_t = time.time()
    last_pose_t = 0.0
    last_act_t  = 0.0
    last_status_t = 0.0
    raw_period = 0.01  # 100 Hz
    last_raw_t = 0.0

    activity = "walk"

    while not stop.is_set():
        now = time.time()
        elapsed = now - t_start

        # 1) Cap nhat activity (cycle hoac fixed)
        new_act = ("walk" if args.no_activity_cycle
                   else activity_at(elapsed, ACTIVITY_CYCLE))
        if new_act != activity or (now - last_act_t) >= 1.0:
            activity = new_act
            conf = round(random.uniform(0.82, 0.97), 2)
            publish(topic_act, {
                "ts": int(now * 1000),
                "class": activity,
                "confidence": conf,
            })
            last_act_t = now

        # 2) Step (chi khi walking / running / stairs)
        if activity in ("walk", "run", "stairs"):
            # Cadence cao hon khi chay
            step_interval = base_step_interval * (
                0.5 if activity == "run" else (1.3 if activity == "stairs" else 1.0)
            )
            if now - last_step_t >= step_interval:
                dx, dy, heading = next(gen)
                pos_x += dx
                pos_y += dy
                step_count += 1
                stride = math.hypot(dx, dy)
                # 2 path bi sai heading - accumulate per step
                if args.three_paths:
                    drift_raw   += args.drift_raw_deg
                    drift_calib += args.drift_calib_deg
                    h_raw   = math.radians(heading + drift_raw)
                    h_calib = math.radians(heading + drift_calib)
                    pos_raw[0]   += stride * math.sin(h_raw)
                    pos_raw[1]   += stride * math.cos(h_raw)
                    pos_calib[0] += stride * math.sin(h_calib)
                    pos_calib[1] += stride * math.cos(h_calib)
                cadence_spm = 60.0 / (now - last_step_t) if last_step_t else args.cadence
                publish(topic_step, {
                    "ts": int(now * 1000),
                    "count": step_count,
                    "cadence": round(cadence_spm, 1),
                    "stride": round(stride, 3),
                })
                last_step_t = now

        # 3) Pose 1 Hz - publish full + (optional) calib/raw
        if now - last_pose_t >= 1.0:
            publish(topic_pose, {
                "ts": int(now * 1000),
                "x": round(pos_x, 3),
                "y": round(pos_y, 3),
                "heading_deg": round(heading, 1),
            })
            if args.three_paths:
                publish(topic_pose_calib, {
                    "ts": int(now * 1000),
                    "x": round(pos_calib[0], 3),
                    "y": round(pos_calib[1], 3),
                    "heading_deg": round(heading + drift_calib, 1),
                })
                publish(topic_pose_raw, {
                    "ts": int(now * 1000),
                    "x": round(pos_raw[0], 3),
                    "y": round(pos_raw[1], 3),
                    "heading_deg": round(heading + drift_raw, 1),
                })
            last_pose_t = now

        # 4) Status retained moi 10s
        if now - last_status_t >= 10.0:
            publish(topic_status, {
                "ts": int(now * 1000),
                "uptime_s": int(elapsed),
                "rssi": random.randint(-75, -45),
                "free_heap": random.randint(180000, 220000),
                "raw_stream": args.raw,
                "simulated": True,
            }, retain=True)
            last_status_t = now

        # 5) Raw 100 Hz (optional, vi du gia tin hieu gia toc + gyro co dao dong)
        if args.raw and (now - last_raw_t >= raw_period):
            t = elapsed
            # Gia lap dao dong ~2Hz cho walking, 3Hz cho run
            freq = {"walk": 2.0, "run": 3.0, "stairs": 1.8}.get(activity, 0.0)
            amp = {"walk": 2.0, "run": 4.0, "stairs": 2.5}.get(activity, 0.1)
            phase = 2 * math.pi * freq * t
            ax = amp * math.sin(phase) + random.gauss(0, 0.05)
            ay = amp * 0.3 * math.cos(phase) + random.gauss(0, 0.05)
            az = 9.81 + amp * 0.5 * math.sin(phase * 2) + random.gauss(0, 0.05)
            gx = random.gauss(0, 0.02)
            gy = random.gauss(0, 0.02)
            # gyro Z khi quay = derivative cua heading. Tam gan nho.
            gz = random.gauss(0, 0.02)
            publish(topic_raw, {
                "ts": int(now * 1000),
                "ax": round(ax, 3), "ay": round(ay, 3), "az": round(az, 3),
                "gx": round(gx, 4), "gy": round(gy, 4), "gz": round(gz, 4),
            })
            last_raw_t = now

        time.sleep(0.005)

    client.loop_stop()
    client.disconnect()
    print(f"\n[SIM] stop, steps={step_count} pos=({pos_x:.2f},{pos_y:.2f})")


if __name__ == "__main__":
    main()
