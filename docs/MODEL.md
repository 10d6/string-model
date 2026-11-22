# The model

String Model is a physical model of a bowed string instrument. The physics
follows the knowledge base in
[10d6/instrument-research](https://github.com/10d6/instrument-research)
(`docs/bowed_string_physical_modeling_kb.md`, cited here as **KB §n**).
Values marked **[T]** are tuning choices made here, not measurements.

```
keys, CCs ─► Performer (gesture layer)            model/Performer.h
              v_b, F_b per string, beta, finger pitch, vibrato
                 │
                 ▼
   ┌──── 4 × BowedString (waveguide, oversampled) ────┐   model/BowedString.h
   │  nut/finger ◄─ dN ─► [bow junction] ◄─ dB ─► bridge │   model/Friction.h
   └─────────────────────────────────────────────────────┘
                 │ bridge force (N), decimated              model/Instrument.h
                 ▼
   body/output: CC0 library-mic response (convolution) or raw pickup
                 ▼
   room (synthkit dsp::Reverb)                               src/Synth.cpp
```

## Where it runs

The instrument is a single object owned by the synth, running in
`processGlobal`. `processEvents` takes every note and controller for it. The
kit's voices only track keys (for the meter and the lit keys) and make no
sound. This is because the strings will share one bridge (Phase 2:
sympathetic ringing, cello wolf notes). Per-note voices render separately in
32-sample blocks and couldn't do that.

The strings run at 2× the host rate below 70 kHz (96 kHz at 48 kHz). A
49-tap Blackman–Harris FIR decimates them back to the host rate.

## String (KB §6.1, §10)

Four one-way fractional delay lines (4-point Lagrange) carry velocity waves:
bow→nut, nut→bow, bow→bridge and bridge→bow. Losses are lumped into the
terminations as −g × a one-pole low-pass.

- **Lengths:** the bridge side is β × the note's period. It doesn't move with
  vibrato, because vibrato moves only the finger (KB §11.2). The filters'
  phase delay at the pitch is subtracted from the loop, so the loop is in
  tune. Both lengths glide over about 1 ms.
- **Losses [T]:** g is set from a T60 for the low partials. Open strings get
  2.2 s; stopped strings get 0.9 s, since the finger is far lossier than the
  nut (KB §2.3: η_F × 3). The low-pass corners are: bridge 9 kHz, nut
  12 kHz, finger 5 kHz.
- **Torsion (KB §3.6):** the bow sees 1/Z_eff = 1/Z₀ + 1/Z_R, with Z_R ≈
  3.5 Z₀ [T]. The torsional part is **high-passed** at 2× the pitch. A pure
  resistance (the KB's "lumped Z_eff") lets a steady bow spin the string
  forever, so the string never slips; that was the first bug found here.
  Only f/(2Z₀) goes into the transverse waves.
- **Bridge force** = Z₀ (incoming − reflected) at the bridge end.
- **Sleep:** a string with the bow off that has decayed below 1e-7 m/s for
  2048 samples stops running.

String impedances (KB §2.1, Z₀ = μc mid-range): G 0.33, D 0.24, A 0.20,
E 0.19 kg/s.

## Friction (KB §3)

**Thermal (default): Woodhouse 2003 (KB §3.2a).** μ depends only on contact
temperature: 1.2 at ambient, falling smoothly to 0.35 at 40 K. With μ fixed
at each instant, the junction (string line v = v_h + f/2Z_eff against
|f| ≤ F_b μ) has one solution, found in closed form. There's no hysteresis
rule; hysteresis comes from the thermal lag. The temperature is updated
implicitly, so it's stable at any step size. Heat leaves the contact by four
paths:

| Path | Model |
|---|---|
| Rosin layer storage | C₀ = ρc · A · δ (A ∝ F_b, a = 250 µm at 1 N, δ = 1 µm) |
| Conduction into the string | half-space disk: A·λ_s·√s, a Foster network of 7 RC branches (τ = 20 µs × 5ᵏ up to 0.3 s), + 4Ka at long times |
| The bow hair | **moving heat source**: the hair passes through the contact at v_b, so each bit is heated for 2a/v_b. Conductance λ_bow · A · √(v_b / 2πa) |
| Rosin carried through | 2aδρc (v_b + slip) |

The moving-hair term isn't in the KB's summary. Without it, fast heavy
strokes overheated and slid continuously (0.3 m/s, 1.2 N, β 0.05 on the A
string: 68 K, no stick).

**Heat-path calibration [T]: ×3** on the conductive paths. With ×1, ff
strokes (β 0.03–0.04, 2–2.5 N) still lost Helmholtz motion. With ×3 they
keep it, and contact temperatures swing about 7–46 K over a period. Woodhouse
reports 17–31 K.

**Classic** (the `Friction Model` switch): the hyperbolic curve μs 0.8,
μd 0.3, v₀ 0.15 m/s, with the hysteresis rule, solved in closed form
(KB §3.4).

**Rosin noise (KB §6.4):** during slip, μ × (1 − A·u) with u uniform in [0, 1).

### Measured with `string_lab` (open D, v_b = 0.13 m/s, thermal, 96 kHz)

```
beta \ F    0.02  0.04  0.08  0.15  0.30  0.60  1.20  2.40  4.80
   0.020     r-99   r-99   r-99   r-99   m -3   m -3   H -6   H-14   r +2
   0.030     r-99   r-99   r-99   r -2   m -4   H -5   H-10   r-29   r+99
   0.045     r-99   r-99   r-99   m -3   H -4   H -8   r-18   r+23   r-99
   0.065     r-99   r-99   m -2   r -3   H -6   H-13   r-35   r+29   r-99
   0.090     r-99   r-99   m -3   H -4   H -9   r-21   r-79   r-99   r-99
   0.130     r-99   r -2   H -3   H -6   r-15   r-41   r+68   r-99   r+99
   0.180     r-99   m -2   H -4   H -7   H-15   m -7   r+99   r+99   r+99
```

Key: H = Helmholtz motion (one slip per period, pitch within 15 cents), m =
multiple slipping, r = anything else. The number is the pitch in cents. Many
`r` cells next to `H` are still Helmholtz motion that has flattened by more
than 15 cents.

What this shows:
- A Schelleng wedge: the window narrows towards the bridge and moves up in
  force.
- Flattening grows with force (KB §1.2).
- Demoucron's measured pp / mf / ff points (β 0.16 / 0.07 / 0.04 at
  0.17 / 0.47 / 1.15 N) are all inside, or at the edge, flattened.

## Gesture layer (KB §7.3, §11.5)

**Keys.** Monophonic with last-note priority:
- Overlapping keys **slur**: the same bow, the finger shifts with an S-curve
  over 8 ms + `Shift Time` × (interval / 5 st, max 1).
- A key played after all keys are up starts a **new stroke** in the other
  bow direction.
- With `Legato Slurs` off, every key is a bow change.
- The string is chosen per note: the highest string whose open note is at or
  below it. Through a slur, the current string is kept if the note is within
  a fifth above its open note.
- Leaving a string lifts the bow from it and lets it ring.

**Playability assistant.** `Dynamics` (mod wheel, CC 11, plus velocity ×
`Velocity → Dynamics`) gives d ∈ [0, 1]. Then:
- v_b/β = 0.6 e^{3d}: the level law L ≈ 7.7 ln(v_b/β), about 23 dB from
  pp to ff.
- v_b = 0.1 e^{1.4d} m/s, so 0.1–0.4 m/s and β 0.17–0.03.
- `Contact Point` (CC 74) multiplies β by 2^(−1.5 c).
- The force follows the players' line through the Schelleng diagram:
  F = F_ref (v_b / 0.13) (β / 0.07)^−1.39 (Z / 0.22)^1.39. F_ref = 0.35 N
  [T], trimmed from Demoucron's 0.47 N to sit mid-window in this model.
  F is capped at 3 N.
- `Pressure` (+ aftertouch × `Aftertouch → Pressure`) multiplies F by
  2^(1.5 p).

**Attacks.** The bow lands with force rising over 8 ms and accelerates at
a = k F (the Guettler plane, KB §7.2), with k = (6 + 8 vel)(1 + 1.5 bite)
m/s² per N. Loud notes also get an accent, a force boost decaying over 70 ms.

**Bow changes.** The bow reverses at the acceleration that takes `Bow Change`
for a full reversal. Force dips to 20% at zero speed (0.2 + 0.8 (v/v_t)²).
Players lighten the bow here, and the KB reports that a deeper dip shortens
the gap. Measured at a mid-note change at the tip: about 26 ms more than
20 dB down, close to the KB's 16–28 ms. The bow position along the hair is
tracked (62 cm), so long notes change bow at the tip and the frog.

**Release.** Key up lifts the bow: the force falls over 25 ms and the string
rings.

**Vibrato (stopped notes only).** `Vibrato Depth` is peak-to-peak at mf,
scaled 0.6–1.4× by dynamics; the rate is ×0.9–1.1. It ramps in over 350 ms
after `Vibrato Delay`, with ±5% rate and ±15% depth drift per cycle.

**Intonation (the player's ear).** Bowing flattens the pitch, by about 10 cents
at mf here. On stopped notes, the time between slip onsets is compared with
the wanted period, and the finger moves to correct it. The correction is
`Intonation` × 1.6% of the error per period, within ±60 cents, and only while
the motion is steadily periodic (±4%). Open strings stay flat, as on a real
violin.

## Output

- **Library Mic** (stereo) and **Library Mic (Smooth)** (mono, the default):
  the CC0 bridge-force → recording responses measured from VSCO-2-CE solo
  violin, from instrument-research `data/library_mic_responses`.
  - They include that library's body, microphones and room.
  - `tools/gen_ir.py` turns `data/violin/*.wav` into `src/ir/ViolinIR.h`.
  - They're resampled from 48 kHz in `prepare`.
  - The response has a strong peak around 540–590 Hz (+14 dB, near the
    B1+ body mode) and a dip around 700–770 Hz (−9 dB). Fundamentals of
    neighbouring notes in that range differ by up to ~12 dB. That's
    measured, but the low-frequency resolution there is only about ¼ octave.
- **Pickup:** the raw bridge force.

## Tools

```
./build/string_lab tone <string 0-3> <note> <vb> <F> <beta> [classic] [out.wav]
./build/string_lab sweep <string 0-3> <note> <vb> [classic]          # Schelleng map
LAB_FS=192000 LAB_HEAT=2 ./build/string_lab ...                      # rate, heat calibration
./build/instrument_lab [legato] [intonation]                         # gesture-layer trace
python3 tools/analyze.py render.wav [out.png]                        # spectrogram, level, pitch per note
```

## Known limits (next phases)

- No bridge coupling yet: other strings ring only after being played; no
  sympathetic ringing (Phase 2).
- The body is a fixed measured response, not a model: no control over the
  bridge hill, mute, or the statistical high band (Phase 3).
- No double stops, pizzicato, harmonics, spiccato, tremolo or martelé yet
  (Phases 4–5).
- The highest notes have few samples per slip even at 96 kHz (KB §6.4); they
  slip longer and flatten more (A6: ~30 cents uncorrected).
- Cost: the instrument itself is about 2% of a native core. The browser
  renders 2.6× faster than realtime, mostly in the convolution and the room.
