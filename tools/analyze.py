#!/usr/bin/env python3
"""Look at a render: spectrogram, level and pitch (YIN) in one PNG, plus a
pitch summary per note.  python3 tools/analyze.py render.wav [out.png]"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy import signal
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def yin(x, sr, fmin=150, fmax=3000, hop=256, win=2048, thresh=0.12):
    taumax = int(sr / fmin); taumin = int(sr / fmax)
    out = []
    for i in range(0, len(x) - win - taumax, hop):
        f = x[i:i + win + taumax]
        d = np.array([np.sum((f[:win] - f[t:t + win]) ** 2) for t in range(taumax + 1)])
        cm = d.copy(); cm[0] = 1
        cm[1:] = d[1:] * np.arange(1, taumax + 1) / np.maximum(np.cumsum(d[1:]), 1e-12)
        t = taumin
        while t < taumax and cm[t] > thresh: t += 1
        if t >= taumax or np.sum(f[:win] ** 2) < 1e-7 * win:
            out.append(np.nan); continue
        while t + 1 < taumax and cm[t + 1] < cm[t]: t += 1
        a, b, c = cm[t - 1], cm[t], cm[t + 1]
        den = a - 2 * b + c
        tt = t + (0.5 * (a - c) / den if den != 0 else 0)
        out.append(sr / tt)
    return np.array(out), hop

sr, x = wavfile.read(sys.argv[1])
scale = {np.dtype("int16"): 32768.0, np.dtype("int32"): 2147483648.0}.get(x.dtype, 1.0)
x = x.astype(np.float64) / scale
if x.ndim > 1: x = x.mean(axis=1)
out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].replace(".wav", ".png")
f0, hop = yin(signal.resample_poly(x, 1, 2), sr / 2, hop=128)
tf = np.arange(len(f0)) * hop / (sr / 2)
fig, ax = plt.subplots(3, 1, figsize=(12, 9), sharex=True)
f, t, S = signal.spectrogram(x, sr, nperseg=2048, noverlap=1536)
ax[0].pcolormesh(t, f, 10 * np.log10(S + 1e-14), shading="auto", vmin=-130, vmax=-30, cmap="magma")
ax[0].set_ylim(0, 12000); ax[0].set_ylabel("Hz")
rms = np.sqrt(np.convolve(x ** 2, np.ones(960) / 960, "same"))
ax[1].plot(np.arange(len(x)) / sr, 20 * np.log10(rms + 1e-9)); ax[1].set_ylim(-90, 0); ax[1].set_ylabel("dB")
midi = 69 + 12 * np.log2(f0 / 440)
ax[2].plot(tf, midi, ".", ms=2); ax[2].set_ylabel("MIDI note")
ax[2].grid(True, alpha=0.3)
plt.tight_layout(); plt.savefig(out, dpi=80)
# Per-note summary: segments of steady pitch.
good = ~np.isnan(midi)
r = np.round(midi[good]); c = (midi[good] - r) * 100
for n in np.unique(r):
    m = r == n
    if m.sum() > 20:
        print(f"note {int(n):3d}: {m.sum()*hop/(sr/2):5.2f} s  cents median {np.median(c[m]):+5.1f}  p10..p90 {np.percentile(c[m],10):+5.1f}..{np.percentile(c[m],90):+5.1f}")
print("wrote", out)
