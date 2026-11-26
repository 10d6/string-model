#!/usr/bin/env python3
"""The held tone's noise: how much of a bowed note is not its harmonics, by octave band.

Why: against the recordings the synth's held tone measured 10-28 dB too clean
(docs/EVALUATION.md "Attacks and sustains by register"). This measures it the way the
onsets were made (tools/make_onsets.py): f0 tracked, each harmonic heterodyned along it
(a three-period window, so slow wander stays in the note) and taken out; what's left, from
0.4 s after the onset for up to 1 s, is the bow's noise (and any fast jitter). Per octave
band (125 Hz-8 kHz) its power re the held harmonics' total, in dB.

References (measured only, never committed):
  --iowa DIR     the Iowa MIS cello arco WAVs (anechoic; each note on its run's string)
  --bigcat DIR   the Karoryfer x bigcat cello (CC0; down- and up-bow sustains p-f)
and the synth playing each of the same notes (matched string, velocity, length; no
vibrato, Room 0), Modelled output, measured the same way. The synth's Pickup output (its
bridge force) gives each band's share of its harmonics, which turns a missing noise-to-
harmonics ratio into a level in bridge force: the noise the model lacks, to add before the
radiation (which then colours both alike).

    python3 -I tools/bow_noise.py --iowa ~/data/iowa/cello/wav/arco --bigcat ~/data/bigcat
            [--render build/stringmodel_render] [--param id=v] [--json out.json]
            [--header model/data/CelloBowNoise.h]

Prints, per string and dynamic (soft pp/p, mid mp/mf, loud f/ff) and band: the recordings'
ratio (median of both), the synth's, and the noise to add. --header writes that table (dB
re the bridge force's RMS) for model/BowNoise.h.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from multiprocessing import Pool

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_onsets as MO  # noqa: E402

SR = 44100
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]  # octave band centres
GROUPS = ["soft", "mid", "loud"]
GROUP_OF = {"pp": 0, "p": 0, "mp": 1, "mf": 1, "f": 2, "ff": 2}
VEL = {"pp": 45, "p": 40, "mp": 60, "mf": 80, "f": 100, "ff": 110}
OPEN = [36, 43, 50, 57]


def string_of(m):
    s = 0
    for i, o in enumerate(OPEN):
        if m >= o:
            s = i
    return s


def band_db(x, sr, ref_power):
    """Each octave band's power (per sample, Welch) re ref_power, in dB."""
    n = 4096
    if len(x) < n:
        return None
    w = np.hanning(n)
    segs = [x[i:i + n] * w for i in range(0, len(x) - n, n // 2)]
    # one-sided power per bin, so that the bins sum to the mean square
    P = np.mean([np.abs(np.fft.rfft(s)) ** 2 for s in segs], axis=0) * 2 / (n * np.sum(w ** 2))
    f = np.fft.rfftfreq(n, 1 / sr)
    out = []
    for c in BANDS:
        m = (f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))
        out.append(float(10 * np.log10(P[m].sum() + 1e-30) - 10 * np.log10(ref_power + 1e-30)))
    return out


def analyse(x, sr, m, t0, t1):
    """Residual band levels re the harmonics' power, and the harmonics' own band shares, over
    [t0, t1] s of x (onset already near t = 0.1 s)."""
    if sr != SR:
        import scipy.signal as sig
        x = sig.resample_poly(x, SR, sr)
    a, b = int(max(t0 - 0.2, 0) * SR), int((t1 + 0.1) * SR)
    seg = x[a:b]
    if len(seg) < int(0.6 * SR):
        return None
    f0s = MO.track_f0(seg, MO.mtof(m))
    h, _, _ = MO.harmonics(seg, f0s)
    i0, i1 = int((t0 - a / SR) * SR), int((t1 - a / SR) * SR)
    r = (seg - h)[i0:i1]
    hh = h[i0:i1]
    hp = np.mean(hh ** 2)
    res = band_db(r, SR, hp)
    harm = band_db(hh, SR, hp)
    if res is None:
        return None
    return dict(res=res, harm=harm)


def render(tool, m, vel, held, string, params, tmp, mode):
    p = {"instrument": 1, "room.mix": 0, "vib.depth": 0, "out.mode": mode}
    if string is not None:
        p["string.select"] = string + 1
    p.update(params)
    tag = f"{m}_{vel}_{string}_{mode}"
    sp = os.path.join(tmp, tag + ".json")
    wp = os.path.join(tmp, tag + ".wav")
    json.dump({"patch": "init", "length": held + 0.6, "params": p,
               "events": [{"t": 0.1, "on": m, "vel": vel}, {"t": 0.1 + held, "off": m}]}, open(sp, "w"))
    subprocess.run([tool, sp, "--out", wp, "--sr", "48000"], check=True, capture_output=True)
    y, sr = sf.read(wp, always_2d=True)
    return y.mean(axis=1), sr


def one(job):
    ref, tool, params, tmp = job
    x, sr = sf.read(ref["path"], always_2d=True, dtype="float64")
    x = x.mean(axis=1)
    if "seg" in ref:
        a, b = ref["seg"]
        x = x[int(max(a - 0.1, 0) * sr):int(b * sr)]
        on = 0.1
    else:
        on = MO.onset(x if sr == SR else x) / sr
        x = x[max(int((on - 0.1) * sr), 0):]
        on = 0.1
    dur = len(x) / sr
    t0, t1 = on + 0.4, min(on + 1.4, dur - 0.15)
    if t1 - t0 < 0.4:
        return ref, None, None, None
    rec = analyse(x, sr, ref["midi"], t0, t1)
    held = t1 + 0.1 - 0.1
    y, ysr = render(tool, ref["midi"], ref["vel"], held, ref["string"], params, tmp, 3)
    syn = analyse(y, ysr, ref["midi"], t0, t1)
    yp, psr = render(tool, ref["midi"], ref["vel"], held, ref["string"], params, tmp, 2)
    pick = analyse(yp, psr, ref["midi"], t0, t1)
    return ref, rec, syn, pick


def refs_iowa(d):
    import iowa
    out = []
    for f in sorted(os.listdir(d)):
        info = iowa.parse(f)
        if not info or info[1] != "arco":
            continue
        for n, st, dyn, on, end in iowa.iowa_notes(os.path.join(d, f)):
            out.append(dict(src="iowa", midi=n, dyn=dyn, vel=VEL[dyn], string=st,
                            path=os.path.join(d, f), seg=(on, end)))
    return out


def refs_bigcat(root):
    d = os.path.join(root, "Samples", "sus")
    out = []
    for f in sorted(os.listdir(d)):
        parts = f[:-4].split("_")
        if len(parts) != 3:
            continue
        m = MO.midi(parts[0])
        out.append(dict(src="bigcat", midi=m, dyn=parts[1], vel=VEL[parts[1]], string=string_of(m),
                        path=os.path.join(d, f)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iowa", default="")
    ap.add_argument("--bigcat", default="")
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--param", action="append", default=[])
    ap.add_argument("--json", default="")
    ap.add_argument("--header", default="")
    a = ap.parse_args()
    params = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in a.param}
    refs = (refs_iowa(a.iowa) if a.iowa else []) + (refs_bigcat(a.bigcat) if a.bigcat else [])
    print(f"{len(refs)} recordings", file=sys.stderr)
    with tempfile.TemporaryDirectory() as tmp:
        with Pool(os.cpu_count() or 2) as pool:
            res = pool.map(one, [(r, a.render, params, tmp) for r in refs])
    rows = [dict(src=r["src"], midi=r["midi"], dyn=r["dyn"], string=r["string"], rec=rec, syn=syn, pick=pick)
            for r, rec, syn, pick in res if rec and syn and pick]
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    table = np.full((4, 3, len(BANDS)), -60.0)
    print(f"\nnoise re the held harmonics (dB) per octave band {BANDS}: recordings (Iowa, bigcat) / synth -> to add (bridge force, dB re its RMS)")
    for s, name in enumerate("CGDA"):
        for g, gname in enumerate(GROUPS):
            cell = [r for r in rows if r["string"] == s and GROUP_OF[r["dyn"]] == g]
            if not cell:
                continue
            cells = []
            for k in range(len(BANDS)):
                iw = [r["rec"]["res"][k] for r in cell if r["src"] == "iowa"]
                bc = [r["rec"]["res"][k] for r in cell if r["src"] == "bigcat"]
                srcs = [np.median(v) for v in (iw, bc) if v]
                rec = float(np.mean(srcs))
                syn = float(np.median([r["syn"]["res"][k] for r in cell]))
                share = float(np.median([r["pick"]["harm"][k] for r in cell]))  # band's share of the bridge force's harmonics
                add = 10 * np.log10(max(10 ** (rec / 10) - 10 ** (syn / 10), 1e-6))
                table[s, g, k] = add + share
                cells.append(f"{(np.median(iw) if iw else np.nan):5.0f},{(np.median(bc) if bc else np.nan):5.0f}/{syn:5.0f}->{table[s, g, k]:5.0f}")
            print(f"  {name} {gname:4s} n={len(cell):3d}  " + "  ".join(cells))
    if a.header:
        lines = ["// Generated by tools/bow_noise.py: the held tone's missing noise per string, dynamic",
                 "// (soft, mid, loud) and octave band, in dB re the bridge force's RMS: the Iowa MIS",
                 "// cello arco and the Karoryfer x bigcat cello against this synth. Do not edit.",
                 "#pragma once", "", "namespace sm::data {", "",
                 f"constexpr int kBowNoiseBands = {len(BANDS)};",
                 "constexpr float kBowNoiseHz[kBowNoiseBands] = {" + ", ".join(f"{b}.f" for b in BANDS) + "};",
                 "constexpr float kBowNoiseVel[3] = {42.f, 70.f, 105.f};  // the dynamics' velocities",
                 "constexpr float kBowNoiseDb[4][3][kBowNoiseBands] = {"]
        for s in range(4):
            lines.append("  {")
            for g in range(3):
                lines.append("    {" + ", ".join(f"{v:.1f}f" for v in table[s, g]) + "},")
            lines.append("  },")
        lines += ["};", "", "}  // namespace sm::data", ""]
        open(a.header, "w").write("\n".join(lines))
        print("wrote", a.header, file=sys.stderr)


if __name__ == "__main__":
    main()
