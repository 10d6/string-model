#!/usr/bin/env python3
"""Intonation Style: a D-major scale and four double stops at style 0 and 1
(vibrato off). Expected at 1 vs 0: leading tone C# about +8, F# +6, B +4,
G -4 cents; double-stop intervals pure (major third -14, minor sixth +14).
python3 tools/intonation_check.py"""
from evalkit import render, pitch, cents, mtof


def run(groups, style, dur):
    ev, t = [], 0
    for g in groups:
        for k in g:
            ev += [{"t": round(t, 3), "on": k, "vel": 80}, {"t": round(t + dur, 3), "off": k}]
        t += dur + 0.25
    sr, x = render({"patch": "init", "length": t + 0.5,
                    "params": {"room.mix": 0, "vib.depth": 0, "finger.style": style}, "events": ev})
    out, t = [], 0
    for g in groups:
        seg = x[int((t + 0.5) * sr):int((t + dur - 0.05) * sr)]
        out.append([cents(pitch(seg, sr, mtof(k)), mtof(k)) for k in g])
        t += dur + 0.25
    return out


scale = [[n] for n in (62, 64, 66, 67, 69, 71, 73, 74)]
names = ["D", "E", "F#", "G", "A", "B", "C#", "D"]
for st in (0, 1):
    r = run(scale, st, 1.2)
    print(f"scale, style {st}: " + "  ".join(f"{nm} {c[0]:+.0f}" for nm, c in zip(names, r)))
pairs = [(62, 66), (62, 69), (64, 72), (66, 74)]
for st in (0, 1):
    r = run(pairs, st, 1.5)
    print(f"double stops, style {st} (upper minus lower, cents vs ET): " +
          "  ".join(f"{a}-{b} {c[1] - c[0]:+.0f}" for (a, b), c in zip(pairs, r)))
