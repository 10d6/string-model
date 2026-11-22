#!/usr/bin/env python3
"""Per-note pitch of any scenario: for each note-on, cents vs equal
temperament over a short window after the onset, and the strongest peak
below the note re the note (a broken, sub-harmonic or raucous note shows
up as a strong peak below). Used for spiccato/sautillé/articulation checks.
python3 tools/note_pitch.py scenario.json [render.wav] [--start 0.02 --len 0.08]"""
import argparse
import json
import numpy as np
from evalkit import render, read, mtof, cents

ap = argparse.ArgumentParser()
ap.add_argument("scenario")
ap.add_argument("wav", nargs="?")
ap.add_argument("--start", type=float, default=0.02)
ap.add_argument("--len", type=float, default=0.08)
a = ap.parse_args()
sc = json.load(open(a.scenario))
sr, x = read(a.wav) if a.wav else render(sc)
spb = 60.0 / sc.get("transport", {}).get("bpm", 120)
for e in [e for e in sc["events"] if "on" in e]:
    t = e["t"] if "t" in e else e["beat"] * spb
    seg = x[int((t + a.start) * sr):int((t + a.start + a.len) * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1 / sr)
    w = mtof(e["on"])
    m = (f > w * 0.85) & (f < w * 1.15)
    lo = (f > 150) & (f < w * 0.85)
    sub = 20 * np.log10((S[lo].max() if lo.any() else 1e-12) / S[m].max())
    print(f"{t:7.3f}s  note {e['on']}: {cents(f[m][np.argmax(S[m])], w):+4.0f} ct, strongest below {sub:+.0f} dB")
