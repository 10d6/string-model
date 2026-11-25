#!/usr/bin/env python3
"""Attacks and sustains by register: the synth against recordings, note by note.

Why: the owner hears the cello's attacks as soft and its sustains at the top and bottom of the
range as "not quite there". tools/perceptual.py says how far off a note is; this says *what*
is off, in terms a model change can aim at. Each recording is matched by a synth note (same
pitch, length and dynamic), and both are measured the same way:

Attack (from each signal's own onset: the first frame within 25 dB of the peak, as perceptual.py)
  rise     ms until the level (10 ms windows) is within 3 dB of the sustain's median
  rise_hf  the same for the level above 2 kHz: a real bow's onset has its full bandwidth within
           ~20 ms, while a model whose brightness follows its swelling force opens like a filter
  early    the first 60 ms' level re the sustain (dB): how far under its note the attack starts
  scratch  how aperiodic the first 80 ms are (dB, noise re periodic power): each period is
           correlated with the next (lag searched, 4x upsampled), noise/periodic = (1 - r) / r,
           median over the window. `scratch_hf` the same above 2 kHz, where the bow's grit is.
  bright   the attack's level above 2 kHz re its total, less the sustain's (dB): + = the
           attack carries extra treble (a catch, a scratch), - = it starts dull
Sustain (0.3 s after the onset to 50 ms before the release, at most 0.8 s)
  noise    the same aperiodicity, full band and above 2 kHz: how clean the held tone is
  h1 h2-3 h4-7 h8-15 h16+   power in bands one f0 wide around each harmonic, grouped by
           harmonic number, re the sum (dB): the source's spectrum, what a register's tone is
  cents    the held pitch re equal temperament at A 440 (median of the first harmonics' peaks)
Relative measures (scratch, bright, rise, early) don't depend on the microphone; the harmonic
groups and the noise level do, so read them against the recordings' own spread.

References (never committed: measured only):
  --phil DIR     the Philharmonia samples decoded to WAV (cello_<note>_<len>_<dyn>_arco-normal.wav,
                 or violin_… with --instrument violin). Vibrato from the onset, a studio room.
  --bigcat DIR   the Karoryfer x bigcat cello (CC0; github.com/sfzinstruments/karoryfer-bigcat.cello):
                 Samples/sus/<name>_<p|mp|mf|f>_<d|g>.wav, names an octave low, no vibrato,
                 close-miked, cut mid-bow (no release). The synth plays it with no vibrato.

    python3 tools/attack_sustain.py --phil ~/data/phil-cello/wav [--bigcat ~/data/bigcat]
            [--notes C2,A4] [--dyn pianissimo,forte] [--dur 1,15] [--param id=value]
            [--sr 48000] [--json out.json] [--clips DIR]
    python3 tools/attack_sustain.py --compare a.json b.json      (two builds or settings)

--sr is the rate the synth renders at (48 kHz: the strings run at 96 kHz; at 192 kHz they run
at 192 kHz, a test of the junction's sampling, KB §6.4). --clips writes, per register and
dynamic group, one note as recording then synth (level-matched), for listening only.
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
import soundfile as sf

SR = 44100
LEAD = 0.2
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
PHIL_DYN = {"pianissimo": ("pp", 20), "piano": ("p", 40), "mezzo-piano": ("mp", 60),
            "mezzo-forte": ("mf", 80), "forte": ("f", 100), "fortissimo": ("ff", 120)}
BIGCAT_DYN = {"p": 40, "mp": 60, "mf": 80, "f": 100}
SOFT = {"pp", "p", "mp"}
HGROUPS = [("h1", 1, 1), ("h2-3", 2, 3), ("h4-7", 4, 7), ("h8-15", 8, 15), ("h16+", 16, 200)]
REGISTERS = {
    "cello": [("C2-F#2 C string", 36, 42), ("G2-B2", 43, 47), ("C3-B3", 48, 59), ("C4-B4", 60, 71),
              ("C5-C6 thumb", 72, 84)],
    "violin": [("G3-B3", 55, 59), ("C4-B4", 60, 71), ("C5-B5", 72, 83), ("C6-B6", 84, 95), ("C7+", 96, 110)],
}
INSTRUMENT = {"violin": 0, "cello": 1}
FEATURES = ["rise", "rise_hf", "early", "scratch", "scratch_hf", "bright", "noise", "noise_hf",
            "h1", "h2-3", "h4-7", "h8-15", "h16+", "cents"]


def midi(name, octave_shift=0):
    m = re.match(r"([A-G])([sb]?)(\d)$", name)
    acc = {"s": 1, "b": -1, "": 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1 + octave_shift) + NAMES[m.group(1)] + acc


def mtof(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    if sr != SR:
        x = sig.resample_poly(x, SR, sr)
    return x


# ---- measurement -------------------------------------------------------------------------

def level_track(x, win=0.010, hop=0.002):
    """Level (dB) in `win` windows every `hop` s."""
    L, H = int(win * SR), int(hop * SR)
    c = np.concatenate([[0.0], np.cumsum(x * x)])
    idx = np.arange(0, max(len(x) - L, 1), H)
    return 10 * np.log10((c[idx + L] - c[idx]) / L + 1e-20)


def marks(x):
    """Onset (s; first 10 ms window within 25 dB of the peak, 4 ms earlier) and release (s; the
    last within 10 dB of the peak)."""
    lv = level_track(x)
    mx = lv.max()
    on = max(int(np.argmax(lv > mx - 25)) - 2, 0) * 0.002
    rel = int(np.where(lv > mx - 10)[0][-1]) * 0.002
    return on, rel


def aperiodicity(seg, f0, hf=False):
    """Median noise/periodic power (dB) of a segment: each stretch of ~max(2 periods, 6 ms)
    correlated with the next period (lag searched ±3 %, 4x upsampled)."""
    if hf:
        sos = sig.butter(4, 2000, "highpass", fs=SR, output="sos")
        seg = sig.sosfiltfilt(sos, seg)
    U = 4
    y = sig.resample_poly(seg, U, 1)
    fs = SR * U
    T = fs / f0
    W = int(max(2 * T, 0.006 * fs))
    lags = np.arange(int(T * 0.97), int(np.ceil(T * 1.03)) + 1)
    hop = int(T) if W < 3 * T else W // 2
    out = []
    i = 0
    while i + W + lags[-1] < len(y):
        a = y[i:i + W]
        ea = a @ a
        best = -1.0
        if ea > 0:
            for l in lags:
                b = y[i + l:i + l + W]
                r = (a @ b) / np.sqrt(ea * (b @ b) + 1e-30)
                best = max(best, r)
        if best > 0:
            out.append(max((1 - best) / best, 1e-5))
        i += hop
    if not out:
        return None
    return float(10 * np.log10(np.median(out)))


def held_pitch(seg, f0):
    """Held pitch (Hz): the median of harmonics 1-3's peaks (zero-padded FFT), divided down."""
    N = 1 << 19
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N))
    f = np.fft.rfftfreq(N, 1 / SR)
    est = []
    for h in (1, 2, 3):
        m = (f > f0 * h * 0.97) & (f < f0 * h * 1.03)
        if m.any():
            est.append(f[m][np.argmax(S[m])] / h)
    return float(np.median(est))


