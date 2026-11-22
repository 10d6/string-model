#!/usr/bin/env python3
"""The radiation curve the synth needs, measured against real recordings.

For each VSCO-2-CE solo violin "Arco Vib" note (CC0) at p and f, render the
same note through the synth's Pickup output (raw bridge force) at a matching
dynamic, measure both harmonic spectra, and take real - synth per harmonic.
That difference is body + radiation + the library's mics and room, with no
assumption about the bridge force's spectrum (unlike the 1/n assumption
behind the library-mic IR, KB §12.7). Per-note gains are removed by aligning
each note on its harmonics between 0.5 and 3 kHz; the result is the median
per 1/6-octave band.

    python3 tools/radiation_target.py <vsco Arco Vib dir> [render] [out.csv|-] [output mode]
"""
import glob, json, os, re, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

src = sys.argv[1]
render = sys.argv[2] if len(sys.argv) > 2 else "build/stringmodel_render"
out_csv = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != "-" else None
# Optional 4th argument: render through this output mode instead of the
# Pickup (2), e.g. 3 (Modelled) to check what's left after calibration.
mode = int(sys.argv[4]) if len(sys.argv) > 4 else 2
names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

def harmonics(x, sr, f0, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18))
    f = np.fft.rfftfreq(1 << 18, 1 / sr)
    out = {}
    for n in range(1, 200):
        fn = n * f0
        if fn > 14000: break
        band = (f > fn * 0.975) & (f < fn * 1.025)   # vibrato-wide
        out[n] = 10 * np.log10(np.sum(S[band] ** 2) + 1e-20)  # energy in the band
    return out

def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.ndim > 1: x = x.mean(axis=1)
    return sr, x / (np.abs(x).max() + 1e-12)

pf, pd = [], []
with tempfile.TemporaryDirectory() as tmp:
    for path in sorted(glob.glob(os.path.join(src, "*.wav"))):
        m = re.search(r"_([A-G])(\d)_([pf])\.wav$", path)
        if not m: continue
        note = 12 * (int(m.group(2)) + 1) + names[m.group(1)]
        if note < 55: continue                     # below the violin's G3
        dyn = m.group(3)
        f0 = 440 * 2 ** ((note - 69) / 12)
        sr, x = load(path)
        # The steady part: from 0.4 s to 1.6 s, or the middle of shorter notes.
        dur = len(x) / sr
        real = harmonics(x, sr, f0, min(0.4, dur * 0.25), min(1.6, dur * 0.75))
        scen = {"patch": "init", "length": 2.0, "params": {"out.mode": mode, "room.mix": 0},
                "events": [{"t": 0, "on": note, "vel": 45 if dyn == "p" else 110}, {"t": 1.8, "off": note}]}
        sp = os.path.join(tmp, "s.json"); wp = os.path.join(tmp, "s.wav")
        json.dump(scen, open(sp, "w"))
        subprocess.run([render, sp, "--out", wp], capture_output=True, check=True)
        sr2, y = load(wp)
        ours = harmonics(y, sr2, f0, 0.5, 1.7)
        ns = [n for n in real if n in ours]
        diff = np.array([real[n] - ours[n] for n in ns]); fr = np.array([n * f0 for n in ns])
        mid = (fr >= 500) & (fr <= 3000)
        if mid.sum() < 2: mid = fr >= 0
        diff -= np.median(diff[mid])
        pf.extend(fr); pd.extend(diff)
pf = np.array(pf); pd = np.array(pd)
centres = 1000 * 2 ** (np.arange(-18, 20) / 6)
rows = []
for c in centres:
    m = (pf >= c * 2 ** (-1 / 12)) & (pf < c * 2 ** (1 / 12))
    if m.sum() >= 4:
        rows.append((c, float(np.median(pd[m])), int(m.sum())))
for c, d, n in rows:
    print("%7.0f Hz  %+6.1f dB  (%d)" % (c, d, n))
if out_csv:
    with open(out_csv, "w") as fp:
        fp.write("freq_hz,target_db\n")
        for c, d, _ in rows: fp.write("%.1f,%.2f\n" % (c, d))
