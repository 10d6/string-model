#!/usr/bin/env python3
"""Per-partial damping of plucked notes: each partial's decay rate after the pluck, as Q
(= pi f tau, tau the amplitude time constant), real against the synth on the same notes.

A plucked string rings freely, so its partials' decay is the string's own damping on the
instrument: the string, the bridge and body, and the finger on stopped notes (KB §2.3:
Valette-Cuesta, 1/Q = 1/Q_air + 1/Q_visco + 1/Q_thermo; a partial's Q depends on its
frequency). Recordings: the Philharmonia pizz-normal notes (--data) and/or VSCO's
(--vsco). For each partial: the STFT magnitude at n f0 (46 ms window, 10 ms hop), a straight
line fitted to its dB level from 30 ms after its peak over its first 25 dB of decay (while
12 dB above the noise floor; at least 40 ms), the slope -> tau -> Q.

    python3 tools/partial_q.py --data ~/data/phil/wav [--vsco <VSCO Pizz dir>] [--render R]
    python3 tools/partial_q.py --instrument cello --iowa ~/data/iowa/cello/wav/pizz [--dyn mf]

--iowa: the University of Iowa MIS pizzicato files (one string's run per file, tools/iowa.py),
each note rendered on the same string. --instrument cello: the cello's notes and open strings,
and a band below 150 Hz.
"""
import argparse, json, os, re, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iowa
from scipy.io import wavfile
from scipy.signal import stft

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
BANDS = [(150, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000)]


def midi(name):
    m = re.match(r"([A-G])(s?)(\d)$", name)
    return 12 * (int(m.group(3)) + 1) + NAMES[m.group(1)] + (1 if m.group(2) else 0)


def load(p):
    sr, x = wavfile.read(p)
    x = x.astype(float)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x / (np.abs(x).max() + 1e-12)


def partial_qs(x, sr, f0, nmax=16):
    """[(freq, Q)] for partials 1..nmax that decay cleanly."""
    win = int(0.046 * sr)
    f, t, Z = stft(x, sr, nperseg=win, noverlap=win - int(0.01 * sr))
    A = 20 * np.log10(np.abs(Z) + 1e-12)
    floor = np.percentile(A, 20, axis=1)  # per-bin noise floor
    out = []
    for n in range(1, nmax + 1):
        fn = n * f0
        if fn > 9000:
            break
        b = (f > fn * 0.985) & (f < fn * 1.015)
        if not b.any():
            continue
        k = np.where(b)[0]
        lvl = A[k].max(axis=0)
        fl = floor[k].max()
        p = int(np.argmax(lvl[:int(0.3 / 0.01)]))
        if lvl[p] - fl < 25:
            continue
        # The initial decay only: its first 25 dB from the peak, while 12 dB above the floor
        # (a recording's room tail, or a synth's digital silence, must not set the slope).
        s = p + 3
        e = s
        while e < len(lvl) and lvl[e] > max(fl + 12, lvl[p] - 25):
            e += 1
        if e - s < 4:
            continue
        tt, yy = t[s:e], lvl[s:e]
        slope = np.polyfit(tt, yy, 1)[0]  # dB/s, negative
        if slope >= -0.5:
            continue
        tau = 8.686 / -slope
        out.append((fn, np.pi * fn * tau))
    return out


def render(R, note, sr, extra=None):
    with tempfile.TemporaryDirectory() as tmp:
        sp, wp = os.path.join(tmp, "s.json"), os.path.join(tmp, "s.wav")
        json.dump({"patch": "init", "length": 3.5, "params": {"artic": 4, "room.mix": 0, "out.width": 0, **(extra or {})},
                   "events": [{"t": 0.1, "on": note, "vel": 90}, {"t": 3.0, "off": note}]}, open(sp, "w"))
        subprocess.run([R, sp, "--out", wp, "--sr", str(sr)], check=True, capture_output=True)
        return load(wp)


def bands(pairs):
    pairs = np.array(pairs) if pairs else np.zeros((0, 2))
    out = []
    for a, b in BANDS:
        m = (pairs[:, 0] >= a) & (pairs[:, 0] < b) if len(pairs) else np.array([], bool)
        out.append(np.median(pairs[m, 1]) if m.sum() >= 3 else np.nan)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="")
    ap.add_argument("--vsco", default="")
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--json", default="")
    ap.add_argument("--iowa", default="")
    ap.add_argument("--dyn", default="mf", help="Iowa dynamics to use, e.g. mf or pp,mf,ff")
    ap.add_argument("--instrument", default="violin")
    ap.add_argument("--param", action="append", default=[], help="id=value for the synth's renders")
    a = ap.parse_args()
    over = {k: float(v) for k, v in (p.split("=") for p in a.param)}
    cello = a.instrument == "cello"
    opens = (36, 43, 50, 57) if cello else (55, 62, 69, 76)
    nlo, nhi = (36, 84) if cello else (55, 96)
    if cello:
        BANDS.insert(0, (60, 150))
    recs = []
    if a.data:
        for f in sorted(os.listdir(a.data)):
            m = re.match(r"violin_([A-G]s?\d)_025_([a-z-]+)_pizz-normal\.wav$", f)
            if m:
                recs.append(("phil", m.group(1), os.path.join(a.data, f)))
    if a.vsco:
        for f in sorted(os.listdir(a.vsco)):
            m = re.match(r".*_([A-G]s?\d)(?:_[a-z0-9]+)*\.wav$", f.replace("#", "s"))
            if m:
                recs.append(("vsco", m.group(1), os.path.join(a.vsco, f)))
    if a.iowa:
        for f in sorted(os.listdir(a.iowa)):
            info = iowa.parse(f)
            if not info or info[2] not in a.dyn.split(","):
                continue
            for n, st, dyn, t0, t1 in iowa.iowa_notes(os.path.join(a.iowa, f)):
                recs.append(("iowa", (n, st, max(0.0, t0 - 0.05), t1), os.path.join(a.iowa, f)))
    real, synth = {"open": [], "stopped": []}, {"open": [], "stopped": []}
    per = {}
    cache = {}
    for src, name, path in recs:
        extra = {"instrument": 1, **over} if cello else dict(over)
        if src == "iowa":
            n, st, t0, t1 = name
            if path not in cache:
                cache = {path: load(path)}
            sr, x = cache[path]
            x = x[int(t0 * sr):int(t1 * sr)]
            extra["string.select"] = st + 1
            name = f"{n}_s{st}_{t0:.0f}"
        else:
            n = midi(name)
            sr, x = load(path)
        if n < nlo or n > nhi:
            continue
        kind = "open" if n in opens else "stopped"
        f0 = 440 * 2 ** ((n - 69) / 12)
        r = partial_qs(x, sr, f0)
        sr2, y = render(a.render, n, min(sr, 48000), extra)
        s = partial_qs(y, sr2, f0)
        real[kind] += r
        synth[kind] += s
        per[f"{src}_{name}"] = {"real": r, "synth": s}
    print("median partial Q by frequency band (real / synth):")
    print("               " + "".join(f"{f'{lo}-{hi} Hz':>18s}" for lo, hi in BANDS))
    for kind in ("open", "stopped"):
        rb, sb = bands(real[kind]), bands(synth[kind])
        print(f"  {kind:8s}     " + "".join(f"{r:8.0f} / {s:<7.0f}" for r, s in zip(rb, sb)) +
              f"   ({len(real[kind])} / {len(synth[kind])} partials)")
    if a.json:
        json.dump(per, open(a.json, "w"))


if __name__ == "__main__":
    main()
