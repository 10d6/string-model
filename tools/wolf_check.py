#!/usr/bin/env python3
"""The cello's wolf note against its acceptance tests (cello doc §5, from
Inácio, Mansour et al. and Firth's measurements):

  1. it beats only within about ±1/2 semitone of the body mode;
  2. its beating gets faster with bow speed and slower with bow force;
  3. the worst note moves up a little as the bow nears the bridge;
  4. the Wolf Eliminator (more damping on the mode) removes it;
  5. odd harmonics beat much more than even ones;
  6. lower string impedance (lower tension) shrinks the range that wolfs.

Each run holds one note on the G string for 3 s, the performer's own attack
and then (from 0.3 s) the bow's speed, force and contact point pinned,
vibrato off (instrument_lab hold; the note moved
in cents with a4), and measures from 0.6 s: the beat (rms of the level's
1-20 Hz modulation, dB), its rate (Hz, where the beat is over 1 dB), and the
regime by the stick test. Measured beating rates were roughly 4-14 Hz.

    python3 tools/wolf_check.py [--quick] [--string 1] [--note 54]
    python3 tools/wolf_check.py --simple [--string 1] [--notes 50-56] [--param body.wolf=0.5]   # notes x dynamics as the performer plays them
"""
import argparse, json, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from scipy import signal
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LAB = "build/instrument_lab"


def hold(note, string, cents, vb, force, beta, wolf=0.0, z0=1.0):
    """Held note through instrument_lab: (beat dB, rate Hz, regime, odd/even beat dB)."""
    with tempfile.TemporaryDirectory() as t:
        env = dict(os.environ, SM_INSTRUMENT="cello", LAB_STRING=str(string), LAB_VIB="0", LAB_AUTO="0",
                   LAB_A4=f"{440 * 2 ** (cents / 1200):.4f}", LAB_VB=str(vb), LAB_FORCE=str(force), LAB_BETA=str(beta),
                   LAB_WOLF=str(wolf), LAB_Z0=str(z0), LAB_PIN_AFTER="0.3")
        out = subprocess.run([LAB, "hold", str(note), "3", "0.7", t + "/h.wav"], env=env, capture_output=True, text=True).stdout
        m = re.search(r"from 0.6 s: .*: (\w+)", out)
        sr, x = wavfile.read(t + "/h.wav")
    x = x.astype(float)[int(0.6 * sr):int(2.95 * sr)]
    f0 = 440 * 2 ** ((note - 69) / 12) * 2 ** (cents / 1200)
    beat, rate = modulation(x, sr, f0)
    # per-harmonic beating: band-pass each harmonic, its level's modulation
    X = np.fft.rfft(x)
    fr = np.fft.rfftfreq(len(x), 1 / sr)
    hb = []
    for h in range(1, 7):
        Y = np.where(np.abs(fr - h * f0) < 0.35 * f0, X, 0)
        hb.append(modulation(np.fft.irfft(Y, len(x)), sr, f0)[0])
    return beat, rate, m.group(1) if m else "?", hb


def modulation(x, sr, f0):
    """The level over whole periods (4 of f0: no ripple from frames that cut
    periods), every 5 ms; its 1-20 Hz modulation (dB) and the strongest rate."""
    hop = int(0.005 * sr)
    win = int(round(4 * sr / f0))
    e = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, "same")[::hop] + 1e-20)
    db = 20 * np.log10(e)
    db = db - db.mean()
    b, a = signal.butter(2, [1 / 100, 20 / 100], "band")  # 1-20 Hz at the 200 Hz frame rate
    mod = signal.filtfilt(b, a, db)
    beat = float(np.sqrt(np.mean(mod ** 2)) * np.sqrt(2))
    n = 1 << 14
    S = np.abs(np.fft.rfft(mod * np.hanning(len(mod)), n))
    f = np.fft.rfftfreq(n, 1 / 200)
    k = (f >= 1) & (f <= 20)
    rate = float(f[k][np.argmax(S[k])]) if beat > 1.0 else 0.0
    return beat, rate


def simple(a):
    """Notes x dynamics as the performer plays them (instrument_lab hold, the
    dynamics law, vibrato off): beat, rate and the stick-test regime."""
    lo, hi = (int(v) for v in a.notes.split("-"))
    over = dict(p.split("=") for p in a.param)
    dyns = [0.1, 0.45, 0.85]

    def one(job):
        n, d = job
        with tempfile.TemporaryDirectory() as t:
            env = dict(os.environ, SM_INSTRUMENT="cello", LAB_STRING=str(a.string), LAB_VIB="0", LAB_AUTO="0", LAB_DYN=str(d),
                       LAB_WOLF=over.get("body.wolf", "0"))
            out = subprocess.run([LAB, "hold", str(n), "3", "0.7", t + "/h.wav"], env=env, capture_output=True, text=True).stdout
            m = re.search(r"from 0.6 s: .*: (\w+)", out)
            sr, x = wavfile.read(t + "/h.wav")
        b, r = modulation(x.astype(float)[int(0.6 * sr):int(2.95 * sr)], sr, 440 * 2 ** ((n - 69) / 12))
        return job, (b, r, m.group(1) if m else "?")

    jobs = [(n, d) for n in range(lo, hi + 1) for d in dyns]
    with ThreadPoolExecutor(a.j) as ex:
        R = dict(ex.map(one, jobs))
    print("note  " + "".join(f"   d{d:.2f}: beat   rate  regime   " for d in dyns))
    for n in range(lo, hi + 1):
        print(f"{n:4d}  " + "".join(f"   {R[(n, d)][0]:4.1f} dB {R[(n, d)][1]:4.1f}  {R[(n, d)][2]:10s}" for d in dyns))


