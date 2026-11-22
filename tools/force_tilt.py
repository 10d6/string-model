#!/usr/bin/env python3
"""How the synth's bridge force departs from the ideal Helmholtz 1/n spectrum.

Renders sustained notes across the range through the Pickup output (raw bridge
force, no body, no room), measures each harmonic's level, and reports
D(f) = level + 20 log10(n), normalised per note on its lowest harmonics,
averaged in 1/3-octave bands. The library-mic responses assume a 1/n source,
so the synthetic radiation targets H_library(f) - D(f) (KB §12.7: subtract
the synth's own deviation or brightness is counted twice).

    python3 tools/force_tilt.py [build/stringmodel_render] [out.csv]
"""
import json, os, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

render = sys.argv[1] if len(sys.argv) > 1 else "build/stringmodel_render"
out_csv = sys.argv[2] if len(sys.argv) > 2 else None
notes = list(range(55, 96, 2))
pts_f, pts_d = [], []
with tempfile.TemporaryDirectory() as tmp:
    for note in notes:
        scen = {"patch": "init", "length": 2.0, "params": {"out.mode": 2, "room.mix": 0, "vib.depth": 0},
                "events": [{"t": 0, "on": note, "vel": 80}, {"t": 1.8, "off": note}]}
        sp = os.path.join(tmp, "s.json"); wp = os.path.join(tmp, "s.wav")
        json.dump(scen, open(sp, "w"))
        subprocess.run([render, sp, "--out", wp], capture_output=True, check=True)
        sr, x = wavfile.read(wp)
        x = x.astype(float).mean(axis=1) / 2**31
        seg = x[int(0.7 * sr):int(1.7 * sr)]
        win = np.hanning(len(seg))
        S = np.abs(np.fft.rfft(seg * win, 1 << 18))
        f = np.fft.rfftfreq(1 << 18, 1 / sr)
        f0 = 440 * 2 ** ((note - 69) / 12)
        lev = []
        for n in range(1, 200):
            fn = n * f0
            if fn > 12000: break
            band = (f > fn * 0.97) & (f < fn * 1.03)
            lev.append((n, fn, 20 * np.log10(S[band].max() + 1e-12)))
        if not lev: continue
        ref = np.mean([l + 20 * np.log10(n) for n, _, l in lev[:3]])
        for n, fn, l in lev:
            pts_f.append(fn); pts_d.append(l + 20 * np.log10(n) - ref)
pts_f = np.array(pts_f); pts_d = np.array(pts_d)
centres = 1000 * 2 ** (np.arange(-12, 12) / 3)
rows = []
for c in centres:
    m = (pts_f >= c * 2 ** (-1 / 6)) & (pts_f < c * 2 ** (1 / 6))
    if m.sum() >= 3:
        rows.append((c, float(np.median(pts_d[m])), int(m.sum())))
for c, d, n in rows:
    print("%7.0f Hz  %+6.1f dB  (%d harmonics)" % (c, d, n))
if out_csv:
    with open(out_csv, "w") as fp:
        fp.write("freq_hz,deviation_db\n")
        for c, d, _ in rows: fp.write("%.1f,%.2f\n" % (c, d))
