#!/usr/bin/env python3
"""The wolf note (KB §5.4): a string note on a strong, lightly damped body mode
beats (energy swaps between string and body at a few Hz) and, worst, loses its
Helmholtz motion. For held notes on one string at a few dynamics (vibrato off,
Pickup output), from 0.4 s to the release:

  beat   envelope modulation depth: rms of the 1-15 Hz part of the 10 ms level,
         dB (a steady note ~0.5; a wolf several dB)
  rate   the strongest envelope frequency, Hz
  clean  % of 20 ms frames in Helmholtz motion (periodic at the note, not at half
         its period); a wolf that breaks drops it

    python3 tools/wolf_check.py [--string 1] [--notes 50-56] [--param body.wolf=0.5] [--render R]
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iowa


def measure(x, sr, note, t0, t1):
    q = max(1, int(sr // 24000))
    y = signal.resample_poly(x, 1, q)
    fs = sr / q
    seg = y[int(t0 * fs):int(t1 * fs)]
    hop = int(0.01 * fs)
    e = np.sqrt(np.convolve(seg ** 2, np.ones(hop) / hop, "same")[::hop] + 1e-20)
    db = 20 * np.log10(e)
    b, a = signal.butter(2, [1 / 50, 15 / 50], "band")  # 1-15 Hz at the 100 Hz frame rate
    mod = signal.filtfilt(b, a, db - db.mean())
    beat = float(np.sqrt(np.mean(mod ** 2)) * np.sqrt(2))
    f, P = signal.welch(db - db.mean(), 100, nperseg=min(len(db), 128))
    m = (f >= 1) & (f <= 15)
    rate = float(f[m][np.argmax(P[m])]) if m.any() else 0.0
    want = 440 * 2 ** ((note - 69) / 12)
    lag = fs / want
    w = int(4 * lag)
    ok = tot = 0
    for i in range(0, len(seg) - w - int(lag * 1.1), int(0.02 * fs)):
        a0 = seg[i:i + w]
        best, bl = -1, 0
        for L in range(int(lag * 0.97), int(lag * 1.03) + 2):
            b0 = seg[i + L:i + L + w]
            r = np.dot(a0, b0) / np.sqrt(np.dot(a0, a0) * np.dot(b0, b0) + 1e-20)
            if r > best:
                best, bl = r, L
        h = int(round(bl / 2))
        bh = seg[i + h:i + h + w]
        rh = np.dot(a0, bh) / np.sqrt(np.dot(a0, a0) * np.dot(bh, bh) + 1e-20)
        tot += 1
        ok += best > 0.9 and rh < 0.5
    return beat, rate, 100.0 * ok / max(tot, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--string", type=int, default=1, help="0 C, 1 G, 2 D, 3 A")
    ap.add_argument("--notes", default="50-56")
    ap.add_argument("--dyn", default="0.1,0.45,0.85")
    ap.add_argument("--param", action="append", default=[])
    a = ap.parse_args()
    lo, hi = (int(v) for v in a.notes.split("-"))
    over = {k: float(v) for k, v in (p.split("=") for p in a.param)}
    dyns = [float(d) for d in a.dyn.split(",")]
    print("note  " + "".join(f"   d{d:.2f}: beat  rate clean" for d in dyns))
    for n in range(lo, hi + 1):
        row = []
        for d in dyns:
            with tempfile.TemporaryDirectory() as t:
                sc = {"patch": "init", "length": 3.0,
                      "params": {"instrument": 1, "string.select": a.string + 1, "vib.depth": 0, "room.mix": 0,
                                 "out.mode": 2, "bow.velSens": 0, "bow.dynamics": d, **over},
                      "events": [{"t": 0.02, "on": n, "vel": 100}, {"t": 2.8, "off": n}]}
                json.dump(sc, open(t + "/s.json", "w"))
                subprocess.run([a.render, t + "/s.json", "--out", t + "/s.wav"], check=True, capture_output=True)
                sr, x = iowa.load(t + "/s.wav")
            row.append(measure(x, sr, n, 0.4, 2.7))
        print(f"{n:4d}  " + "".join(f"   {b:5.1f} dB {r:4.1f} {c:4.0f}%" for b, r, c in row), flush=True)


if __name__ == "__main__":
    main()
