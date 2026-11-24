#!/usr/bin/env python3
"""Measure where the playability assistant sits inside each string's
Helmholtz window: for notes on each string and several dynamics, sweep the
Pressure offset and report the lowest and highest values that give clean
Helmholtz motion (slip rate within 2% of the pitch). A well-placed assistant
has the window straddling 0.

    python3 tools/calibrate_force.py [build/instrument_lab] [--instrument cello]
"""
import os, re, subprocess, sys
cello = "cello" in sys.argv[1:]
args = [a for a in sys.argv[1:] if a not in ("--instrument", "cello")]
lab = args[0] if args else "build/instrument_lab"
strings = {"C": 36, "G": 43, "D": 50, "A": 57} if cello else {"G": 55, "D": 62, "A": 69, "E": 76}
if cello: os.environ["SM_INSTRUMENT"] = "cello"
pressures = [round(-0.8 + 0.1 * i, 1) for i in range(19)]  # -0.8 .. 1.0
def clean(note, dyn, p):
    env = dict(os.environ, LAB_DYN=str(dyn), LAB_PRESSURE=str(p))
    out = subprocess.run([lab, "chord", str(note)], env=env, capture_output=True, text=True).stdout
    m = re.search(r"slip rate ([0-9.]+) Hz", out)
    if not m: return False
    hz = 440 * 2 ** ((note - 69) / 12)
    return abs(float(m.group(1)) / hz - 1) < 0.02
for name, open_note in strings.items():
    for off in (0, 4, 7):
        note = open_note + off
        row = []
        for dyn in (0.0, 0.3, 0.6, 0.9):
            ok = [p for p in pressures if clean(note, dyn, p)]
            row.append("d%.1f: %s" % (dyn, ("%+.1f..%+.1f" % (min(ok), max(ok))) if ok else "  none   "))
        print("%s %3d  %s" % (name, note, "   ".join(row)), flush=True)
