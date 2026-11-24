#!/usr/bin/env python3
"""Pizzicato two-stage decay: the synth against VSCO-2-CE solo violin pizz
(CC0; fetch the Pizz folder with instrument-research's fetch_vsco.sh or a
sparse checkout of "Strings/Solo Violin/Pizz/" — never commit it).
Per note: early decay (peak+20..150 ms), late decay (0.3-1 s), level 1 s
after the pluck; the score is the mean error (rates in 30 dB/s, level in
10 dB): 1.71 with one polarisation, 1.02 with two (Phase 6). The synth is
rendered with its default room, since the recordings' tails include theirs.
python3 tools/pizz_decay.py "<VSCO>/Strings/Solo Violin/Pizz"

The cello against the Iowa MIS pizzicato (anechoic: the synth without its
room; each note on the string it was recorded on, tools/iowa.py):
python3 tools/pizz_decay.py --iowa ~/data/iowa/cello/wav/pizz [--dyn mf,ff] [--param id=value ...]
"""
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


def iowa_main(args):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import iowa
    d = args[args.index("--iowa") + 1]
    dyns = args[args.index("--dyn") + 1].split(",") if "--dyn" in args else ["mf", "ff"]
    over = {}
    for i, a in enumerate(args):
        if a == "--param":
            k, v = args[i + 1].split("=")
            over[k] = float(v)
    vel = {"pp": 40, "mf": 80, "ff": 115}
    err, rows = [], []
    for f in sorted(os.listdir(d)):
        info = iowa.parse(f)
        if not info or info[2] not in dyns:
            continue
        sr, x = iowa.load(os.path.join(d, f))
        for n, st, dyn, t0, t1 in iowa.iowa_notes(os.path.join(d, f)):
            if t1 - t0 < 1.3:
                continue
            seg = x[int(max(0, t0 - 0.05) * sr):int(t1 * sr)]
            r = rates(sr, seg)
            srs, y = render({"patch": "init", "length": 2.0,
                             "params": {"instrument": 1, "artic": 4, "room.mix": 0, "string.select": st + 1, **over},
                             "events": [{"t": 0.05, "on": n, "vel": vel[dyn]}, {"t": 1.9, "off": n}]})
            sy = rates(srs, y)
            rows.append((dyn, n, st, sy, r))
            for i in range(3):
                if not (np.isnan(sy[i]) or np.isnan(r[i])):
                    err.append(abs(sy[i] - r[i]) / (10 if i == 2 else 30))
    print("dyn note str   early synth/real   late synth/real   level@1s synth/real")
    for dyn, n, st, sy, r in rows:
        print(f"{dyn:3s} {n:4d}  {'CGDA'[st]}   {sy[0]:6.0f}/{r[0]:6.0f}     {sy[1]:6.0f}/{r[1]:6.0f}     {sy[2]:6.1f}/{r[2]:6.1f}")
    a = np.array([[*sy, *r] for _, _, _, sy, r in rows])
    med = np.nanmedian(a, 0)
    print(f"median: early {med[0]:.0f}/{med[3]:.0f} dB/s, late {med[1]:.0f}/{med[4]:.0f} dB/s, level@1s {med[2]:.1f}/{med[5]:.1f} dB")
    print(f"score (lower is better): {np.mean(err):.2f} over {len(rows)} notes")


def main():
    if "--iowa" in sys.argv:
        return iowa_main(sys.argv)
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
