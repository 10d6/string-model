#!/usr/bin/env python3
"""How different does the synth sound from a real violin? A perceptual distance, without listeners.

Ported from 10d6/piano (tools/measure/perceptual.py, docs/TESTING.md), adapted to bowed notes:

  1. Both signals mono (the synth's L+R), 44.1 kHz, each note level-matched to its recording
     over the held part (so this measures how it sounds, not how loud: see --levels for that).
  2. A gammatone filterbank, one band per ERB (the ear's frequency resolution), 40 Hz-15 kHz
     (the recordings are 96 kbit/s MP3s: their top octave is cut at 16-20 kHz).
  3. Each band's power, smoothed over 8 ms and read every 2 ms.
  4. Specific loudness per band (Zwicker's form, above the threshold of hearing, a full-scale
     sine at 100 dB SPL).
  5. The difference per band, less 5 % of the louder (about 1 dB, the ear's level
     discrimination).

Windows, each aligned on its own signal's onset (or release):
  attack   0-80 ms after the onset, frame by frame
  early    80-250 ms, frame by frame (the bow settles; vibrato hasn't started)
  sustain  250 ms to 50 ms before the release, as each band's mean loudness: vibrato in the
           recording and in the synth won't line up frame by frame, and the ear judges a
           settled tone by its spectrum
  release  0-300 ms from the release (where the level has fallen 10 dB from the peak), frame
           by frame
The distance in a window is the audible difference over the recording's loudness there, in %;
the score is the mean of the attack, early and sustain windows a note has. The release is
reported but not scored: two takes of one note differ by ~50 % there (the yardstick), since a
player ends a short and a long note differently. Signed low (< 500 Hz), mid (0.5-4 kHz) and
high (> 4 kHz) parts say which way it's wrong.

Yardsticks, from the recordings themselves (--yardstick):
  take     the same note and dynamic, the 0.5 s against the 1 s recording (another take: how
           much a player differs from themselves)
  dynamic  the same note and length, one dynamic step up (level-matched)

The recordings: the Philharmonia Orchestra's violin samples (free to use; not to be shared as
samples: never commit them), decoded to WAV in --data. Mapping of dynamics to key velocity:
VEL below.

    python3 tools/perceptual.py --data ~/data/phil/wav [--notes G3,A4] [--dyn piano,forte]
            [--dur 05,1] [--param out.mode=2] [--json out.json]
    python3 tools/perceptual.py --data … --yardstick
    python3 tools/perceptual.py --compare a.json b.json [--by-note]   (also per dynamic)
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from multiprocessing import Pool

import numpy as np
import scipy.signal as sig
from scipy.io import wavfile

SR = 44100
HOP = 0.002
SMOOTH = 0.008
TOL = 0.05
REGIONS = [("low", 0, 500), ("mid", 500, 4000), ("high", 4000, 1e9)]
DYNS = ["pianissimo", "piano", "mezzo-piano", "mezzo-forte", "forte", "fortissimo"]
VEL = {"pianissimo": 20, "piano": 40, "mezzo-piano": 60, "mezzo-forte": 80, "forte": 100, "fortissimo": 120}
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
LEAD = 0.2   # s of silence before the synth's note


def midi(name):
    m = re.match(r"([A-G])(s?)(\d)$", name)
    return 12 * (int(m.group(3)) + 1) + NAMES[m.group(1)] + (1 if m.group(2) else 0)


def erb_centres(lo=40.0, hi=15000.0, step=1.0):
    e = lambda f: 21.4 * np.log10(1 + 0.00437 * f)
    inv = lambda E: (10 ** (E / 21.4) - 1) / 0.00437
    return inv(np.arange(e(lo), e(hi) + 1e-9, step))


CENTRES = erb_centres()


def band_power(x, fc):
    """A 4th-order gammatone band at fc: shift down by fc, four one-pole low-passes; |y|^2."""
    erb = 24.7 * (4.37e-3 * fc + 1)
    a = np.exp(-2 * np.pi * 1.019 * erb / SR)
    y = x * np.exp(-2j * np.pi * fc * np.arange(len(x)) / SR)
    for _ in range(4):
        y = sig.lfilter([1 - a], [1, -a], y)
    return np.abs(y) ** 2 / 2


def band_powers(x):
    """Each band's power per frame: (frames, bands); a full-scale sine = 0.5."""
    step, L = int(HOP * SR), int(SMOOTH * SR)
    out = []
    for fc in CENTRES:
        c = np.concatenate([[0.0], np.cumsum(band_power(x, fc))])
        idx = np.arange(0, len(x) - L, step)
        out.append((c[idx + L] - c[idx]) / L)
    return np.array(out).T


