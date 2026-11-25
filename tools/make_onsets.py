#!/usr/bin/env python3
"""The cello's recorded onsets: the catch and scratch of a real bow, without its note.

Generates model/data/CelloOnsets.h from the Karoryfer x bigcat cello (CC0,
github.com/sfzinstruments/karoryfer-bigcat.cello): every bowed sustain (17 notes C2-C6 a minor
third apart, p/mp/mf/f, down and up bow). For each:

1. The onset: the first 10 ms window within 25 dB of the peak, less 4 ms (as attack_sustain.py).
2. The note: f0 tracked every 2.5 ms; each harmonic heterodyned along the tracked phase and
   low-passed (a Hann window three periods long), as 10d6/strings' tools/harmonics.py does. The
   harmonic estimate is faded in over the 5 ms before the onset: a centred window would
   otherwise put the note's harmonics into the residual before the bow touches.
3. The residual: the recording less its harmonics, from 5 ms before the onset for 0.2 s (faded
   out over the last 80 ms): the bow's catch, its scratch and the body's knock.
4. Into bridge force: the synth adds the residual to the bridge force, so the body, mics and
   room act on it as on the note. To keep the recording's residual-to-harmonics balance in
   every band whatever bigcat's mic and body did, the residual is equalised by S(f)/H(f): H
   the recording's held harmonics (0.4-0.9 s after the onset, power per harmonic re their sum,
   smoothed over a third of an octave), S a sawtooth's (1/n^2, the Helmholtz bridge force).
   It is expressed re the held note's harmonic RMS, so the synth scales it by its own note's
   level (model/Onsets.h). Gains are limited to +-24 dB.
5. Odd takes: a clip whose first 80 ms sit more than 9 dB above its note's median (of the
   eight), or with a click (peak 22 dB over its first 0.1 s RMS), is replaced by the same note and bow at the nearest dynamic that isn't; the takes
   10d6/strings heard bounce (tools/onset_choice.json there: A2 p, F#3 mp/mf, C5 f, down-bow)
   the same way.
6. The tone's envelope: the recording's harmonic part's RMS (a one-pole on h^2, 2.5 ms, as
   the synth follows its own bridge force) re its held RMS, every 2.5 ms (kEnv). The synth
   scales a clip by its own envelope over this one (model/Onsets.h), so the scratch keeps the
   recording's noise-to-tone ratio moment by moment while the synth's note builds up: the
   synth's attack starts ~20 dB under its note (the swell), bigcat's near it.
7. Stored at 32 kHz (the treble to 16 kHz), mu-law 8-bit (the residual is noise ~20 dB under
   the note: the 38 dB of mu-law is plenty), with a scale and the recorded pitch per clip.

    python3 -I tools/make_onsets.py ~/data/bigcat model/data/CelloOnsets.h [--wav DIR]

--wav writes each residual (before step 4, at 44.1 kHz) for listening.
"""
import argparse
import os
import re
import sys

import numpy as np
import scipy.signal as sig
import soundfile as sf
from scipy.signal import fftconvolve

SR = 44100
OUT_SR = 32000
PRE = 0.005
LEN = 0.2
FADE = 0.08
ENV_RATE = 400  # Hz: the tone envelope's step (2.5 ms)
ENV_TAU = 0.0025  # s: its one-pole (the synth's follower, model/Instrument.h)
DYNS = ["p", "mp", "mf", "f"]
BOWS = ["d", "g"]
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
MU = 255.0
ODD_DB = 9.0
ODD_CREST = 22.0  # dB, peak re the first 0.1 s RMS: a click in the take
BOUNCED = {(45, "p", "d"), (66, "mp", "d"), (66, "mf", "d"), (84, "f", "d")}


def midi(name):
    m = re.match(r"([A-G])(b?)(\d)$", name)
    # bigcat names notes an octave low ("C1" is the open C string, C2)
    return 12 * (int(m.group(3)) + 2) + NAMES[m.group(1)] - (1 if m.group(2) else 0)


