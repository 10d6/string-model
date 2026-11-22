#!/usr/bin/env python3
"""Natural and artificial harmonics: cents vs equal temperament (Intonation
Style 0), and how far the fundamental below is down. Natural: A5 D6 E6 A6
D5 G5; artificial: B5 F#6 B6 C#6. Expect within a few cents and the
fundamental 30+ dB down.   python3 tools/harmonics_tuning.py"""
import numpy as np
from evalkit import render, mtof, cents

notes = [81, 86, 88, 93, 74, 83, 90, 95, 79, 85]
ev, t = [], 0
for n in notes:
    ev += [{"t": round(t, 2), "on": n, "vel": 85}, {"t": round(t + 0.8, 2), "off": n}]
    t += 0.9
sr, x = render({"patch": "init", "length": t + 1.5,
                "params": {"room.mix": 0, "artic": 5, "vib.depth": 0, "finger.style": 0}, "events": ev})
out = []
for i, n in enumerate(notes):
    seg = x[int((i * 0.9 + 0.4) * sr):int((i * 0.9 + 0.8) * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 17))
    f = np.fft.rfftfreq(1 << 17, 1 / sr)
    w = mtof(n)
    m = (f > w * 0.9) & (f < w * 1.1)
    lo = (f > 100) & (f < w * 0.9)
    out.append(f"{n}: {cents(f[m][np.argmax(S[m])], w):+.0f} ct / {20 * np.log10(S[lo].max() / S[m].max()):+.0f} dB")
print("\n".join(out))
