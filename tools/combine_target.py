#!/usr/bin/env python3
"""Combine the radiation target the synthetic body is calibrated to.

  below 800 Hz: the CC0 library-mic curve (1/6-oct smoothed; the synth's force
                is within 1 dB of 1/n there, and the library fit pooled
                thousands of vibrato frames)
  above 800 Hz: tools/radiation_target.py's real-vs-synth measurement,
                smoothed to 1/3 octave, cross-faded over 700-900 Hz, rolling
                off at -12 dB/oct above its last band (~9 kHz)

    python3 tools/combine_target.py <library response.csv> <measured.csv> <out.csv> [--from HZ]

measured.csv "-": the library curve alone over the whole range (a first
target for a new instrument, before radiation_target.py can measure one).
--from: the grid's first frequency (default 150 Hz; the cello's starts lower).
--residual=CSV: radiation_target.py's measure of the Modelled output (mode 3)
against the recordings, from a build with this target: its level below
800 Hz, smoothed over an octave, is added to the library curve there (the
cello's bridge force is not 1/n at its low partials, so the 1/n library
curve leaves the low band ~3 dB strong); the body's detail stays the
library's.
"""
import sys
import numpy as np
args = [a for a in sys.argv[1:] if not a.startswith("--")]
f0 = 150.0
residual = None
for a in sys.argv[1:]:
    if a.startswith("--from="): f0 = float(a.split("=", 1)[1])
    if a.startswith("--residual="): residual = a.split("=", 1)[1]
lib = np.genfromtxt(args[0], delimiter=',', names=True)
f = f0 * 2 ** (np.arange(0, int(round(np.log2(150 * 2 ** 6.5 / f0) * 24)) + 1) / 24)  # f0 .. ~13.6 kHz, 1/24 oct
H = np.interp(np.log(f), np.log(lib['freq_hz']), lib['H_mono_smooth_1_6oct_db'])
if residual:
    r = np.genfromtxt(residual, delimiter=',', names=True)
    def octave(fq):
        fq = max(fq, r['freq_hz'].min())   # below the lowest measured band: its level
        w = np.maximum(0, 1 - np.abs(np.log2(r['freq_hz'] / fq)) / 1.0)
        return np.sum(w * r['target_db']) / np.sum(w)
    R = np.array([octave(x) for x in f])
    fade = np.clip((np.log2(1000) - np.log2(f)) / (np.log2(1000) - np.log2(800)), 0, 1)
    H = H + R * fade
if args[1] == "-":
    T = H - H.max()
    with open(args[2], "w") as fp:
        fp.write("freq_hz,target_db\n")
        for a, b in zip(f, T): fp.write("%.1f,%.2f\n" % (a, b))
    for c in (70, 100, 150, 200, 300, 400, 700, 1000, 2000, 3000, 5000, 10000):
        print("%6d Hz %+6.1f dB" % (c, np.interp(np.log(c), np.log(f), T)))
    sys.exit(0)
meas = np.genfromtxt(args[1], delimiter=',', names=True)
mf, mt = meas['freq_hz'], meas['target_db']
# 1/3-octave smoothing of the measured points (log-frequency triangular kernel).
def smooth(fq):
    w = np.maximum(0, 1 - np.abs(np.log2(mf / fq)) / (1 / 3))
    return np.sum(w * mt) / np.sum(w) if w.sum() > 0 else np.nan
M = np.array([smooth(x) for x in f])
last = mf.max()
tail = f > last
M[tail] = np.interp(last, f, M) - 12 * np.log2(f[tail] / last)
M[np.isnan(M)] = 0
# Align levels on 800-1200 Hz, then cross-fade.
band = (f >= 800) & (f <= 1200)
M += np.mean(H[band]) - np.mean(M[band])
x = np.clip((np.log2(f) - np.log2(700)) / (np.log2(900) - np.log2(700)), 0, 1)
T = (1 - x) * H + x * M
T -= T.max()
with open(args[2], "w") as fp:
    fp.write("freq_hz,target_db\n")
    for a, b in zip(f, T): fp.write("%.1f,%.2f\n" % (a, b))
for c in (200, 300, 400, 550, 700, 1000, 1500, 2300, 3000, 4000, 5000, 7000, 10000):
    print("%6d Hz %+6.1f dB" % (c, np.interp(np.log(c), np.log(f), T)))
