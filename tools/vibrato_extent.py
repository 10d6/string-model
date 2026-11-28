#!/usr/bin/env python3
"""Vibrato Shaping: extent (5-95% pitch range, cents) on a held B4 early and
late, on B6, and on a B4 right after a fast run, at Shaping 0 and 0.7.
Expected: B6 wider; after a run narrow at first, full within ~1 s.
python3 tools/vibrato_extent.py"""
import numpy as np
from evalkit import render, pitch_track, mtof


def extent(x, sr, t0, t1, want):
    c = 1200 * np.log2(pitch_track(x, sr, t0, t1, want) / want)
    return np.percentile(c, 95) - np.percentile(c, 5)


b4, b6 = mtof(71), mtof(95)
for shape in (0, 0.7):
    ev = [{"t": 0, "on": 71, "vel": 80}, {"t": 3, "off": 71}, {"t": 3.3, "on": 95, "vel": 80}, {"t": 6.3, "off": 95}]
    t = 6.6
    for n in [72, 74, 76, 77, 79, 77, 76, 74] * 2:
        ev += [{"t": round(t, 3), "on": n, "vel": 80}, {"t": round(t + 0.115, 3), "off": n}]
        t += 0.12
    ev += [{"t": round(t, 3), "on": 71, "vel": 80}, {"t": round(t + 1.5, 3), "off": 71}]
    sr, x = render({"patch": "init", "length": t + 2, "params": {"room.mix": 0, "vib.shape": shape}, "events": ev})
    print(f"shape {shape}: B4 0.5-1 s {extent(x, sr, 0.5, 1.0, b4):.0f}c, 2.2-2.9 s {extent(x, sr, 2.2, 2.9, b4):.0f}c; "
          f"B6 {extent(x, sr, 5.0, 6.0, b6):.0f}c; B4 after the run 0.2-0.5 s {extent(x, sr, t + 0.2, t + 0.5, b4):.0f}c, "
          f"0.7-1.3 s {extent(x, sr, t + 0.7, t + 1.3, b4):.0f}c")
