#!/usr/bin/env python3
"""Pizzicato two-stage decay: the synth against VSCO-2-CE solo violin pizz
(CC0; fetch the Pizz folder with instrument-research's fetch_vsco.sh or a
sparse checkout of "Strings/Solo Violin/Pizz/" — never commit it).
Per note: early decay (peak+20..150 ms), late decay (0.3-1 s), level 1 s
after the pluck; the score is the mean error (rates in 30 dB/s, level in
10 dB): 1.71 with one polarisation, 1.02 with two (Phase 6). The synth is
rendered with its default room, since the recordings' tails include theirs.
python3 tools/pizz_decay.py "<VSCO>/Strings/Solo Violin/Pizz" """
import glob
import os
import sys
import numpy as np
from evalkit import render, read

NOTES = {"G3": 55, "A3": 57, "C4": 60, "E4": 64, "G4": 67, "A4": 69, "C5": 72, "E5": 76, "G5": 79, "A5": 81, "E6": 88, "A6": 93}


def rates(sr, x):
    h, w = int(0.005 * sr), int(0.01 * sr)
    e = np.array([np.sqrt(np.mean(x[i:i + w] ** 2)) for i in range(0, len(x) - w, h)])
    e = 20 * np.log10(e / e.max() + 1e-9)
    p = int(np.argmax(e))
    t = (np.arange(len(e)) - p) * 0.005

    def slope(a, b):
        m = (t >= a) & (t <= b) & (e > -45)
        return np.polyfit(t[m], e[m], 1)[0] if m.sum() >= 5 else float("nan")
    late = e[(t > 0.9) & (t < 1.0)].mean() if (t > 1).any() else float("nan")
    return slope(0.02, 0.15), slope(0.3, 1.0), late


def main():
    real = {}
    for p in glob.glob(os.path.join(sys.argv[1], "*_f_RR*.wav")):
        name = os.path.basename(p).split("_")[2]
        real.setdefault(name, []).append(rates(*read(p)))
    err = []
    print("note   early synth/real   late synth/real   level@1s synth/real")
    for name, n in NOTES.items():
        if name not in real:
            continue
        sr, x = render({"patch": "init", "length": 2.0, "params": {"artic": 4},
                        "events": [{"t": 0.05, "on": n, "vel": 110}, {"t": 1.9, "off": n}]})
        s, r = rates(sr, x), np.nanmean(np.array(real[name]), 0)
        print(f"{name:4s} {s[0]:6.0f}/{r[0]:6.0f}     {s[1]:6.0f}/{r[1]:6.0f}     {s[2]:6.1f}/{r[2]:6.1f}")
        for i in range(3):
            if not (np.isnan(s[i]) or np.isnan(r[i])):
                err.append(abs(s[i] - r[i]) / (10 if i == 2 else 30))
    print(f"score (lower is better): {np.mean(err):.2f}")


main()