def threshold_db(f):
    k = f / 1000.0
    return 3.64 * k ** -0.8 - 6.5 * np.exp(-0.6 * (k - 3.3) ** 2) + 1e-3 * k ** 4


THR = 10 ** (threshold_db(CENTRES) / 10)


def specific_loudness(P, spl=100.0):
    E = P * 2 * 10 ** (spl / 10)
    return np.maximum(0.08 * THR ** 0.23 * ((0.5 + 0.5 * E / THR) ** 0.23 - 1), 0)


def envelope_marks(P):
    """Onset (first frame within 25 dB of the peak, 4 ms earlier) and release (last frame
    within 10 dB of the peak), in frames, from the total band power."""
    tot = 10 * np.log10(P.sum(axis=1) + 1e-20)
    mx = tot.max()
    on = max(int(np.argmax(tot > mx - 25)) - 2, 0)
    rel = int(np.where(tot > mx - 10)[0][-1])
    return on, rel


def analyse(x):
    """Band powers and envelope marks of a mono signal."""
    P = band_powers(np.asarray(x, float))
    on, rel = envelope_marks(P)
    return P, on, rel


def matched(Pr, rr, Pm, rm):
    """The synth's band powers scaled so its held part's total power equals the recording's."""
    (onr, relr), (onm, relm) = rr, rm
    er = Pr[onr:max(relr, onr + 1)].sum(axis=1).mean()
    em = Pm[onm:max(relm, onm + 1)].sum(axis=1).mean()
    return Pm * (er / max(em, 1e-30))


def distance(Pr, rr, Pm, rm):
    """The windows' distances (%) and signed parts, Pm level-matched to Pr."""
    Pm = matched(Pr, rr, Pm, rm)
    Nr, Nm = specific_loudness(Pr), specific_loudness(Pm)
    (onr, relr), (onm, relm) = rr, rm
    fr = lambda s: int(round(s / HOP))
    res, scores = {}, []

    def framewise(name, a0, b0, a1, b1):
        n = min(b0 - a0, b1 - a1, len(Nr) - a0, len(Nm) - a1)
        if n <= 2:
            return
        R, M = Nr[a0:a0 + n], Nm[a1:a1 + n]
        add(name, R, M)

    def add(name, R, M):
        ref = R.sum()
        if ref <= 0:
            return
        diff = M - R
        aud = np.maximum(np.abs(diff) - TOL * np.maximum(R, M), 0)
        d = 100 * aud.sum() / ref
        res[name] = round(float(d), 2)
        scores.append(d)
        for rn, f0, f1 in REGIONS:
            b = (CENTRES >= f0) & (CENTRES < f1)
            res[f"{name}_{rn}"] = round(float(100 * diff[..., b].sum() / ref), 2)

    framewise("attack", onr, onr + fr(0.08), onm, onm + fr(0.08))
    framewise("early", onr + fr(0.08), onr + fr(0.25), onm + fr(0.08), onm + fr(0.25))
    s0r, s1r = onr + fr(0.25), relr - fr(0.05)
    s0m, s1m = onm + fr(0.25), relm - fr(0.05)
    if s1r - s0r > fr(0.1) and s1m - s0m > fr(0.1):
        add("sustain", Nr[s0r:s1r].mean(axis=0, keepdims=True), Nm[s0m:s1m].mean(axis=0, keepdims=True))
    n_scored = len(scores)
    framewise("release", relr, relr + fr(0.3), relm, relm + fr(0.3))
    # The release isn't in the score: two takes of one note already differ by ~50 % there (a
    # player ends a 0.5 s and a 1 s note differently), so it can't tell a fault from a habit.
    res["score"] = round(float(np.mean(scores[:n_scored])), 2) if n_scored else None
    return res


