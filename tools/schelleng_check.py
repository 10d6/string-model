#!/usr/bin/env python3
"""The cello D string's Helmholtz window against Galluzzo's measurement (cello
doc §4.2, KB §7.1): an open Dominant D on a cello, bowed at 5 cm/s by a rigid
rod, sustained Helmholtz motion between F_min ~ 0.00086/beta^2 N and
F_max ~ 0.13/beta N.

For each beta, string_lab strokes (the string alone, rigid bridge; the
synth's friction) over a log grid of forces; a cell is Helmholtz when it
slips once per period (within 3%) and is within 15 cents (string_lab's
sweep test, from 0.75 s, i.e. after ~110 periods). Prints each row's
Helmholtz cells and the measured edges next to Galluzzo's.

    python3 tools/schelleng_check.py [--vb 0.05] [--string 2 --note 50] [--lab build/string_lab]
    (SM_INSTRUMENT=violin for the violin; default the cello)
"""
import argparse, os, re, subprocess
from concurrent.futures import ThreadPoolExecutor
import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", default="build/string_lab")
    ap.add_argument("--vb", type=float, default=0.05)
    ap.add_argument("--string", type=int, default=2)
    ap.add_argument("--note", type=int, default=50)
    ap.add_argument("--model", default="enhanced")
    a = ap.parse_args()
    betas = np.geomspace(0.02, 0.15, 9)
    forces = np.geomspace(0.03, 12, 27)
    env = dict(os.environ)
    env.setdefault("SM_INSTRUMENT", "cello")

    def run(bf):
        b, f = bf
        out = subprocess.run([a.lab, "tone", str(a.string), str(a.note), str(a.vb), f"{f:.4f}", f"{b:.4f}", a.model],
                             capture_output=True, text=True, env=env).stdout
        m = re.search(r"cents ([-+0-9.]+)\s+slips/period ([0-9.]+).*rms ([0-9.]+)", out)
        if not m:
            return bf, "?"
        c, spp, rms = float(m.group(1)), float(m.group(2)), float(m.group(3))
        if rms < 1e-3:
            return bf, "."
        if abs(spp - 1) < 0.03 and abs(c) < 15:
            return bf, "H"
        return bf, "m" if spp > 1.5 else "r"

    jobs = [(b, f) for b in betas for f in forces]
    with ThreadPoolExecutor(int(os.environ.get("J", "2"))) as ex:
        R = dict(ex.map(run, jobs))
    vb_scale = a.vb / 0.05  # Galluzzo measured at 5 cm/s; Schelleng's limits scale with v_b
    print(f"v_b {a.vb} m/s, string {a.string} note {a.note}. Forces {forces[0]:.2f}..{forces[-1]:.0f} N, log grid; H Helmholtz, m multiple, r other")
    print(f"{'beta':>6}  {'cells':27}  model F_min..F_max   Galluzzo (x v_b/0.05)")
    ratios = []
    for b in betas:
        row = "".join(R[(b, f)] for f in forces)
        hs = [f for f in forces if R[(b, f)] == "H"]
        gmin, gmax = 0.00086 / b ** 2 * vb_scale, 0.13 / b * vb_scale
        if hs:
            ratios.append((hs[0] / gmin, hs[-1] / gmax))
            edges = f"{hs[0]:6.2f}..{hs[-1]:5.2f} N"
        else:
            edges = "     none     "
        print(f"{b:6.3f}  {row}  {edges}   {gmin:5.2f}..{gmax:5.2f} N")
    if ratios:
        lo, hi = np.array(ratios).T
        print(f"model / Galluzzo, median: F_min x{np.median(lo):.2f}, F_max x{np.median(hi):.2f}  "
              f"(the grid steps x{forces[1] / forces[0]:.2f})")


if __name__ == "__main__":
    main()
