#!/usr/bin/env python3
"""How much brighter f is than p: the recordings against the synth.

For each VSCO-2-CE solo violin "Arco Vib" note from G3 to E5 that has both a p
and an f take, and the same note rendered at velocity 45 / 110 (as
radiation_target.py pairs them): band levels at 2-4, 4-8 and 8-16 kHz re
0.5-2 kHz, between 0.5 and 1.5 s, then f minus p, median over the notes. The
mics and the body cancel in the difference, so it isolates what playing louder
does to the colour. VSCO is CC0; fetch it with instrument-research's
tools/fetch_vsco.sh and never commit it.

    python3 tools/brightness_spread.py --vsco "<VSCO>/Strings/Solo Violin/Arco Vib" [render ...] [--param id=value ...]

Several renders (e.g. a base build in a worktree) print one row each. With the
output tilt in model/Instrument.h set to 0 it shows what the string does by
itself (docs/MODEL.md, Output: Force-dependent brightness).
"""
import argparse, glob, json, re, subprocess, tempfile
import numpy as np
from scipy.io import wavfile

ap = argparse.ArgumentParser()
ap.add_argument("--vsco", required=True, help="the VSCO Arco Vib folder")
ap.add_argument("renders", nargs="*", default=["build/stringmodel_render"])
ap.add_argument("--param", action="append", default=[], help="id=value for the synth renders")
ap.add_argument("--lo", type=int, default=55, help="lowest MIDI note (default G3)")
ap.add_argument("--hi", type=int, default=76, help="highest MIDI note (default E5)")
a = ap.parse_args()
extra = {k: float(v) for k, v in (p.split("=", 1) for p in a.param)}

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
BANDS = [(2000, 4000), (4000, 8000), (8000, 16000)]


def levels(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    ref = S[(f >= 500) & (f < 2000)].sum()
    return [10 * np.log10(S[(f >= lo) & (f < hi)].sum() / ref) for lo, hi in BANDS]


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    return sr, (x.mean(1) if x.ndim > 1 else x)


notes = {}
for p in glob.glob(a.vsco + "/*.wav"):
    m = re.search(r"_([A-G])(#?)(\d)_([pf])\.wav$", p)
    if m:
        n = 12 * (int(m.group(3)) + 1) + NAMES[m.group(1)] + (1 if m.group(2) else 0)
        if a.lo <= n <= a.hi:
            notes.setdefault(n, {})[m.group(4)] = p
pairs = {n: d for n, d in notes.items() if len(d) == 2}
if not pairs:
    raise SystemExit("no p/f pairs found in " + a.vsco)

rows = {"VSCO": []}
for n, d in sorted(pairs.items()):
    L = {}
    for dyn, p in d.items():
        sr, x = load(p)
        e = np.abs(x)
        t = np.argmax(e > 0.1 * e.max()) / sr
        L[dyn] = levels(x, sr, t + 0.5, t + 1.5)
    rows["VSCO"].append(np.subtract(L["f"], L["p"]))
    for r in a.renders:
        L = {}
        for dyn in "pf":
            with tempfile.TemporaryDirectory() as tmp:
                sc = {"patch": "init", "length": 2.0, "params": dict({"room.mix": 0, "vib.depth": 0}, **extra),
                      "events": [{"t": 0, "on": n, "vel": 45 if dyn == "p" else 110}, {"t": 1.8, "off": n}]}
                json.dump(sc, open(tmp + "/s.json", "w"))
                subprocess.run([r, tmp + "/s.json", "--out", tmp + "/s.wav"], capture_output=True, check=True)
                sr, x = load(tmp + "/s.wav")
                L[dyn] = levels(x, sr, 0.5, 1.5)
        rows.setdefault(r, []).append(np.subtract(L["f"], L["p"]))

print("f minus p, dB re 0.5-2 kHz, median over %d notes:   2-4k   4-8k   8-16k" % len(rows["VSCO"]))
for k, v in rows.items():
    print("%-45s" % k, "  ".join("%+5.1f" % x for x in np.median(np.array(v), 0)))