def shape(P, on, rel):
    """Two things the score can't see, reported beside it:
    (ring, the vibrato's steady centre line, is in ring())
    rise    ms from the onset until the level is within 3 dB of the sustain's (soft bowed
            notes swell: the recordings take 150-190 ms at pp-mp)
    shimmer the sustain's level fluctuation per band (std over time of 20 ms-smoothed band
            power, dB), median over 1-8 kHz: vibrato sweeping partials through the body's
            peaks, and the bow's own unsteadiness (the time-averaged sustain hides both)"""
    tot = 10 * np.log10(P.sum(axis=1) + 1e-20)
    s0, s1 = on + int(0.25 / HOP), rel - int(0.05 / HOP)
    out = {}
    if s1 - s0 > int(0.1 / HOP):
        sus = np.median(tot[s0:s1])
        out["rise"] = round(float(np.argmax(tot[on:] > sus - 3) * HOP * 1000), 1)
    a0 = on + int(0.3 / HOP)
    if s1 - a0 > int(0.2 / HOP):
        L = 10 * np.log10(P[a0:s1] + 1e-20)
        k = 10
        Ls = np.array([np.convolve(L[:, b], np.ones(k) / k, "valid") for b in range(L.shape[1])]).T
        band = (CENTRES > 1000) & (CENTRES < 8000)
        out["shimmer"] = round(float(np.median(Ls.std(axis=0)[band])), 2)
    return out


def ring(x, P, on, rel, f0):
    """How far each partial's centre pitch stands above its vibrato's smear (dB): the max of the
    long-term spectrum within 3 cents of the partial's centre over the median within 20 cents,
    partials 2-8, median. A vibrato around a fixed centre leaves a steady line there, heard as a
    ring like beating; the recordings' wanders (~7 dB)."""
    a, b = on * HOP + 0.4, rel * HOP - 0.05
    if b - a < 0.7:
        return None
    seg = x[int(a * SR):int(b * SR)]
    N = 1 << 19
    S = 10 * np.log10(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N)) ** 2 + 1e-20)
    f = np.fft.rfftfreq(N, 1 / SR)
    out = []
    for k in range(2, 9):
        if k * f0 > 9000:
            break
        c = 1200 * np.log2(np.maximum(f, 1) / (k * f0))
        w = (c > -60) & (c < 60)
        pw = 10 ** (S[w] / 10)
        cc = (c[w] * pw).sum() / pw.sum()
        out.append(S[np.abs(c - cc) < 3].max() - np.median(S[np.abs(c - cc) < 20]))
    return round(float(np.median(out)), 2) if out else None


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.dtype != float or np.abs(x).max() > 2:
        x = x / 32768.0
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR:
        x = sig.resample_poly(x, SR, sr)
    return x


def recordings(data, notes, dyns, durs):
    out = []
    for f in sorted(os.listdir(data)):
        m = re.match(r"violin_([A-G]s?\d)_(025|05|1|15)_([a-z-]+)_arco-normal\.wav$", f)
        if not m:
            continue
        note, dur, dyn = m.groups()
        if dyn not in VEL or (notes and note not in notes) or (dyns and dyn not in dyns) or (durs and dur not in durs):
            continue
        out.append((note, dur, dyn, os.path.join(data, f)))
    return out


