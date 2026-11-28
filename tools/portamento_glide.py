#!/usr/bin/env python3
"""Portamento: the pitch glide (10-90% of the interval, ms) on slow slurred
leaps (keys 1 s apart, overlapping; Legato on), at Portamento 0, 0.3 and 1. Leaps that
cross strings or start from an open string don't slide.

    python3 tools/portamento_glide.py [render]"""
import json, os, subprocess, sys, tempfile, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from evalkit import read, pitch, mtof
R = sys.argv[1] if len(sys.argv) > 1 else "build/stringmodel_render"
leaps = [(69, 76), (76, 69), (69, 81), (64, 71), (71, 64), (62, 69), (74, 81), (67, 74), (72, 79), (60, 67)]
def run(port):
    ev = []; t = 0.3
    for a, b in leaps:
        ev += [{"t": round(t, 3), "on": a, "vel": 60}, {"t": round(t + 1.0, 3), "on": b, "vel": 60},
               {"t": round(t + 1.05, 3), "off": a}, {"t": round(t + 1.9, 3), "off": b}]
        t += 2.4
    with tempfile.TemporaryDirectory() as tmp:
        json.dump({"patch": "init", "length": t + 0.5, "params": {"room.mix": 0, "vib.depth": 0, "finger.portamento": port, "bow.legato": 1},
                   "events": ev}, open(tmp + "/s.json", "w"))
        subprocess.run([R, tmp + "/s.json", "--out", tmp + "/s.wav"], check=True, capture_output=True)
        sr, x = read(tmp + "/s.wav")
    x = x.mean(1) if x.ndim > 1 else x
    out = []; t = 0.3
    for a, b in leaps:
        fa, fb = mtof(a), mtof(b); lo, hi = min(fa, fb) * 0.94, max(fa, fb) * 1.06
        w = int(0.025 * sr); F = np.fft.rfftfreq(1 << 15, 1 / sr); m = (F > lo) & (F < hi)
        ts = np.arange(t + 0.85, t + 1.6, 0.002)
        f = [F[m][np.argmax(np.abs(np.fft.rfft(x[int(u*sr):int(u*sr)+w] * np.hanning(w), 1 << 15))[m])] for u in ts]
        c = 1200 * np.log2(np.array(f) / mtof(a)); span = 100 * (b - a)
        frac = c / span
        try:
            t10 = ts[np.argmax(frac > 0.1)]; t90 = ts[np.argmax(frac > 0.9)]
            out.append(round((t90 - t10) * 1000))
        except Exception: out.append(None)
        t += 2.4
    return out
for p in (0, 0.3, 1):
    print("portamento %.1f: glide ms (10-90%%) %s" % (p, run(p)))
print("leaps:", leaps)
