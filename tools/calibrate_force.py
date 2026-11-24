#!/usr/bin/env python3
"""Measure where the playability assistant sits inside each string's
Helmholtz window: for notes on each string (open, +4, +7 semitones, the
string forced) and four dynamics, sweep the Pressure offset (-1.6 .. +1.6 by
0.2) and report the contiguous run of Pressures around 0 that give Helmholtz
motion by the stick test (instrument_lab: stick fraction, slips per period,
jitter; KB §7.3). A well-placed assistant has the window straddling 0 with
margin on both sides; '*' marks a window that misses 0.

    python3 tools/calibrate_force.py [build/instrument_lab] [--instrument cello] [-j 2]
"""
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

argv = sys.argv[1:]
cello = "cello" in argv
jobs_n = int(argv[argv.index("-j") + 1]) if "-j" in argv else 2
args = [a for a in argv if a not in ("--instrument", "cello", "-j") and not (argv.index(a) > 0 and argv[argv.index(a) - 1] == "-j")]
lab = args[0] if args else "build/instrument_lab"
strings = {"C": 36, "G": 43, "D": 50, "A": 57} if cello else {"G": 55, "D": 62, "A": 69, "E": 76}
inst = "cello" if cello else "violin"
pressures = [round(-1.6 + 0.2 * i, 1) for i in range(17)]
dyns = (0.0, 0.3, 0.6, 0.9)


def regime(job):
    s, note, dyn, p = job
    env = dict(os.environ, SM_INSTRUMENT=inst, LAB_STRING=str(s), LAB_DYN=str(dyn), LAB_PRESSURE=str(p))
    out = subprocess.run([lab, "chord", str(note)], env=env, capture_output=True, text=True).stdout
    return job, out.strip().endswith("helmholtz")


jobs = [(s, o + off, d, p) for s, o in enumerate(strings.values()) for off in (0, 4, 7) for d in dyns for p in pressures]
with ThreadPoolExecutor(jobs_n) as ex:
    R = dict(ex.map(regime, jobs))
margins = []
for s, (name, o) in enumerate(strings.items()):
    for off in (0, 4, 7):
        note = o + off
        row = []
        for d in dyns:
            ok = [R[(s, note, d, p)] for p in pressures]
            i0 = pressures.index(0.0)
            if ok[i0]:
                lo = hi = i0
                while lo > 0 and ok[lo - 1]: lo -= 1
                while hi < len(ok) - 1 and ok[hi + 1]: hi += 1
                margins.append(min(-pressures[lo], pressures[hi]))
                row.append("d%.1f: %+.1f..%+.1f " % (d, pressures[lo], pressures[hi]))
            else:
                runs = [i for i, v in enumerate(ok) if v]
                margins.append(-min(abs(pressures[i]) for i in runs) if runs else -2)
                near = min(runs, key=lambda i: abs(pressures[i])) if runs else None
                row.append("d%.1f: %s" % (d, ("near %+.1f*   " % pressures[near]) if near is not None else "  none*      "))
        print("%s %3d  %s" % (name, note, "  ".join(row)), flush=True)
print("margin at Pressure 0: mean %.2f, worst %.2f (Pressure steps 0.2)" % (sum(margins) / len(margins), min(margins)))
