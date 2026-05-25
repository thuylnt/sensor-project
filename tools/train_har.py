"""
Train 1D-CNN HAR tu CSV thu duoc bang collect_label.py.

Pipeline:
  1. Doc nhieu file CSV (cot: ts_ms, device, ax..gz, label)
  2. Cua so 200 mau (2s @ 100Hz), step = 100 (overlap 50%)
  3. Z-score per-window per-axis
  4. Train CNN: Conv1D(16,9,s=2) -> Conv1D(32,5) -> Conv1D(32,3) -> GAP -> Dense(32) -> Dense(4)
  5. Quantize int8 va export model_quant.tflite + array C de copy vao model_data.h

Cach dung:
  python train_har.py --data data/*.csv --out model/
"""
import argparse
import glob
from pathlib import Path

import numpy as np
import pandas as pd


LABELS = ["stand", "walk", "run", "stairs"]
WINDOW = 200
HOP = 100
AXES = ["ax", "ay", "az", "gx", "gy", "gz"]


def load_csvs(patterns):
    dfs = []
    for p in patterns:
        for f in glob.glob(p):
            d = pd.read_csv(f)
            d["__src"] = Path(f).stem
            dfs.append(d)
    if not dfs:
        raise SystemExit("Khong tim thay file CSV nao")
    return pd.concat(dfs, ignore_index=True)


def make_windows(df):
    X, y, src = [], [], []
    # Cua so chi trong cung 1 file va 1 nhan lien tuc
    for (file_id, label), grp in df.groupby(["__src", "label"]):
        arr = grp[AXES].to_numpy(dtype=np.float32)
        for s in range(0, len(arr) - WINDOW + 1, HOP):
            w = arr[s:s + WINDOW]
            mean = w.mean(axis=0, keepdims=True)
            std = w.std(axis=0, keepdims=True) + 1e-6
            X.append((w - mean) / std)
            y.append(LABELS.index(label))
            src.append(file_id)
    return np.stack(X), np.array(y), np.array(src)


def build_model(n_classes):
    import tensorflow as tf
    from tensorflow.keras import layers, Model
    inp = layers.Input(shape=(WINDOW, len(AXES)))
    x = layers.Conv1D(16, 9, strides=2, padding="same", activation="relu")(inp)
    x = layers.BatchNormalization()(x)
    x = layers.MaxPool1D(2)(x)
    x = layers.Conv1D(32, 5, strides=1, padding="same", activation="relu")(x)
    x = layers.BatchNormalization()(x)
    x = layers.MaxPool1D(2)(x)
    x = layers.Conv1D(32, 3, padding="same", activation="relu")(x)
    x = layers.GlobalAveragePooling1D()(x)
    x = layers.Dense(32, activation="relu")(x)
    x = layers.Dropout(0.3)(x)
    out = layers.Dense(n_classes, activation="softmax")(x)
    return Model(inp, out)


def quantize_to_tflite(model, X_sample, out_path):
    import tensorflow as tf

    def rep_data():
        for x in X_sample[:200]:
            yield [x[None, ...].astype(np.float32)]

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = rep_data
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    tflite = conv.convert()
    Path(out_path).write_bytes(tflite)
    return tflite


def write_c_array(tflite_bytes, header_path):
    name = "g_model_data"
    lines = ["#pragma once",
             f"// Auto-generated. Size: {len(tflite_bytes)} bytes",
             "#include <stddef.h>",
             "",
             f"alignas(16) const unsigned char {name}[] = {{"]
    for i in range(0, len(tflite_bytes), 12):
        chunk = tflite_bytes[i:i + 12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"const unsigned int {name}_len = {len(tflite_bytes)};")
    Path(header_path).write_text("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True, help="CSV path(s) hoac glob")
    ap.add_argument("--out", default="model/")
    ap.add_argument("--epochs", type=int, default=40)
    args = ap.parse_args()

    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)

    df = load_csvs(args.data)
    print(f"[DATA] {len(df)} rows tu {df['__src'].nunique()} file")
    print(df["label"].value_counts())

    X, y, src = make_windows(df)
    print(f"[WIN] {X.shape} y={np.bincount(y, minlength=len(LABELS))}")

    # Train/val split THEO file de tranh leak
    rng = np.random.default_rng(42)
    files = np.unique(src); rng.shuffle(files)
    val_files = set(files[:max(1, len(files) // 5)])
    val_mask = np.isin(src, list(val_files))
    Xtr, ytr = X[~val_mask], y[~val_mask]
    Xva, yva = X[val_mask], y[val_mask]
    print(f"[SPLIT] train={len(Xtr)} val={len(Xva)} val_files={sorted(val_files)}")

    import tensorflow as tf
    model = build_model(len(LABELS))
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy",
                  metrics=["accuracy"])
    model.summary()

    cw = {i: len(ytr) / (len(LABELS) * max(1, (ytr == i).sum())) for i in range(len(LABELS))}
    model.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=args.epochs,
              batch_size=64, class_weight=cw, verbose=2)

    # Eval F1
    from sklearn.metrics import classification_report, confusion_matrix
    yp = model.predict(Xva, verbose=0).argmax(1)
    print("\n[VAL]")
    print(classification_report(yva, yp, target_names=LABELS, digits=3))
    print("Confusion matrix:")
    print(confusion_matrix(yva, yp))

    model.save(out / "model.keras")
    tfb = quantize_to_tflite(model, Xtr, out / "model_quant.tflite")
    write_c_array(tfb, out / "model_data.h")
    print(f"[DONE] saved {out / 'model_quant.tflite'} ({len(tfb)} bytes)")
    print(f"       copy {out / 'model_data.h'} -> firmware/src/model_data.h")
    print("       trong inference.cpp, set USE_TFLITE = 1")


if __name__ == "__main__":
    main()
