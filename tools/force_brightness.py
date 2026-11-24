#!/usr/bin/env python3
"""Does the bowed string itself brighten with bow force? (string_lab, no body)

Renders one stroke per force at fixed bow speed and position (string_lab
tone), for each friction model with and without the bow width, and prints the
band levels at 2-4 / 4-8 / 8-16 kHz re 0.5-2 kHz: how much 4x the force
raises them, with the slips per period (1 = Helmholtz motion) at each force.
The KB (§2, §11.5) says log centroid rises linearly with log force; with the
loss shelves the string gives +3/+8/+8 dB on G3 for 4x force (docs/MODEL.md).

    python3 tools/force_brightness.py [build/string_lab] [enhanced classic thermal]
"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

lab = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1] in ("enhanced", "classic", "thermal") else "build/string_lab"
models = [m for m in sys.argv[1:] if m in ("enhanced", "classic", "thermal")] or ["enhanced"]
# name, string, note, bow speed, beta, forces (all Helmholtz at the defaults)
CASES = [("G3", 0, 55, 0.2, 0.08, [0.3, 0.6, 1.2]), ("A4", 2, 69, 0.2, 0.08, [0.15, 0.3, 0.6])]


def bands(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)[int(0.5 * sr):int(1.5 * sr)]
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / sr)
    ref = S[(f >= 500) & (f < 2000)].sum()
    return [10 * np.log10(S[(f >= a) & (f < b)].sum() / ref) for a, b in ((2000, 4000), (4000, 8000), (8000, 16000))]


with tempfile.TemporaryDirectory() as tmp:
    wav = os.path.join(tmp, "t.wav")
    for model in models:
        for width in ("0", "3"):
            for name, s, n, vb, beta, forces in CASES:
                rows = []
                for fb in forces:
                    env = dict(os.environ, LAB_WIDTH=width)
                    out = subprocess.run([lab, "tone", str(s), str(n), str(vb), str(fb), str(beta), model, wav],
                                         env=env, capture_output=True, text=True).stdout
                    slips = out.split("slips/period")[1].split()[0] if "slips/period" in out else "?"
                    rows.append((fb, slips, bands(wav)))
                lo, hi = rows[0][2], rows[-1][2]
                print(f"{model:9s} width {width} {name}: " + "  ".join(f"F {fb}: {sl} slips" for fb, sl, _ in rows) +
                      "   x4 force: %+.1f / %+.1f / %+.1f dB" % tuple(h - l for h, l in zip(hi, lo)))