def render(job):
    render_tool, params, note, dyn, held, tmp = job
    key = f"{note}_{dyn}_{held:.3f}"
    sp, wp = os.path.join(tmp, key + ".json"), os.path.join(tmp, key + ".wav")
    p = {"room.mix": 20}
    p.update(params)
    json.dump({"patch": "init", "length": LEAD + held + 1.2, "params": p,
               "events": [{"t": LEAD, "on": midi(note), "vel": VEL[dyn]}, {"t": LEAD + held, "off": midi(note)}]},
              open(sp, "w"))
    subprocess.run([render_tool, sp, "--out", wp, "--sr", str(SR)], check=True, capture_output=True)
    return load(wp)


def one(job):
    """(id, result) for one recording: analyse it, render the synth to match, compare."""
    rec_path, render_tool, params, note, dur, dyn, tmp = job
    x = load(rec_path)
    Pr, onr, relr = analyse(x)
    held = max((relr - onr) * HOP, 0.1)
    y = render((render_tool, params, note, dyn, held, tmp))
    Pm, onm, relm = analyse(y)
    r = distance(Pr, (onr, relr), Pm, (onm, relm))
    for k, v in shape(Pr, onr, relr).items():
        r[k + "_rec"] = v
    for k, v in shape(Pm, onm, relm).items():
        r[k + "_syn"] = v
    f0 = 440 * 2 ** ((midi(note) - 69) / 12)
    if midi(note) not in (55, 62, 69, 76):  # open strings don't vibrate in the synth
        a, b = ring(x, Pr, onr, relr, f0), ring(y, Pm, onm, relm, f0)
        if a is not None and b is not None:
            r["ring_rec"], r["ring_syn"] = a, b
    return f"{note}_{dur}_{dyn}", r


def yard(job):
    a, b, kind, name = job
    xa, xb = load(a), load(b)
    Pa, oa, ra = analyse(xa)
    Pb, ob, rb = analyse(xb)
    return kind, name, distance(Pa, (oa, ra), Pb, (ob, rb))