def harmonic_groups(seg, f0):
    """Power in bands one f0 wide around each harmonic (to 15 kHz), grouped (dB re the sum)."""
    N = 1 << int(np.ceil(np.log2(len(seg))) + 1)
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N)) ** 2
    f = np.fft.rfftfreq(N, 1 / SR)
    c = np.concatenate([[0.0], np.cumsum(P)])
    nmax = int(min(15000, SR / 2 - f0) // f0)
    hp = []
    for n in range(1, nmax + 1):
        a, b = np.searchsorted(f, [(n - 0.5) * f0, (n + 0.5) * f0])
        hp.append(c[b] - c[a])
    hp = np.array(hp)
    tot = hp.sum() + 1e-30
    out = {}
    for name, lo, hi in HGROUPS:
        if lo <= nmax:
            out[name] = float(10 * np.log10(hp[lo - 1:min(hi, nmax)].sum() / tot + 1e-12))
    return out


def hf_share(seg):
    """Level above 2 kHz re the total (dB)."""
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    return float(10 * np.log10(P[f > 2000].sum() / (P.sum() + 1e-30) + 1e-12))


def measure(x, f0, sustain_from=0.3):
    """Every feature of one note (dict), or None if it has no sustain to measure."""
    on, rel = marks(x)
    s0, s1 = on + sustain_from, min(rel - 0.05, on + sustain_from + 0.8)
    if s1 - s0 < 0.15:
        return None
    S = x[int(s0 * SR):int(s1 * SR)]
    A = x[int(on * SR):int((on + 0.08) * SR)]
    lv = level_track(x)
    t = np.arange(len(lv)) * 0.002
    sus = np.median(lv[(t >= s0) & (t < s1)])
    after = lv[t >= on]
    r = {}
    r["rise"] = float(np.argmax(after > sus - 3) * 2.0)
    xh = sig.sosfiltfilt(sig.butter(4, 2000, "highpass", fs=SR, output="sos"), x)
    lh = level_track(xh)
    sh = np.median(lh[(t >= s0) & (t < s1)])
    r["rise_hf"] = float(np.argmax(lh[t >= on] > sh - 3) * 2.0)
    e = x[int(on * SR):int((on + 0.06) * SR)]
    r["early"] = float(10 * np.log10(np.mean(e * e) + 1e-20) - 10 * np.log10(np.mean(S * S) + 1e-20))
    fs = held_pitch(S, f0)
    r["cents"] = float(1200 * np.log2(fs / f0))
    r["scratch"] = aperiodicity(A, fs)
    r["scratch_hf"] = aperiodicity(A, fs, hf=True)
    r["noise"] = aperiodicity(S[:int(0.3 * SR)], fs)
    r["noise_hf"] = aperiodicity(S[:int(0.3 * SR)], fs, hf=True)
    r["bright"] = hf_share(A) - hf_share(S)
    r.update(harmonic_groups(S, fs))
    r["_on"], r["_s0"], r["_s1"] = on, s0, s1
    return r


# ---- references and renders ------------------------------------------------------------

def phil_refs(data, instrument, notes, dyns, durs):
    out = []
    pat = re.compile(rf"{instrument}_([A-G]s?\d)_(025|05|1|15)_([a-z-]+)_arco-normal\.wav$")
    for f in sorted(os.listdir(data)):
        m = pat.match(f)
        if not m:
            continue
        note, dur, dyn = m.groups()
        if dyn not in PHIL_DYN or (notes and note not in notes) or (dyns and dyn not in dyns) or dur not in durs:
            continue
        d, vel = PHIL_DYN[dyn]
        out.append(dict(src="phil", id=f"phil_{note}_{dur}_{d}", midi=midi(note), dyn=d, vel=vel,
                        path=os.path.join(data, f), vib=True))
    return out


def bigcat_refs(root, notes, dyns):
    out = []
    d = os.path.join(root, "Samples", "sus")
    for f in sorted(os.listdir(d)):
        m = re.fullmatch(r"([A-G]b?\d)_(p|mp|mf|f)_d\.wav", f)  # down-bows only (from the air, like the synth)
        if not m:
            continue
        name, dyn = m.groups()
        n = midi(name, octave_shift=1)  # bigcat names notes an octave low
        if (notes and n not in notes) or (dyns and dyn not in dyns):
            continue
        out.append(dict(src="bigcat", id=f"bigcat_{name}_{dyn}", midi=n, dyn=dyn, vel=BIGCAT_DYN[dyn],
                        path=os.path.join(d, f), vib=False))
    return out


def render(render_tool, instrument, params, ref, held, sr, tmp):
    p = {"instrument": INSTRUMENT[instrument], "room.mix": 20}
    if not ref["vib"]:
        p["vib.depth"] = 0
    p.update(params)
    sp = os.path.join(tmp, ref["id"] + ".json")
    wp = os.path.join(tmp, ref["id"] + ".wav")
    json.dump({"patch": "init", "length": LEAD + held + 1.0, "params": p,
               "events": [{"t": LEAD, "on": ref["midi"], "vel": ref["vel"]},
                          {"t": LEAD + held, "off": ref["midi"]}]}, open(sp, "w"))
    subprocess.run([render_tool, sp, "--out", wp, "--sr", str(sr)], check=True, capture_output=True)
    return load(wp)


def one(job):
    ref, render_tool, instrument, params, sr, tmp, clips = job
    x = load(ref["path"])
    f0 = mtof(ref["midi"])
    mr = measure(x, f0)
    if mr is None:
        return ref, None, None
    on, rel = marks(x)
    held = max(rel - on, 0.4)
    y = render(render_tool, instrument, params, ref, held, sr, tmp)
    ms = measure(y, f0)
    if clips and ms is not None:
        np.save(os.path.join(tmp, ref["id"] + "_pair.npy"),
                np.array([x[int(mr["_on"] * SR):int((mr["_on"] + held + 0.5) * SR)],
                          y[int(ms["_on"] * SR):int((ms["_on"] + held + 0.5) * SR)]], dtype=object),
                allow_pickle=True)
    return ref, mr, ms


# ---- reporting ---------------------------------------------------------------------------

def register_of(n, instrument):
    for name, lo, hi in REGISTERS[instrument]:
        if lo <= n <= hi:
            return name
    return None


def table(rows, instrument, src):
    """Median per register x dynamic group: recording / synth / synth - recording."""
    print(f"\n=== {src}: median recording / synth (synth - recording), by register and dynamic ===")
    groups = {}
    for r in rows:
        if r["src"] != src or r["rec"] is None or r["syn"] is None:
            continue
        reg = register_of(r["midi"], instrument)
        dg = "soft" if r["dyn"] in SOFT else "loud"
        groups.setdefault((reg, dg), []).append(r)
    order = [name for name, *_ in REGISTERS[instrument]]
    hdr = f"{'register':18s} {'dyn':4s} {'n':>3s} " + " ".join(f"{k:>17s}" for k in FEATURES)
    print(hdr)
    for reg in order:
        for dg in ("soft", "loud"):
            g = groups.get((reg, dg))
            if not g:
                continue
            cells = []
            for k in FEATURES:
                a = [r["rec"][k] for r in g if r["rec"].get(k) is not None and r["syn"].get(k) is not None]
                b = [r["syn"][k] for r in g if r["rec"].get(k) is not None and r["syn"].get(k) is not None]
                if not a:
                    cells.append(f"{'-':>17s}")
                    continue
                ma, mb = np.median(a), np.median(b)
                d = np.median(np.array(b) - np.array(a))
                cells.append(f"{ma:5.0f}/{mb:5.0f}({d:+5.0f})" if k.startswith("rise") else f"{ma:5.1f}/{mb:5.1f}({d:+5.1f})")
            print(f"{reg:18s} {dg:4s} {len(g):3d} " + " ".join(cells))


def write_clips(rows, instrument, tmp, outdir):
    """One note per register x dynamic group (the median pitch): recording, 0.4 s, synth."""
    os.makedirs(outdir, exist_ok=True)
    groups = {}
    for r in rows:
        if r["rec"] is None or r["syn"] is None:
            continue
        key = (r["src"], register_of(r["midi"], instrument), "soft" if r["dyn"] in SOFT else "loud")
        groups.setdefault(key, []).append(r)
    for (src, reg, dg), g in sorted(groups.items()):
        g = sorted(g, key=lambda r: r["midi"])
        r = g[len(g) // 2]
        pf = os.path.join(tmp, r["id"] + "_pair.npy")
        if not os.path.exists(pf):
            continue
        x, y = np.load(pf, allow_pickle=True)
        x = np.asarray(x, float)
        y = np.asarray(y, float)
        rx, ry = np.sqrt(np.mean(x ** 2)), np.sqrt(np.mean(y ** 2))
        y = y * rx / max(ry, 1e-12)
        out = np.concatenate([x, np.zeros(int(0.4 * SR)), y])
        out *= 0.5 / max(np.abs(out).max(), 1e-9)
        name = f"{src}_{reg.split()[0]}_{dg}_{r['id'].split('_', 1)[1]}.wav"
        sf.write(os.path.join(outdir, name), out, SR, subtype="PCM_16")
        print("clip:", os.path.join(outdir, name))


def compare(a, b, instrument):
    """Two runs: per register x dynamic group, the change in synth - recording."""
    A, B = json.load(open(a)), json.load(open(b))
    ia = {r["id"]: r for r in A["rows"] if r["rec"] and r["syn"]}
    ib = {r["id"]: r for r in B["rows"] if r["rec"] and r["syn"]}
    common = sorted(set(ia) & set(ib))
    print(f"{len(common)} notes in both. Median of (synth - recording): A -> B")
    groups = {}
    for i in common:
        r = ia[i]
        groups.setdefault((r["src"], register_of(r["midi"], instrument),
                           "soft" if r["dyn"] in SOFT else "loud"), []).append(i)
    print(f"{'src':7s} {'register':18s} {'dyn':4s} " + " ".join(f"{k:>14s}" for k in FEATURES))
    order = {name: j for j, (name, *_) in enumerate(REGISTERS[instrument])}
    for key in sorted(groups, key=lambda k: (k[0], order.get(k[1], 99), k[2])):
        cells = []
        for k in FEATURES:
            da = [ia[i]["syn"][k] - ia[i]["rec"][k] for i in groups[key]
                  if ia[i]["syn"].get(k) is not None and ia[i]["rec"].get(k) is not None]
            db = [ib[i]["syn"][k] - ib[i]["rec"][k] for i in groups[key]
                  if ib[i]["syn"].get(k) is not None and ib[i]["rec"].get(k) is not None]
            cells.append(f"{np.median(da):+6.1f}>{np.median(db):+6.1f}" if da and db else f"{'-':>14s}")
        print(f"{key[0]:7s} {key[1]:18s} {key[2]:4s} " + " ".join(cells))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--instrument", default="cello", choices=list(INSTRUMENT))
    ap.add_argument("--phil", default="")
    ap.add_argument("--bigcat", default="")
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--notes", default="", help="Philharmonia note names (C2,As3,...)")
    ap.add_argument("--dyn", default="", help="Philharmonia dynamics (pianissimo,forte,...) / bigcat p,mp,mf,f")
    ap.add_argument("--dur", default="1,15", help="Philharmonia lengths")
    ap.add_argument("--param", action="append", default=[])
    ap.add_argument("--sr", type=int, default=48000)
    ap.add_argument("--json", default="")
    ap.add_argument("--clips", default="")
    ap.add_argument("--compare", nargs=2)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()
    if a.compare:
        compare(*a.compare, a.instrument)
        return
    params = {}
    for kv in a.param:
        k, v = kv.split("=", 1)
        params[k] = float(v)
    notes = set(filter(None, a.notes.split(",")))
    dyns = set(filter(None, a.dyn.split(",")))
    refs = []
    if a.phil:
        refs += phil_refs(a.phil, a.instrument, notes, dyns, set(a.dur.split(",")))
    if a.bigcat:
        if a.instrument != "cello":
            sys.exit("bigcat is a cello")
        refs += bigcat_refs(a.bigcat, {midi(n) for n in notes}, dyns)
    if not refs:
        sys.exit("no recordings: give --phil and/or --bigcat")
    print(f"{len(refs)} recordings; rendering at {a.sr} Hz, params {params}", file=sys.stderr)
    with tempfile.TemporaryDirectory() as tmp:
        jobs = [(r, a.render, a.instrument, params, a.sr, tmp, bool(a.clips)) for r in refs]
        rows = []
        with Pool(a.jobs) as pool:
            for k, (ref, mr, ms) in enumerate(pool.imap_unordered(one, jobs)):
                rows.append(dict(id=ref["id"], src=ref["src"], midi=ref["midi"], dyn=ref["dyn"],
                                 rec={k2: v for k2, v in (mr or {}).items() if not k2.startswith("_")} if mr else None,
                                 syn={k2: v for k2, v in (ms or {}).items() if not k2.startswith("_")} if ms else None))
                if (k + 1) % 25 == 0:
                    print(f"  {k + 1}/{len(jobs)}", file=sys.stderr)
        rows.sort(key=lambda r: r["id"])
        for src in ("phil", "bigcat"):
            if any(r["src"] == src for r in rows):
                table(rows, a.instrument, src)
        if a.clips:
            write_clips(rows, a.instrument, tmp, a.clips)
    if a.json:
        json.dump({"sr": a.sr, "params": params, "instrument": a.instrument, "rows": rows}, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
