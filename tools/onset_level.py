#!/usr/bin/env python3
"""The recorded onsets' level in the synth against the recordings (model/Onsets.h).

The onsets are added to the bridge force after the strings, so a render with Bow Catch 1
less one with Bow Catch 0 is exactly the onset as heard (through the body, mics and room),
with no chaos between the two. For each bigcat sustain (the clips' source) this renders the
same note both ways, from the air, and compares:

  synth  the onset's level 40-120 ms after it starts re the note's (Bow Catch 0) over the
         same 40 ms, in dB: the noise-to-tone ratio, which the onsets keep while the note
         builds up (model/Onsets.h)
  rec    the recording's residual (its harmonics removed, as tools/make_onsets.py does) re
         its harmonic part, over the same window after its onset

and prints the median of synth - rec per string and dynamic: what InstrumentDef::onsetGain
(and onsetString) should change by. Room 0, no vibrato.

    python3 -I tools/onset_level.py ~/data/bigcat [--render build/stringmodel_render] [--param id=v]
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

SR = 48000
LEAD = 0.2
VEL = {"p": 40, "mp": 60, "mf": 80, "f": 100}
OPEN = [36, 43, 50, 57]  # C G D A


def string_of(m):
    """The string the performer plays a note on (Auto: the highest string below it)."""
    s = 0
    for i, o in enumerate(OPEN):
        if m >= o:
            s = i
    return s


def render(tool, m, vel, catch, params, tmp):
    p = {"instrument": 1, "room.mix": 0, "vib.depth": 0, "bow.catch": catch}
    p.update(params)
    sp = os.path.join(tmp, f"{m}_{vel}_{catch}.json")
    wp = sp.replace(".json", ".wav")
    json.dump({"patch": "init", "length": LEAD + 1.4, "params": p,
               "events": [{"t": LEAD, "on": m, "vel": vel}, {"t": LEAD + 1.2, "off": m}]}, open(sp, "w"))
    subprocess.run([tool, sp, "--out", wp, "--sr", str(SR)], check=True, capture_output=True)
    x, _ = sf.read(wp, always_2d=True)
    return x.mean(axis=1)


def db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-30)


def one(job):
    path, m, dyn, tool, params, tmp = job
    # the recording: residual 0-80 ms after the onset re the held harmonics
    x, _ = sf.read(path, dtype="float64")
    on = MO.onset(x)
    a = max(on - int(MO.PRE * MO.SR), 0)
    seg = x[a:a + MO.SR]
    f0s = MO.track_f0(seg, MO.mtof(m))
    h, _, _ = MO.harmonics(seg, f0s)
    o = on - a
    pre = int(MO.PRE * MO.SR)
    ramp = np.clip((np.arange(len(seg)) - (o - pre)) / pre, 0, 1)
    r = seg - h * ramp
    w0, w1 = int(0.04 * MO.SR), int(0.12 * MO.SR)
    rec = db(r[o + w0:o + w1]) - db((h * ramp)[o + w0:o + w1])
    # the synth: (catch 1 - catch 0) over 80 ms from the onset re the held note
    y0 = render(tool, m, VEL[dyn], 0, params, tmp)
    y1 = render(tool, m, VEL[dyn], 1, params, tmp)
    d = y1 - y0
    t0 = int(LEAD * SR)
    k = int(np.argmax(np.abs(d[t0:]) > 1e-3 * np.abs(d).max())) + t0  # the onset clip's start
    k += int(MO.PRE * SR)  # the clip starts PRE before its recorded onset
    syn = db(d[k + int(0.04 * SR):k + int(0.12 * SR)]) - db(y0[k + int(0.04 * SR):k + int(0.12 * SR)])
    return m, dyn, rec, syn


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bigcat")
    ap.add_argument("--render", default="build/stringmodel_render")
    ap.add_argument("--param", action="append", default=[])
    a = ap.parse_args()
    params = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in a.param}
    d = os.path.join(a.bigcat, "Samples", "sus")
    jobs = []
    with tempfile.TemporaryDirectory() as tmp:
        for f in sorted(os.listdir(d)):
            parts = f[:-4].split("_")
            if len(parts) != 3 or parts[2] != "d":
                continue
            jobs.append((os.path.join(d, f), MO.midi(parts[0]), parts[1], a.render, params, tmp))
        with Pool(os.cpu_count() or 2) as pool:
            res = pool.map(one, jobs)
    # leave out the takes make_onsets.py replaces (the synth plays another clip there): the
    # bounced ones, and those 9 dB over their note's median (of the down-bows here)
    keep = []
    for m, dyn, rec, syn in res:
        med = np.median([r for mm, _, r, _ in res if mm == m])
        if (m, dyn, "d") in MO.BOUNCED or rec > med + MO.ODD_DB:
            print(f"left out: {m} {dyn} (an odd take, replaced in the header)")
            continue
        keep.append((m, dyn, rec, syn))
    res = keep
    print(f"{'note':>4s} {'dyn':3s} {'rec':>6s} {'synth':>6s} {'diff':>6s}")
    for m, dyn, rec, syn in sorted(res):
        print(f"{m:4d} {dyn:3s} {rec:6.1f} {syn:6.1f} {syn - rec:+6.1f}")
    print("\nmedian synth - rec (dB) by string x dynamic:")
    print("string  " + " ".join(f"{k:>6s}" for k in VEL) + "    all")
    for s, name in enumerate("CGDA"):
        row = [np.median([sy - re for m, dy, re, sy in res if string_of(m) == s and dy == k] or [np.nan]) for k in VEL]
        al = np.median([sy - re for m, dy, re, sy in res if string_of(m) == s])
        print(f"{name:6s}  " + " ".join(f"{v:+6.1f}" for v in row) + f" {al:+6.1f}")
    print(f"all     {np.median([sy - re for *_, re, sy in res]):+6.1f}")


if __name__ == "__main__":
    main()