def summarise(results, label=""):
    keys = ["score", "attack", "early", "sustain", "release"]
    vals = {k: [r[k] for r in results.values() if r.get(k) is not None] for k in keys}
    line = "  ".join(f"{k} {np.median(v):5.1f}" for k, v in vals.items() if v)
    print(f"{label}median over {len(results)} notes:  {line}")
    for k in ("rise", "shimmer", "ring"):
        rv = [r[k + "_rec"] for r in results.values() if k + "_rec" in r and k + "_syn" in r]
        sv = [r[k + "_syn"] for r in results.values() if k + "_rec" in r and k + "_syn" in r]
        if rv:
            unit = "ms" if k == "rise" else "dB"
            print(f"   {k:8s} recordings {np.median(rv):6.1f} {unit}, synth {np.median(sv):6.1f} {unit} (median, {len(rv)} notes)")
    for w in ["attack", "early", "sustain", "release"]:
        parts = [np.median([r[f'{w}_{rn}'] for r in results.values() if f'{w}_{rn}' in r]) for rn, *_ in REGIONS
                 if any(f'{w}_{rn}' in r for r in results.values())]
        if parts:
            print(f"   {w:8s} signed low/mid/high {parts[0]:+6.1f} {parts[1]:+6.1f} {parts[2]:+6.1f}  (+ = synth louder)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=os.environ.get("PHIL_WAV", os.path.expanduser("~/data/phil/wav")))
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--notes", default="")
    ap.add_argument("--dyn", default="")
    ap.add_argument("--dur", default="05,1")
    ap.add_argument("--param", action="append", default=[], help="synth parameter id=value (repeatable)")
    ap.add_argument("--json", default="")
    ap.add_argument("--yardstick", action="store_true")
    ap.add_argument("--compare", nargs=2)
    ap.add_argument("--by-note", action="store_true", help="with --compare: also per note name")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()

    if a.compare:
        A, B = (json.load(open(p)) for p in a.compare)
        common = sorted(set(A) & set(B))
        summarise({k: A[k] for k in common}, "A: ")
        summarise({k: B[k] for k in common}, "B: ")
        d = [B[k]["score"] - A[k]["score"] for k in common if A[k]["score"] is not None and B[k]["score"] is not None]
        print(f"B - A per note: median {np.median(d):+.2f}, B nearer on {sum(x < 0 for x in d)} of {len(d)}")
        # Where the change lands: per dynamic, then per note (a fix often
        # helps one group and hurts another; the overall median hides it).
        def group(key):
            g = {}
            for k in common:
                if A[k]["score"] is not None and B[k]["score"] is not None:
                    g.setdefault(key(k), []).append(k)
            return g
        med = lambda v: np.median(v) if v else float("nan")
        print("   by dynamic         rise rec / A / B ms   attack B-A   score B-A   B nearer")
        for dy, ks in sorted(group(lambda k: k.split("_", 2)[2]).items(), key=lambda kv: DYNS.index(kv[0]) if kv[0] in DYNS else 99):
            r = lambda J, f: med([J[k][f] for k in ks if f in J[k]])
            print(f"   {dy:16s} {r(A, 'rise_rec'):6.0f} / {r(A, 'rise_syn'):4.0f} / {r(B, 'rise_syn'):4.0f}"
                  f"   {med([B[k]['attack'] - A[k]['attack'] for k in ks]):+8.2f}   {med([B[k]['score'] - A[k]['score'] for k in ks]):+8.2f}"
                  f"   {sum(B[k]['score'] < A[k]['score'] for k in ks):4d}/{len(ks)}")
        if "--by-note" in sys.argv:
            print("   by note            score B-A   B nearer")
            for nm, ks in sorted(group(lambda k: k.split("_")[0]).items()):
                print(f"   {nm:16s} {med([B[k]['score'] - A[k]['score'] for k in ks]):+8.2f}   {sum(B[k]['score'] < A[k]['score'] for k in ks):4d}/{len(ks)}")
        return

    notes = set(a.notes.split(",")) if a.notes else None
    dyns = set(a.dyn.split(",")) if a.dyn else None
    durs = set(a.dur.split(",")) if a.dur else None
    recs = recordings(a.data, notes, dyns, durs)
    if not recs:
        sys.exit(f"no recordings in {a.data}")

    if a.yardstick:
        allrecs = {(n, d, y): p for n, d, y, p in recordings(a.data, notes, dyns, None)}
        jobs = []
        for (n, d, y), p in allrecs.items():
            if d == "05" and (n, "1", y) in allrecs:
                jobs.append((p, allrecs[(n, "1", y)], "take", f"{n}_{y}"))
            if durs and d not in durs:
                continue
            i = DYNS.index(y)
            if i + 1 < len(DYNS) and (n, d, DYNS[i + 1]) in allrecs:
                jobs.append((p, allrecs[(n, d, DYNS[i + 1])], "dynamic", f"{n}_{d}_{y}"))
        with Pool(a.jobs) as pool:
            out = pool.map(yard, jobs)
        for kind in ("take", "dynamic"):
            summarise({nm: r for k, nm, r in out if k == kind}, f"yardstick {kind}: ")
        return

    params = {}
    for p in a.param:
        k, v = p.split("=", 1)
        params[k] = float(v)
    with tempfile.TemporaryDirectory() as tmp:
        jobs = [(p, os.path.abspath(a.render), params, n, d, y, tmp) for n, d, y, p in recs]
        with Pool(a.jobs) as pool:
            out = dict(pool.map(one, jobs))
    if a.json:  # first: a closed pipe mustn't lose the run
        json.dump(out, open(a.json, "w"), indent=1)
    summarise(out)


if __name__ == "__main__":
    main()