def mtof(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def onset(x):
    L, H = int(0.01 * SR), int(0.002 * SR)
    c = np.concatenate([[0.0], np.cumsum(x * x)])
    idx = np.arange(0, len(x) - L, H)
    lv = 10 * np.log10((c[idx + L] - c[idx]) / L + 1e-20)
    i = max(int(np.argmax(lv > lv.max() - 25)) - 2, 0)
    return i * H


def track_f0(x, f_guess, hop=0.0025):
    """f0 per sample: harmonics 1-8's peaks (parabolic) within a semitone of k f, weighted."""
    win = max(0.04, 6.0 / f_guess)
    n = int(win * SR)
    nfft = 1 << int(np.ceil(np.log2(n * 4)))
    w = np.hanning(n)
    times, f0s, lev = [], [], []
    for i in range(0, len(x) - n, int(hop * SR)):
        seg = x[i:i + n] * w
        S = np.abs(np.fft.rfft(seg, nfft))
        lev.append(20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-12))
        est, wt = [], []
        for k in range(1, 9):
            fk = k * f_guess
            if fk > 8000:
                break
            lo, hi = int(fk / 2 ** (1 / 12) * nfft / SR), int(fk * 2 ** (1 / 12) * nfft / SR) + 1
            j = lo + int(np.argmax(S[lo:hi]))
            if 0 < j < len(S) - 1:
                a, b, c = np.log(S[j - 1:j + 2] + 1e-12)
                d = 0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c != 0 else 0.0
                est.append((j + d) * SR / nfft / k)
                wt.append(S[j])
        times.append((i + n / 2) / SR)
        f0s.append(np.average(est, weights=wt) if est else np.nan)
    times, f0s, lev = map(np.array, (times, f0s, lev))
    f0s[lev < lev.max() - 40] = np.nan
    ok = np.isfinite(f0s)
    f = np.interp(times, times[ok], f0s[ok])
    return np.interp(np.arange(len(x)) / SR, times, f)


