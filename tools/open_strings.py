#!/usr/bin/env python3
"""Open strings' pitch under the bow at three dynamics (cents vs equal
temperament, Intonation Style 0). Use after changing friction, string or
the open-string allowance (InstrumentDef::openTuneCents): each should be
within a few cents.   python3 tools/open_strings.py   (RENDER=... to pick a build)"""
from evalkit import render, pitch, cents, mtof

for dyn in (0.15, 0.5, 0.85):
    ev, t = [], 0
    for n in (55, 62, 69, 76):
        ev += [{"t": t, "on": n, "vel": 80}, {"t": t + 2.0, "off": n}]
        t += 2.3
    sr, x = render({"patch": "init", "length": t + 0.5,
                    "params": {"room.mix": 0, "finger.style": 0, "bow.dynamics": dyn}, "events": ev})
    row = []
    for i, n in enumerate((55, 62, 69, 76)):
        seg = x[int((i * 2.3 + 0.6) * sr):int((i * 2.3 + 1.9) * sr)]
        row.append(cents(pitch(seg, sr, mtof(n)), mtof(n)))
    print(f"dynamics {dyn}: " + "  ".join(f"{s} {c:+.1f}" for s, c in zip("GDAE", row)))
