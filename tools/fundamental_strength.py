#!/usr/bin/env python3
"""The fundamental's strength, recordings against the synth.

For each note, the 1st partial re partials 2-4 (dB) between 0.3 and 0.8 s
after the onset: the median over the Philharmonia arco-normal mf and f takes
(never commit them), and the synth rendered at velocity 80. A weak or
too-strong fundamental is the "thin" or "buzzy" low end (docs/PLAN.md Ideas:
G4-A4 on the D string are weak). Useful after changing the body, the bridge,
the low cut or the string choice.

    python3 tools/fundamental_strength.py --data <phil wav> [render] [--param id=value ...] [--notes 55,62,69]
"""
import argparse, glob, json, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from evalkit import read, mtof

ap = argparse.ArgumentParser()
ap.add_argument("--data", default=os.path.expanduser("~/data/phil/wav"))
ap.add_argument("render", nargs="?", default="build/stringmodel_render")
ap.add_argument("--param", action="append", default=[])
ap.add_argument("--notes", default="55,57,60,62,64,65,67,68,69,70,71,72,74,76,79,81")
a = ap.parse_args()
extra = {k: float(v) for k, v in (p.split("=", 1) for p in a.param)}
NAMES = ["C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B"]


def h1(sr, x, f0, t0):
    seg = x[int((t0 + 0.3) * sr):int((t0 + 0.8) * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 17)) ** 2
    F = np.fft.rfftfreq(1 << 17, 1 / sr)
    b = lambda n: S[(F > n * f0 * 0.97) & (F < n * f0 * 1.03)].sum()
    return 10 * np.log10(b(1) / (b(2) + b(3) + b(4)))


def synth(n):
    with tempfile.TemporaryDirectory() as t:
        json.dump({"patch": "init", "length": 1.6, "params": dict({"room.mix": 0}, **extra),
                   "events": [{"t": 0, "on": n, "vel": 80}, {"t": 1.3, "off": n}]}, open(t + "/s.json", "w"))
        subprocess.run([a.render, t + "/s.json", "--out", t + "/s.wav"], check=True, capture_output=True)
        sr, x = read(t + "/s.wav")
    return h1(sr, x.mean(1) if x.ndim > 1 else x, mtof(n), 0.0)


print("note   recordings   synth   synth - real   (1st partial re 2-4, dB)")
for n in map(int, a.notes.split(",")):
    nm = f"{NAMES[n % 12]}{n // 12 - 1}"
    reals = []
    for p in glob.glob(f"{a.data}/violin_{nm}_*_mezzo-forte_arco-normal.wav") + glob.glob(f"{a.data}/violin_{nm}_*_forte_arco-normal.wav"):
        sr, x = read(p)
        x = x.mean(1) if x.ndim > 1 else x
        e = np.abs(x)
        reals.append(h1(sr, x, mtof(n), np.argmax(e > 0.1 * e.max()) / sr))
    s = synth(n)
    if reals:
        r = float(np.median(reals))
        print(f"{nm:5s}  {r:+7.1f} ({len(reals)})  {s:+6.1f}   {s - r:+6.1f}")
    else:
        print(f"{nm:5s}       -        {s:+6.1f}")
