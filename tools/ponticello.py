#!/usr/bin/env python3
"""Sul ponticello through the synth: held notes at Contact Point 0 / 0.5 /
0.85 (the preset) / 1, per dynamic. Reports, per contact point:
  lvl   level re the same note at contact 0 (dB)
  h1    the 1st partial re the whole tone (dB): multiple slipping weakens it
  dom   the strongest partial (KB §1.2, §11.2: n slips, the n-th partial)
  hnr   harmonic-to-noise ratio, 80 Hz-12 kHz (dB): low = raucous, "crunchy"
  ct    pitch (cents): open strings carry the bow allowance (contactForceOct)
Use after changing the contact law (Performer::contactForceOct), the force
law or friction.  python3 tools/ponticello.py [--instrument cello]   (RENDER=... to pick a build)"""
import sys
import numpy as np
from evalkit import render, pitch, cents, mtof, level_db

cello = "cello" in sys.argv[1:]
NOTES = (36, 43, 50, 57, 64) if cello else (55, 62, 69, 76, 83)
CONTACTS = (0.0, 0.5, 0.85, 1.0)
HOLD, GAP = 2.2, 0.4


def partials(seg, sr, f0):
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    band = (f > 80) & (f < 12000)
    harm = np.zeros_like(band)
    for k in range(1, int(12000 / f0) + 1):
        harm |= np.abs(f - k * f0) < max(6, 0.025 * f0)
    amps = [S[np.abs(f - k * f0) < max(6, 0.025 * f0)].sum() for k in range(1, 21)]
    tot = S[band].sum()
    hnr = 10 * np.log10(S[band & harm].sum() / S[band & ~harm].sum())
    return 10 * np.log10(amps[0] / tot), int(np.argmax(amps)) + 1, hnr


for dyn in (0.25, 0.55, 0.85):
    print(f"dynamics {dyn}")
    ref = {}
    for c in CONTACTS:
        ev, t = [], 0.0
        for n in NOTES:
            ev += [{"t": t, "on": n, "vel": 80}, {"t": t + HOLD, "off": n}]
            t += HOLD + GAP
        sr, x = render({"patch": "init", "length": t + 0.5,
                        "params": {"room.mix": 0, "vib.depth": 0, "bow.auto": 0, "finger.style": 0, "finger.avoidOpen": 0,
                                   "bow.dynamics": dyn, "bow.contact": c, "instrument": 1 if cello else 0},
                        "events": ev})
        row = []
        for i, n in enumerate(NOTES):
            seg = x[int((i * (HOLD + GAP) + 0.8) * sr):int((i * (HOLD + GAP) + 2.0) * sr)]
            f = pitch(seg, sr, mtof(n), harmonics=(1, 2, 3, 4))
            lvl = level_db(seg)
            ref.setdefault(n, lvl)
            h1, dom, hnr = partials(seg, sr, f)
            row.append(f"{n}: lvl {lvl - ref[n]:+5.1f} h1 {h1:6.1f} dom {dom:2d} hnr {hnr:4.1f} ct {cents(f, mtof(n)):+5.1f}")
        print(f"  contact {c:4.2f}  " + " | ".join(row))
