"""Notes in the University of Iowa MIS recordings (theremin.music.uiowa.edu,
"free of restrictions"): each file holds one string's chromatic run, e.g.
Cello.pizz.mf.sulG.G2Gb3.wav. iowa_notes() finds each note by its pitch and
returns (midi, string index, dynamic, onset s, end s) for one file.
Used by partial_q.py and other tools (--iowa DIR). Keep the recordings
outside the repo."""
import re
import numpy as np
from scipy import signal
from scipy.io import wavfile

_N = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
STRINGS = {"cello": "CGDA", "violin": "GDAE", "viola": "CGDA"}


def midi(name):
    m = re.match(r"([A-G])(b|#|s)?(\d)$", name)
    acc = {"b": -1, "#": 1, "s": 1}.get(m.group(2), 0)
    return 12 * (int(m.group(3)) + 1) + _N[m.group(1)] + acc


def parse(path):
    """(instrument, technique, dynamic, string letter, lowest, highest) from a file name."""
    m = re.search(r"(\w+)\.(arco|pizz)\.(pp|mf|ff)\.sul([A-G])\.([A-G]b?\d)([A-G]b?\d)", path)
    if not m:
        return None
    return m.group(1).lower(), m.group(2), m.group(3), m.group(4), midi(m.group(5)), midi(m.group(6))


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x / (np.abs(x).max() + 1e-12)


def _pitch(x, sr, fmin=55.0, fmax=1100.0, hop=0.01):
    """Frame-wise f0 (Hz, 0 if unvoiced) by normalised autocorrelation, and level (dB)."""
    q = max(1, int(sr // 24000))
    y = signal.resample_poly(x, 1, q)
    fs = sr / q
    n = int(hop * fs)
    w = int(3.0 / fmin * fs)
    lo, hi = int(fs / fmax), int(fs / fmin)
    f0, lv = [], []
    for i in range(0, len(y) - w - hi, n):
        seg = y[i:i + w + hi]
        a = seg[:w]
        e0 = np.dot(a, a)
        lv.append(10 * np.log10(e0 / w + 1e-20))
        if e0 <= 0:
            f0.append(0.0)
            continue
        c = np.correlate(seg, a, "valid")[lo:hi + 1]
        e1 = np.convolve(seg ** 2, np.ones(w), "valid")[lo:hi + 1]
        r = c / np.sqrt(e0 * e1 + 1e-20)
        k = int(np.argmax(r))
        # a multiple of the period: a lag 1/2, 1/3 or 1/4 as long correlates
        # almost as well (the shortest such wins)
        for dv in (4, 3, 2):
            h = int(round((lo + k) / dv)) - lo
            if h >= 1 and r[h - 1:h + 2].max() > 0.85 * r[k]:
                k = h - 1 + int(np.argmax(r[h - 1:h + 2]))
                break
        d = 0.0
        if 0 < k < len(r) - 1:  # parabolic interpolation of the peak
            den = r[k - 1] - 2 * r[k] + r[k + 1]
            d = 0.5 * (r[k - 1] - r[k + 1]) / den if den < 0 else 0.0
        f0.append(fs / (lo + k + d) if r[k] > 0.6 else 0.0)
    return np.array(f0), np.array(lv), hop


def iowa_notes(path):
    info = parse(path)
    if not info:
        return []
    inst, tech, dyn, sletter, lo, hi = info
    s = STRINGS.get(inst, "CGDA").index(sletter)
    sr, x = load(path)
    f0, lv, hop = _pitch(x, sr)
    floor = np.percentile(lv, 10)
    with np.errstate(divide="ignore"):
        m = np.where(f0 > 0, 69 + 12 * np.log2(f0 / 440.0), -99)
    out, t_from = [], 0
    for n in range(lo, hi + 1):
        ok = (np.abs(m - n) < 0.4) & (lv > floor + 20)
        ok[:t_from] = False
        idx = np.where(ok)[0]
        if len(idx) < 5:
            continue
        a = idx[0]
        b = a
        while b + 1 < len(ok) and (ok[b + 1] or (b + 2 < len(ok) and ok[b + 2])):
            b += 1
        if b - a < 5:
            continue
        # onset: the level's rise just before the pitch is found
        s0 = max(t_from, a - 30)
        on = s0 + int(np.argmax(np.diff(lv[s0:a + 3]))) if a + 3 > s0 + 1 else a
        # end: where the next note starts, or the level reaches the floor
        e = b
        while e + 1 < len(lv) and lv[e + 1] > floor + 6 and not (abs(m[e + 1] - n) > 0.6 and m[e + 1] > 0 and lv[e + 1] > lv[e] + 6):
            e += 1
        out.append((n, s, dyn, on * hop, e * hop))
        t_from = b + 1
    return out


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        notes = iowa_notes(p)
        info = parse(p)
        print(p.split("/")[-1], f"{len(notes)} of {info[5] - info[4] + 1} notes:",
              " ".join(f"{n}@{t:.1f}-{e:.1f}" for n, s, d, t, e in notes))