def harmonics(x, f0s, fmax=16000.0):
    """Harmonic part (heterodyned per harmonic, 3-period Hann) and each harmonic's |a|^2 track."""
    phi = 2 * np.pi * np.cumsum(f0s) / SR
    fm = float(np.median(f0s))
    K = int(min(fmax, SR / 2 - 200) // fm)
    n = int(max(3.0 / fm, 0.01) * SR) | 1
    w = np.hanning(n)
    w /= w.sum()
    h = np.zeros(len(x))
    pw = np.zeros((K, len(x)))
    # the files start at the attack: near the start the window is only partly over the
    # signal, so divide by how much of it is (else the harmonics leak into the residual)
    cover = np.maximum(fftconvolve(np.ones(len(x)), w, mode="same"), 0.5)
    for k in range(1, K + 1):
        ak = fftconvolve(x * np.exp(-1j * k * phi), w, mode="same") / cover
        h += 2 * np.real(ak * np.exp(1j * k * phi))
        pw[k - 1] = 2 * np.abs(ak) ** 2
    return h, pw, fm


def eq_gains(P, fm, freqs):
    """S(f)/H(f) per FFT bin (amplitude): P the held harmonics' powers re their sum."""
    n = np.arange(1, len(P) + 1)
    S = (1.0 / n ** 2) / (np.pi ** 2 / 6)
    gdb = 10 * np.log10(S / np.maximum(P, 1e-12))
    fk = n * fm
    # smooth over a third of an octave (in dB), then interpolate over log f
    sm = np.array([np.mean(gdb[np.abs(np.log2(fk / f)) <= 1 / 6]) for f in fk])
    sm = np.clip(sm, -24, 24)
    g = np.interp(np.log2(np.maximum(freqs, 1.0)), np.log2(fk), sm)
    return 10 ** (g / 20), sm


def mulaw(y):
    q = np.sign(y) * np.log1p(MU * np.abs(y)) / np.log1p(MU)
    return np.clip(np.round(q * 127), -127, 127).astype(np.int8)


def unmulaw(q):
    q = q.astype(float) / 127
    return np.sign(q) * ((1 + MU) ** np.abs(q) - 1) / MU


def one(path, m):
    x, sr = sf.read(path, dtype="float64")
    assert sr == SR
    if x.ndim > 1:
        x = x.mean(axis=1)
    on = onset(x)
    pre = int(PRE * SR)
    a = max(on - pre, 0)
    seg = x[a:a + int(1.0 * SR)]
    f0s = track_f0(seg, mtof(m))
    h, pw, fm = harmonics(seg, f0s)
    o = on - a
    ramp = np.clip((np.arange(len(seg)) - (o - pre)) / max(pre, 1), 0, 1)
    r = seg - h * ramp
    # the held note: 0.4-0.9 s after the onset
    s0, s1 = o + int(0.4 * SR), o + int(0.9 * SR)
    P = pw[:, s0:s1].mean(axis=1)
    tot = P.sum()
    held_rms = np.sqrt(np.mean(h[s0:s1] ** 2))
    L = int(LEN * SR)
    res = r[:L].copy()
    fade = int(FADE * SR)
    res[-fade:] *= 0.5 * (1 + np.cos(np.pi * np.arange(fade) / fade))
    res[:int(0.001 * SR)] *= np.linspace(0, 1, int(0.001 * SR))
    raw = res / held_rms
    # equalise into bridge force (zero-phase, padded)
    N = 1 << int(np.ceil(np.log2(L * 2)))
    freqs = np.fft.rfftfreq(N, 1 / SR)
    g, sm = eq_gains(P / tot, fm, freqs)
    y = np.fft.irfft(np.fft.rfft(raw, N) * g, N)[:L]
    # pitch of the onset's note: f0 over 50-200 ms after the onset (what it is transposed from)
    hz = float(np.median(f0s[o + int(0.05 * SR):o + int(0.2 * SR)]))
    hh = (h * ramp)[:L]
    a1 = np.exp(-1 / (ENV_TAU * SR))
    env2 = sig.lfilter([1 - a1], [1, -a1], hh ** 2)
    env = np.sqrt(np.maximum(env2, 0)) / held_rms
    step = SR / ENV_RATE
    envs = env[(np.arange(int(LEN * ENV_RATE)) * step).astype(int)]
    y32 = sig.resample_poly(y, 320, 441)  # 44.1 -> 32 kHz
    return dict(y=y32, raw=raw, hz=hz, held=held_rms, eq=sm, on=on, env=envs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bigcat")
    ap.add_argument("header")
    ap.add_argument("--wav", default="")
    a = ap.parse_args()
    d = os.path.join(a.bigcat, "Samples", "sus")
    names = sorted({f.split("_")[0] for f in os.listdir(d) if f.endswith(".wav")}, key=midi)
    notes = [midi(n) for n in names]
    clips = {}
    for nm, m in zip(names, notes):
        for dy in DYNS:
            for b in BOWS:
                p = os.path.join(d, f"{nm}_{dy}_{b}.wav")
                c = one(p, m)
                clips[(m, dy, b)] = c
                ratio = 10 * np.log10(np.mean(c["raw"][:int(0.08 * SR)] ** 2) + 1e-20)
                print(f"{nm:4s}({m}) {dy:2s} {b}: f0 {c['hz']:7.2f} ({1200 * np.log2(c['hz'] / mtof(m)):+5.1f} c), "
                      f"residual 0-80 ms {ratio:+5.1f} dB re the held note, eq {c['eq'].min():+5.1f}..{c['eq'].max():+5.1f} dB",
                      file=sys.stderr)
                if a.wav:
                    os.makedirs(a.wav, exist_ok=True)
                    sf.write(os.path.join(a.wav, f"{m}_{dy}_{b}.wav"),
                             c["raw"] / max(np.abs(c["raw"]).max(), 1e-9) * 0.5, SR, subtype="PCM_16")
    # odd takes -> the same note and bow at the nearest dynamic that isn't odd
    lev = {k: 10 * np.log10(np.mean(c["raw"][:int(0.08 * SR)] ** 2) + 1e-20) for k, c in clips.items()}
    odd = set(BOUNCED)
    for m in notes:
        med = np.median([lev[(m, dy, b)] for dy in DYNS for b in BOWS])
        odd |= {(m, dy, b) for dy in DYNS for b in BOWS if lev[(m, dy, b)] > med + ODD_DB}
    for k, c in clips.items():
        y = c["y"]
        crest = 20 * np.log10(np.abs(y).max() / np.sqrt(np.mean(y[:int(0.1 * OUT_SR)] ** 2)))
        if crest > ODD_CREST:
            odd.add(k)
            print(f"click in {k}: crest {crest:.1f} dB", file=sys.stderr)
    use = {}
    for k in clips:
        if k in odd:
            m, dy, b = k
            i = DYNS.index(dy)
            cand = sorted((abs(j - i), j) for j in range(4) if (m, DYNS[j], b) not in odd)
            use[k] = (m, DYNS[cand[0][1]], b)
            print(f"odd take {k} ({lev[k]:+.1f} dB) -> {use[k]}", file=sys.stderr)
        else:
            use[k] = k
    n = len(clips[(notes[0], "p", "d")]["y"])
    lines = ["// Generated by tools/make_onsets.py from the Karoryfer x bigcat cello (CC0 1.0,",
             "// Karoryfer Lecolds and bigcat instruments; cellist Kamila Borowiak). Do not edit.",
             "// Each clip: the residual of a bowed sustain's first 0.2 s (its harmonics removed),",
             "// equalised into bridge force and expressed re the held note's harmonic RMS;",
             "// mu-law 8-bit at kRate. Order: note, dynamic (p mp mf f), bow (down, up).",
             "#pragma once", "#include <cstdint>", "", "namespace sm::onsets {", "",
             f"constexpr int kRate = {OUT_SR};", f"constexpr int kLen = {n};",
             f"constexpr int kNotes = {len(notes)};", "constexpr int kDyns = 4;", "constexpr int kBows = 2;",
             f"constexpr int kPre = {int(round(PRE * OUT_SR))};  // samples before the recorded onset",
             "constexpr int kMidi[kNotes] = {" + ", ".join(map(str, notes)) + "};",
             "constexpr int kDynVel[kDyns] = {40, 60, 80, 100};  // the velocity each layer stands for",
             f"constexpr int kEnvRate = {ENV_RATE};  // the tone envelope's steps per second",
             f"constexpr int kEnvLen = {int(LEN * ENV_RATE)};", ""]
    scales, hzs, data, envs = [], [], [], []
    for m in notes:
        for dy in DYNS:
            for b in BOWS:
                c = clips[use[(m, dy, b)]]
                s = float(np.abs(c["y"]).max())
                q = mulaw(c["y"][:n] / s)
                scales.append(s)
                hzs.append(c["hz"])
                data.append(q)
                envs.append(c["env"])
    lines.append("// Peak of each clip (re the held note's RMS) and its recorded pitch (Hz).")
    lines.append("constexpr float kScale[kNotes * kDyns * kBows] = {")
    for i in range(0, len(scales), 8):
        lines.append("  " + ", ".join(f"{v:.6e}f" for v in scales[i:i + 8]) + ",")
    lines.append("};")
    lines.append("constexpr float kHz[kNotes * kDyns * kBows] = {")
    for i in range(0, len(hzs), 8):
        lines.append("  " + ", ".join(f"{v:.7e}f" for v in hzs[i:i + 8]) + ",")
    lines.append("};")
    lines.append("// The recording's tone envelope re its held RMS (kEnvRate steps).")
    lines.append("constexpr float kEnv[kNotes * kDyns * kBows * kEnvLen] = {")
    fe = np.concatenate(envs)
    for i in range(0, len(fe), 10):
        lines.append("  " + ", ".join(f"{max(v, 1e-6):.3e}f" for v in fe[i:i + 10]) + ",")
    lines.append("};")
    lines.append("constexpr int8_t kData[kNotes * kDyns * kBows * kLen] = {")
    flat = np.concatenate(data)
    for i in range(0, len(flat), 40):
        lines.append(",".join(str(int(v)) for v in flat[i:i + 40]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace sm::onsets")
    os.makedirs(os.path.dirname(a.header) or ".", exist_ok=True)
    open(a.header, "w").write("\n".join(lines) + "\n")
    print(f"wrote {a.header}: {len(data)} clips x {n} samples ({len(flat) / 1e6:.2f} MB)", file=sys.stderr)


if __name__ == "__main__":
    main()