def main():
    global LAB
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", default=LAB)
    ap.add_argument("--string", type=int, default=1)
    ap.add_argument("--note", type=int, default=54, help="the note nearest the wolf mode (F#3, 185 Hz)")
    ap.add_argument("--vb", type=float, default=0.15)
    ap.add_argument("--force", type=float, default=1.2)
    ap.add_argument("--beta", type=float, default=0.09)
    ap.add_argument("--simple", action="store_true")
    ap.add_argument("--notes", default="50-56")
    ap.add_argument("--param", action="append", default=[])
    ap.add_argument("-j", type=int, default=2)
    a = ap.parse_args()
    LAB = a.lab
    if a.simple:
        return simple(a)
    pool = ThreadPoolExecutor(a.j)
    run = lambda kw: hold(a.note, a.string, **kw)
    base = dict(vb=a.vb, force=a.force, beta=a.beta)
    det = list(range(-150, 151, 25))

    print(f"G string" if a.string == 1 else f"string {a.string}", f"around note {a.note}; v_b {a.vb} m/s, F {a.force} N, beta {a.beta}")
    print("\n1. Where it wolfs (cents from the note): beat dB / rate Hz / regime")
    r1 = list(pool.map(run, [dict(base, cents=c) for c in det]))
    for c, (b, r, g, _) in zip(det, r1):
        print(f"   {c:+4d} c: {b:4.1f} dB {r:5.1f} Hz  {g}")
    worst = det[int(np.argmax([b for b, *_ in r1]))]
    wolfy = [c for c, (b, r, g, _) in zip(det, r1) if b > 1.5 or g != "helmholtz"]
    print(f"   worst at {worst:+d} c; wolfs from {min(wolfy) if wolfy else 0:+d} to {max(wolfy) if wolfy else 0:+d} c "
          f"(test: within about ±50 c of the mode)")

    print("\n2. Beat rate (Hz) at the worst note: rows bow speed, columns force")
    vbs, fs = [0.08, 0.15, 0.25], [0.8, 1.2, 2.0]
    r2 = list(pool.map(run, [dict(base, cents=worst, vb=v, force=f) for v in vbs for f in fs]))
    print("   v_b \\ F " + "".join(f"{f:9.1f} N" for f in fs))
    for i, v in enumerate(vbs):
        print(f"   {v:5.2f}    " + "".join(f"  {r2[i * 3 + j][1]:5.1f} ({r2[i * 3 + j][0]:3.1f})" for j in range(3)))
    print("   (beat dB in brackets; the rate counts where the beat is over 2 dB)")
    ok = bad = 0
    for i in range(3):
        for j in range(3):
            b0, r0 = r2[i * 3 + j][0], r2[i * 3 + j][1]
            if b0 < 2: continue
            if j + 1 < 3 and r2[i * 3 + j + 1][0] >= 2: (ok := ok + 1) if r2[i * 3 + j + 1][1] <= r0 else (bad := bad + 1)
            if i + 1 < 3 and r2[(i + 1) * 3 + j][0] >= 2: (ok := ok + 1) if r2[(i + 1) * 3 + j][1] >= r0 else (bad := bad + 1)
    print(f"   test: rate up with speed, down with force: {ok} of {ok + bad} neighbouring pairs agree")

    print("\n3. The worst note vs the contact point")
    fine = list(range(-50, 101, 10))
    worsts = []
    for b in (0.13, 0.09, 0.06):
        # Schelleng: the force for the same place in the window ~ 1/beta
        rr = list(pool.map(run, [dict(base, cents=c, beta=b, force=a.force * a.beta / b) for c in fine]))
        beats = [x[0] for x in rr]
        worsts.append(fine[int(np.argmax(beats))])
        print(f"   beta {b:4.2f} (F {a.force * a.beta / b:.2f} N): worst at {worsts[-1]:+d} c (beat {max(beats):.1f} dB)")
    print(f"   test: the worst note rises as beta falls: {'yes' if worsts[0] <= worsts[1] <= worsts[2] else 'no'}")

    print("\n4. The Wolf Eliminator at the worst note")
    r4 = list(pool.map(run, [dict(base, cents=worst, wolf=w) for w in (0.0, 0.5, 1.0)]))
    for w, (b, r, g, _) in zip((0.0, 0.5, 1.0), r4):
        print(f"   {w:3.1f}: {b:4.1f} dB, {g}")

    print("\n5. Each harmonic's beating at the worst note (dB), harmonics 1-6")
    hb = r1[det.index(worst)][3]
    print("   " + "  ".join(f"h{i + 1} {v:4.1f}" for i, v in enumerate(hb)),
          f"   odd mean {np.mean(hb[0::2]):.1f}, even mean {np.mean(hb[1::2]):.1f} (test: odd >> even)")

    print("\n6. Lower string impedance (x0.7, a lower-tension set): where it wolfs")
    r6 = list(pool.map(run, [dict(base, cents=c, z0=0.7) for c in det]))
    w6 = [c for c, (b, r, g, _) in zip(det, r6) if b > 1.5 or g != "helmholtz"]
    print(f"   Z0 x1: {len(wolfy)} of {len(det)} pitches wolf; Z0 x0.7: {len(w6)} (test: fewer)")


if __name__ == "__main__":
    main()
