#!/usr/bin/env python3
"""Each Space's wet/direct power ratio at Room 100% (Modelled output, the
reference mic distance), for the distance trim (src/Synth.cpp kRoomPower).
The wet part is the render at 100% minus the render at 0%: the instrument
is deterministic and the room doesn't feed back into it.
    python3 tools/room_power.py [render] [--instrument cello]
"""
import json, os, subprocess, sys, tempfile
import numpy as np
from scipy.io import wavfile

args = [a for a in sys.argv[1:] if not a.startswith("--instrument") and a != "cello"]
cello = "cello" in sys.argv[1:]
render = args[0] if args else "build/stringmodel_render"
spaces = ["Room", "Chamber", "Hall", "Cathedral", "Plate"]
# A phrase across the range, mf, with rests (the tails count); the cello's
# an octave and a fifth lower (the same places on its strings).
ev, t = [], 0.0
down = 19 if cello else 0
for n, d in [(55, .6), (62, .6), (69, .6), (76, .6), (81, 1.2), (74, .6), (67, .6), (60, 1.2)]:
    ev += [{"t": round(t, 3), "on": n - down, "vel": 80}, {"t": round(t + d * 0.9, 3), "off": n - down}]
    t += d


def run(space, mix, tmp):
    sp, wp = os.path.join(tmp, "s.json"), os.path.join(tmp, "s.wav")
    json.dump({"patch": "init", "length": t + 6, "params": {"room.space": space, "room.mix": mix, "instrument": 1 if cello else 0},
               "events": ev}, open(sp, "w"))
    subprocess.run([render, sp, "--out", wp], check=True, capture_output=True)
    sr, x = wavfile.read(wp)
    return x.astype(float)


with tempfile.TemporaryDirectory() as tmp:
    dry = run(0, 0, tmp)
    pd = np.mean(dry ** 2)
    out = []
    for i, name in enumerate(spaces):
        wet = run(i, 100, tmp) - dry
        w = np.mean(wet ** 2) / pd
        out.append(w)
        print(f"{name:10s} W = {w:.3f}  ({10 * np.log10(w):+.1f} dB)")
    print("kRoomPower = {" + ", ".join(f"{w:.3f}f" for w in out) + "}")
