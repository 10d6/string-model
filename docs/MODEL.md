# The model

String Model is a physical model of a bowed string instrument. The physics
follows the knowledge base in
[10d6/instrument-research](https://github.com/10d6/instrument-research)
(`docs/bowed_string_physical_modeling_kb.md`, cited here as **KB §n**).
Values marked **[T]** are tuning choices made here, not measurements.

```
keys, CCs ─► Performer (gesture layer)            model/Performer.h
              v_b, F_b per string, beta, finger pitch, vibrato, chords
                 │
                 ▼
   ┌──── 4 × BowedString (waveguide, oversampled) ────┐   model/BowedString.h
   │  nut/finger ◄─ dN ─► [bow junction] ◄─ dB ─► ─┐   │   model/Friction.h
   └───────────────────────────────────────────────│───┘
                       shared bridge: modal Y(z) ◄─┘        model/Bridge.h
                 │ bridge force (N), decimated              model/Instrument.h
                 ▼
   body/output: modelled radiation (stereo modes)      model/Radiation.h
                or CC0 library-mic response (convolution), or raw pickup
                 ▼
   room (synthkit dsp::Reverb)                               src/Synth.cpp
```

## Where it runs

The instrument is a single object owned by the synth, running in
`processGlobal`. `processEvents` takes every note and controller for it. The
kit's voices only track keys (for the meter and the lit keys) and make no
sound. This is because the four strings share one bridge (sympathetic
ringing, double stops; the cello's wolf note later), solved sample by sample.
Per-note voices render separately in 32-sample blocks and couldn't do that.

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
- **The loop's gain at DC must not exceed its gain at the fundamental.**
  Phase 1 raised g to make up for the low-passes. On high notes that pushed
  the loop's DC gain to ~1, and the waveguide then held a DC wave: a uniform
  slope that a string fixed at both ends can't have. The bow's one-way drag
  fed it, and it barely decayed (the open E "rang" for ~15 s at DC, hidden by
  the output DC blocker but offsetting the velocity the bow sees). Now g
  isn't raised, so high notes decay a little faster, as with a constant Q.
  Two fixes that failed, for the record:
  - Blocking DC in the loop breaks Helmholtz motion. The bow's mean drag is
    held by a static deflection, which in a waveguide is a DC wave in each
    segment.
  - Making the ends lossless only at DC made a ~5 Hz resonance.
- **Open strings are tuned a little sharp at rest [T].** Violinists tune
  while bowing, and bowing flattens (measured at mf: G −11, D −6, A −15,
  E −20 cents). The open strings sit +7 / +4 / +9 / +12 cents at rest.
  Bowed, they're within ~9 cents. That's 60% compensation; the full amount
  would leave them too sharp to ring with the (corrected) stopped notes.
- **Torsion (KB §3.6):** the bow sees 1/Z_eff = 1/Z₀ + 1/Z_R, with Z_R ≈
  3.5 Z₀ [T]. The torsional part is **high-passed** at 2× the pitch. A pure
  resistance (the KB's "lumped Z_eff") lets a steady bow spin the string
  forever, so the string never slips; that was the first bug found here.
  Only f/(2Z₀) goes into the transverse waves.
- **Bridge end:** the string meets the moving bridge (below). The incoming
  wave a (after the string's own loss) reflects as r = u − a, where u is the
  bridge velocity, and pushes with F = Z₀(a − r) = Z₀(2a − u).
- **Sleep:** a string with the bow off that has decayed below 1e-7 m/s for
  2048 samples stops running. It wakes when the bridge moves.

## Bridge (KB §5.1–5.3)

One driving-point admittance Y = v/F in the bowing direction, shared by the
four strings: a passive modal filter bank (`model/Bridge.h`),
Y(s) = Σ (1/m_k) s / (s² + (ω_k/Q_k) s + ω_k²). Each mode has a positive
mass, so Y is positive real. The bilinear transform, pre-warped at each
mode, keeps it passive. Each mode's biquad has a direct term, so the strings
and bridge are solved together each sample without a delay:

    F = Σ Z_i (2 a_i − u) = S − Zs·u,   u = Y0·F + h   ⇒   u = (Y0·S + h) / (1 + Y0·Zs)

This is the reflectance −1 + 2Z₀Y of KB §5.1, solved exactly. A sleeping
string still loads the bridge (its Z is in Zs) and wakes when the bridge
moves; that's how open strings ring in sympathy.

**Violin admittance [T]:**

| Mode | Hz | Q | peak (s/kg) |
|---|---|---|---|
| A0 (air, weak at the bridge) | 280 | 40 | 0.024 |
| CBR (weak) | 400 | 30 | 0.030 |
| B1− | 460 | 40 | 0.15 |
| B1+ | 550 | 40 | 0.18 |
| | 660 / 820 | 30 | 0.06 / 0.072 |
| ~1 kHz cluster | 1000 / 1150 | 25 | 0.12 / 0.09 |

Above those is a dense band from 1.3 to 7 kHz: random modes (deterministic
seed) about 150 Hz apart, Q falling from ~40 to 8. Their levels are shaped
by the bridge hill: 2.6 kHz, Q 3, peak 0.15 s/kg (KB §5.3). That's 45 modes
in all. `Body Coupling` scales Y (0 = rigid bridge).

**How the levels were set.** My first guesses were the upper half of the
KB's ranges (B1± 0.25–0.30, hill 0.35), and they were too strong:
- 4·Z₀·Re(Y) took ~16% of the energy per reflection at 1–4 kHz. That gives
  partials a Q of ~40, made the coupled instrument 4–10 dB quieter and
  darker, and stopped A6 from sustaining.
- Notes matching an open string's harmonic became wolf-like at pp. Under
  D5, the open D rang only 8 dB below the bowed string, and soft D5 lost
  9 dB.

With the values above, coupled versus rigid costs 0–3 dB on most notes.
Measured with `instrument_lab sympathy`:

| Bowed | Open string response |
|---|---|
| G4 (D string) | open G (2nd harmonic) rings +1.6 dB, vs −27 dB under G#4 |
| D5 (A string) | open D −7.8 dB, open G −15 dB; under D#5 both ≈ −27 dB |
| E4 (D string) | open E −7 dB |

Two exactly tuned high-Q resonances exchange energy efficiently even
through a weak coupling, which is why sympathy is so audible on real
violins.

String impedances (KB §2.1, Z₀ = μc mid-range): G 0.33, D 0.24, A 0.20,
E 0.19 kg/s.

**High-frequency loss per string [T]:** the termination low-passes are
scaled by G 0.55, D 0.7, A 0.85, E 1. Thicker, wound strings lose their high
partials faster, since internal viscous loss grows with frequency and
diameter (KB §2.3). Without this, D5 sul G came out brighter than D5 on the
A string. Now A4 sul D is clearly darker than the open A (spectral centroid
2359 vs 2723 Hz). D5 sul G is close to D5 on the A string (2311 vs
2184 Hz); its remaining brightness is the extra force high positions on a
thick string need. After this change the radiation target was re-measured
and the force calibration re-run (both unchanged in quality).

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

**Keys.** Last-note priority, plus double stops and chords:
- Overlapping keys **slur**: the same bow, the finger shifts with an S-curve
  over 8 ms + `Shift Time` × (interval / 5 st, max 1).
- A key played after all keys are up starts a **new stroke** in the other
  bow direction.
- With `Legato Slurs` off, every key is a bow change.
- The string is chosen per note: the highest string whose open note is at or
  below it. Through a slur, the current string is kept if the note is within
  a fifth above its open note.
- Leaving a string lifts the bow from it and lets it ring. After 0.25 s
  [T] the finger lifts too, so the string rings open, and in sympathy.
- **Double stops and chords.** Keys pressed within `Chord Window` (35 ms) of
  each other are one chord.
  - The notes go on adjacent strings, lowest note on the lowest string, at
    the lowest positions, with the hand spanning at most a fifth.
  - If the whole chord doesn't fit (C4–E4–G4: G4 is below the open A), its
    top notes are played instead (E4 on G, G4 on D).
  - Two notes share the bow.
  - Three or four are **rolled** (KB §11.2): the bow takes the lower pair,
    then moves up a pair every `Chord Roll` (60 ms) and sustains the top
    pair, while the lower strings ring.
  - Releasing a chord note leaves the bow on the rest.
  - A key outside the window slurs out of the chord.
  - Measured with `instrument_lab chord`: every pair tried (G–D, D–A, A–E;
    open and stopped; 3–4 note chords) gives one slip per period on both
    strings, at their own pitches.

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
- **Calibrated against the coupled instrument** (`tools/calibrate_force.py`).
  The script sweeps Pressure for notes on every string at four dynamics and
  reports the range with clean Helmholtz motion. Four corrections followed:
  - **A margin**, most at pp: log₂ F += 1.5 (0.1 + 0.8 (1 − d)²). Soft notes
    sat at the bottom of the window.
  - **The E string × 1.22.**
  - **High positions:** × (max(1, f/(1.5 f_open)))^0.7. Short, high strings
    are loaded by the body. A6 went from −19 cents and unstable to −3 cents.
  - **Body load:** log₂ F += 1.5 · min(1, 25 Z₀ |Y(f₀)|) for notes whose
    fundamental sits on a body mode. C#5 is on the 550 Hz B1+ mode and
    needed +0.7 pressure.
- After these corrections, every note in the sweep (each string's open
  note, third and fifth, at dynamics 0–0.9) is clean at the default
  pressure, with at least ±0.3 of margin on both sides.
- `Pressure` (+ aftertouch × `Aftertouch → Pressure`) multiplies F by
  2^(1.5 p).

**Attacks.** The bow lands with force rising over 8 ms and accelerates at
a = k F (the Guettler plane, KB §7.2), with k = (6 + 8 vel)(1 + 1.5 bite)
m/s² per N. The acceleration is at least 2.5 m/s² [T]: below about
2 m/s², soft notes (pp force ≈ 0.07 N) took 0.25 s to speak; from 2 m/s²
they settle within ~50 ms. Loud notes also get an accent, a force boost
decaying over 70 ms.

**Long notes save bow** (KB §7.3 step 2; Demoucron measured 12 cm/s on whole
notes against 54 cm/s on quarters). From 0.25 s into a stroke, over 0.6 s,
the bow slows to `Long-Note Bow Speed` (default 0.6) × the attack speed. β
moves towards the bridge to keep v_b/β, the loudness, the same, but no
closer than β 0.06. Below that the note gets slightly softer instead:
Demoucron's players stayed 25–40 mm from the bridge on long strokes, and
re-attacks at bow changes get very hard near β 1/25 (KB §7.2). Allowed down
to β 0.025, a bow change stayed > 6 dB down for ~90 ms.

**Bow changes.** The bow position along the hair is tracked (62 cm). A long
note changes bow at the tip or the frog; `Auto Bow Change` off gives an
endless bow. The bow reverses in `Bow Change` (default 20 ms) at constant
acceleration. Force dips towards 20% at zero speed (0.2 + 0.8 (v/v_t)²):
players lighten the bow here, and the KB reports that a deeper dip shortens
the gap.

Measured above 1 kHz (where the body boosts) at each change, over 35 held
notes (D4–A5, five velocities): mean dip 12.5 dB, more than 6 dB down for
15 ms on average (30 ms at most), with changes about every 4–5 s at mf.

What made the difference:
- The first version (60 ms reversals, no bow saving) changed every 2.5 s
  with 27–31 dB, 50–70 ms cuts.
- Faster reversals hardly help, and the force dip's shape barely matters.
  This matches the KB's simulations, where even a halved bow change left a
  ~12 ms gap.
- What does matter: a playable β on long notes and enough force (the
  calibration above).

**Controls are smoothed by the performer, not the kit.** The instrument's
parameters have `smoothing(0)`, so they step at the controller's own sample
whatever the host block size. The performer smooths dynamics, pressure and
contact (15 ms) on its own fixed 16-sample clock. Bowing is chaotic: the kit's
per-control-block smoothing made renders differ by 1.9 dB between block sizes.

**Release** (`Release`; KB §11.2: three releases that sound very different):
- **Lift:** the force falls over 25 ms and the string rings.
- **Stop on String:** the bow decelerates to rest in ~40 ms with its force
  held. Once nearly still, the contact turns into a damper over 15 ms: a
  point resistance R = 2Z₀ (matched, so it absorbs best) in place of the
  friction junction. The bow lifts after 150 ms.
  - A rigid stick would only pin the string at the bow point (a node), and
    both halves rang on at 1–2 kHz. That was the first version.
  - Measured (`instrument_lab release`): −40 dB in ~270 ms, against
    0.5–1.3 s for a lift. Through the synth, it's −26 dB 100 ms after the
    key-up, against −8 dB.
- **Finger Off:** the bow lifts, and 40 ms later the playing finger lifts,
  so the ringing note falls to the open string (B4 → A4, E4 → D4).
- **Auto (default):** uses the key-up velocity. Above 0.7 (a quick release)
  it stops on the string; otherwise it lifts. Keyboards that don't measure
  key-up speed send 64, which lifts.
- A note played during a stop starts a new stroke from rest.

**Shifts** (KB §11.2 "Shifts, portamento"). A slur on the same string slides
the finger with an S-curve over 8 ms + `Shift Time` × min(1, interval/5 st)
× (1.6 − 1.2·velocity) [T]: a gentle key slides slowly (×1.4 at 0.2), a hard
one quickly (×0.4 at 1). The finger's speed along the string,
v = |dx/dt| with x = L(1 − 2^(−p/12)) and L = 0.328 m, does two things:
- **Lighter finger:** the stopped T60 is divided by (1 + 1.5 v) [T]. The
  note dips ~6 dB during a slow 5-semitone slide.
- **Winding noise** on the wound strings (G 1, D 1, A 0.7, E 0) [T]:
  - White noise band-passed (Q 2) at v / 0.12 mm (the turns of winding the
    finger crosses), with level `Slide Noise` × 0.3 × v.
  - It's injected at the finger end. Since a sticking bow reflects it back
    toward the finger, it also goes straight into the bridge force (in
    reality it also reaches the body through the neck and the string's
    other wave types).
  - Measured during a slow shift: about −20 dB re the note at `Slide Noise`
    1, −23 dB at 0.4 (not linear: the noise also nudges the chaotic bowing).

**String choice** (`String`: Auto / G / D / A / E). A chosen string plays
every note from its open note up two octaves; others fall back to Auto.
**Keyswitches** (`String Keyswitches`, on): C1 Auto, C#1 G, D1 D, D#1 A,
E1 E. They set the `String` parameter (`ctx.setParam`, so the page and host
follow) and don't sound. Checked with `LAB_STRING`: G-string notes up to
G5 and D-string notes up to G5 keep clean Helmholtz motion at soft and loud
dynamics. Forces on the high G string reach the 3 N cap at mf and above.

**Vibrato (stopped, bowed strings).** `Vibrato Depth` is peak-to-peak at mf,
scaled 0.6–1.4× by dynamics; the rate is ×0.9–1.1. It ramps in over 350 ms
after `Vibrato Delay`, with ±5% rate and ±15% depth drift per cycle.

**Intonation (the player's ear).** Bowing flattens the pitch, by about 10 cents
at mf here. On stopped notes, the time between slip onsets is compared with
the wanted period, and the finger moves to correct it. The correction is
`Intonation` × 1.6% of the error per period, within ±60 cents, and only while
the motion is steadily periodic (±4%). Open strings stay flat, as on a real
violin.

## Output (KB §12)

**Modelled (default): `model/Radiation.h`.** A bank of body modes driven by
the total bridge force (KB §12.3a):
- 16 per octave from 180 Hz to 1 kHz (Q 22), and 24 per octave from 1 to
  10 kHz (Q ~40, falling slowly), with a little random jitter: 120 modes.
- Each is a peak-normalised band-pass. Each channel mixes the modes with its
  own residues.
- **Low band (below 600 Hz):** a monopole, the same residues left and right
  (KB §5.5).
- **High band (above 1.2 kHz):** each channel's residues get an independent
  random sign and level (±4 dB), blended in between. Overlapping modes
  interfere differently in each ear, so the response changes from semitone
  to semitone and differs left and right ("directional tone colour"). Above
  1 kHz, left and right differ by 4–9 dB RMS, and each has 3–6 dB of fine
  structure per 1/3 octave.
- **Low cut:** a 4th-order high-pass at 230 Hz. The violin radiates little
  below its A0 air mode (~280 Hz) and is weak at the open G (KB §12.7).

**Calibration.** At `prepare`, the residues are fitted (10 + 10 iterations,
~4 ms) so the channels' mean power, smoothed, follows a target curve: 1/12
octave below 1 kHz, 1/3 octave above. A second fit makes a smoothed low band
for `Body Resonance` 0. The gain matches the Library Mic (Smooth) loudness
from 300 Hz to 5 kHz (×1.19).

**The target** (`data/violin/radiation_target.csv`, from
`tools/combine_target.py`):
- **Below 800 Hz:** the CC0 library-mic curve, 1/6-octave smoothed. My
  bridge force is within 1 dB of 1/n there (`tools/force_tilt.py`).
- **Above 800 Hz:** a direct measurement (`tools/radiation_target.py`). Each
  VSCO-2-CE solo-violin "Arco Vib" note (CC0, G3–C7, p and f) is compared
  with the same note rendered through this synth's Pickup output. Real minus
  synth per harmonic, aligned per note on 0.5–3 kHz, median per band,
  1/3-octave smoothed, with −12 dB/oct above 9 kHz.
- **Why not the library curve alone:** it assumes a 1/n bridge force (KB
  §12.7). Above 2 kHz my force falls below 1/n (−5 dB at 4 kHz, −13 at
  6.3 kHz, −22 at 10 kHz). With the library IR, the output came out ~8 dB
  too dark above 5.6 kHz compared with the real notes. Fully undoing the
  roll-off would overshoot, since real forces roll off too; the direct
  comparison measures where the truth lies.

**Result:** Modelled minus real, per half-octave band from 350 Hz to
9.5 kHz: −1.4, −2.9, −0.1, +1.1, +0.8 dB (re-measured after the per-string
HF loss in Phase 4). For the Library Mic (Smooth):
+1.6, −0.9, −0.3, +3.0, +8.1 dB.

**Controls** (on the 120 residues, gliding over ~5 ms):
- **`Bridge Hill`** (1.8–3.4 kHz, default 2.6): the radiation is re-shaped
  by the ratio of the hill shape (Q 2.5) at the new centre to the measured
  one, normalised so frequencies well below don't move. At the same time
  the bridge admittance's dense band follows the hill (`Bridge::setHill`),
  so the string's damping moves too. Measured on a scale: 2.0 kHz gives
  −5 to −6 dB above 2.5 kHz; 3.3 kHz gives +5 to +7 dB at 4–6 kHz.
  Below 1.6 kHz it's unchanged (±0.6 dB).
- **`Mute`** (KB §11.2: the mute's mass on the bridge): the hill moves to
  65% and the admittance's dense band drops to 60%. The radiation gets the
  same hill move plus a −5 dB shelf above ~1.5 kHz [T]. Measured: −12 to
  −13 dB above 2.5 kHz, −1.7 dB at 1.6 kHz, lows unchanged. It goes on
  over ~80 ms. With the mute on, notes still sustain: G6, A6 and C7 slip
  once per period (`LAB_MUTE=1 instrument_lab chord`).
- **`Body Resonance`** (0–1): blends the low band (below 1.2 kHz) between
  the measured peaks and dips and the smoothed fit. At 0, the 700 Hz dip
  fills by +8 dB and the 550 Hz region is trimmed: an even instrument.
- **`Stereo Width`:** mid/side on the per-channel gains (0 = mono).

**Library Mic / Library Mic (Smooth):** the CC0 library-mic IRs by
convolution (resampled from 48 kHz in `prepare`). They have the library's
mics and room baked in, and are ~8 dB darker than the real notes above
5.6 kHz, as above.

**Pickup:** the raw bridge force (an electric violin).

**Vibrato and life.** On a held vibrato B4, harmonics swing 2–7 dB over
each vibrato cycle in both outputs. Most of that is common to both channels:
it comes from the string itself (vibrato against the bridge and the
sympathetic strings). With the measured default vibrato (±8 cents, KB §11.2),
a partial moves less than the high-band modes' bandwidth, so the left/right
differences in the swing depend on where each harmonic falls: some
decorrelate (r ≈ 0.5), many don't. Wider vibrato decorrelates more.

## Tools

```
./build/string_lab tone <string 0-3> <note> <vb> <F> <beta> [classic] [out.wav]
./build/string_lab sweep <string 0-3> <note> <vb> [classic]          # Schelleng map
LAB_FS=192000 LAB_HEAT=2 ./build/string_lab ...                      # rate, heat calibration
./build/instrument_lab detache [legato]                               # gesture-layer trace
./build/instrument_lab hold <note> <seconds> [vel] [out.wav]         # bow changes, dips, attack time
./build/instrument_lab chord <n1> <n2> [n3 n4]                       # per bowed string: force, slip rate
./build/instrument_lab sympathy <note>                               # each string's level, bowing and after
./build/instrument_lab bridge                                        # |Y| of the bridge
python3 tools/calibrate_force.py                                     # Helmholtz window vs the assistant
./build/radiation_lab [sampleRate]                                   # radiation vs target, L/R difference per band
python3 tools/force_tilt.py                                          # bridge force vs 1/n, per 1/3 octave
python3 tools/radiation_target.py <VSCO Arco Vib dir> [render] [csv|-] [mode]   # real vs synth per band
python3 tools/combine_target.py <library csv> <measured csv> <out csv>          # the radiation target
./build/instrument_lab release <note> <1 lift|2 stop|3 finger off> [relVel]   # decay and the pitch left ringing
  env: LAB_HF=1 (measure above 1 kHz), LAB_DYN, LAB_PRESSURE, LAB_COUPLING, LAB_MUTE, LAB_STRING, LAB_CHANGE, LAB_FLOOR,
       LAB_WIDTH, LAB_SUSTAIN, LAB_BMIN, LAB_MINACC, LAB_TRACE=from,to
python3 tools/analyze.py render.wav [out.png]                        # spectrogram, level, pitch per note
```

## Known limits (next phases)

- No pizzicato, harmonics, spiccato, tremolo or martelé yet (Phases 4–5).
- The bridge has one direction (bowing). There's no vertical admittance or
  second polarisation yet.
- The highest notes have few samples per slip even at 96 kHz (KB §6.4); they
  slip longer and flatten more (A6: ~30 cents uncorrected).
- Cost: all four strings run while the bridge moves, plus 45 bridge modes
  at 96 kHz and 120 radiation modes at the host rate. The browser renders
  2.2× faster than realtime.
- Start-up: in the browser, `prepare` runs when audio starts. The radiation
  fit takes ~4 ms natively; notes in the first ~300 ms after audio starts
  may be missed (mostly the kit's reverb set-up). Calibration was 24 ms
  until it precomputed its matrices, and the kit's browser test then missed
  its first chord.
- Radiation has one fixed mic position. No placement presets (close,
  overhead, room) or near-field sources yet (KB §12.3b–c).
