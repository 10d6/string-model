#!/usr/bin/env python3
"""The coupled instrument's Helmholtz windows: for one note per string (the
string forced), the bow's contact point beta, speed and force pinned
(instrument_lab LAB_BETA / LAB_VB / LAB_FORCE), the regime from the stick
test (KB §7.3): H Helmholtz, m multiple slip, s sub-harmonic, r raucous,
. stick/slip/silent. Then where the dynamics law puts the bow (beta at the
law's dynamics) for comparison.

    python3 tools/window_map.py [--vb 0.1,0.25] [--offset 4] [--instrument cello|violin]
"""
import argparse, os, re, subprocess
from concurrent.futures import ThreadPoolExecutor
import numpy as np

LABEL = {"helmholtz": "H", "multiple": "m", "subharmonic": "s", "raucous": "r"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", default="build/instrument_lab")
    ap.add_argument("--vb", default="0.1,0.25")
    ap.add_argument("--offset", type=int, default=4, help="semitones above each open string")
    ap.add_argument("--instrument", default="cello")
    ap.add_argument("--betas", default="0.04,0.05,0.06,0.075,0.09,0.11,0.13,0.15,0.18,0.22")
    a = ap.parse_args()
    cello = a.instrument == "cello"
    opens, names = ((36, 43, 50, 57), "CGDA") if cello else ((55, 62, 69, 76), "GDAE")
    betas = [float(b) for b in a.betas.split(",")]
    forces = np.geomspace(0.1, 20, 19) if cello else np.geomspace(0.03, 6, 19)
    vbs = [float(v) for v in a.vb.split(",")]

    def run(job):
        s, vb, b, f = job
        env = dict(os.environ, SM_INSTRUMENT=a.instrument, LAB_STRING=str(s), LAB_BETA=str(b), LAB_VB=str(vb),
                   LAB_FORCE=f"{f:.4f}")
        out = subprocess.run([a.lab, "chord", str(opens[s] + a.offset)], env=env, capture_output=True, text=True).stdout
        m = re.search(r": (helmholtz|multiple|subharmonic|raucous|stick|slip)\s*$", out.strip())
        return job, LABEL.get(m.group(1), ".") if m else "?"

    jobs = [(s, vb, b, f) for s in range(4) for vb in vbs for b in betas for f in forces]
    with ThreadPoolExecutor(int(os.environ.get("J", "2"))) as ex:
        R = dict(ex.map(run, jobs))
    print(f"forces {forces[0]:.2f}..{forces[-1]:.0f} N, log grid (x{forces[1] / forces[0]:.2f}); H Helmholtz m multiple s sub-harmonic r raucous")
    for s in range(4):
        for vb in vbs:
            print(f"{names[s]} string, note {opens[s] + a.offset}, v_b {vb} m/s")
            for b in betas:
                row = "".join(R[(s, vb, b, f)] for f in forces)
                hs = [f for f in forces if R[(s, vb, b, f)] == "H"]
                edge = f"{hs[0]:5.2f}..{hs[-1]:5.2f} N ({len(hs)} cells)" if hs else "none"
                print(f"  beta {b:5.3f}  {row}  {edge}")


if __name__ == "__main__":
    main()
