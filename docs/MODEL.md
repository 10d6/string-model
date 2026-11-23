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
- **Open strings carry a bow allowance while bowed [T].** Bowing flattens a
  string, and no finger can correct an open one. Violinists tune while
  bowing, so the performer raises an open string the bow is on by G +15.5,
  D +11.5, A +14, E +16 cents (`openTuneCents`), easing in and out over
  ~30 ms as the bow lands and leaves. Harmonics, bowed much lighter, get
  0.55× of it. Unbowed open strings sit at true pitch, so they ring in
  sympathy with stopped notes. `tools/open_strings.py`: within ±1 cent at
  mf, ±3.4 from pp to ff.
  - Until the open-strings PR the allowance (+7/+4/+9/+12) applied to every
    open string. It was held to 60% so unbowed strings could still ring,
    which left bowed open strings 4–10 cents flat.
  - In tune, an open string's partials that coincide with another open
    string's (D's 3rd = A's 2nd) come out 2–4 dB lower: they interact
    through the bridge. Open notes are 0.7–1.9 dB quieter than before
    (none of it with body coupling off).
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

**Enhanced Thermal (default since Phase 6): the structure of Woodhouse &
Galluzzo 2025 (KB §3.2a).** Friction = Y(T) × h(u): a temperature-dependent
yield stress times a function of the slip speed u with h(0) = 1. The 2003
thermal model can only lower friction as the contact heats; h makes it drop
the moment the string slips, which is what measured Guettler transients show.
The paper's fitted constants weren't available here (the publisher's page
lacks the equation bodies), so both are tuned [T]:
- h(u) = 0.8 + 0.2 · 0.6 / (0.6 + u), a hyperbola like the classic curve's;
- Y(T) falls from 1.2 to 0.35/0.8 = 0.44 over 30 K (the paper's crossover is
  ~26 K above ambient; the 2003 model ran ~10 K hotter), so steady sliding
  matches the thermal model. The heat paths below are shared.
- With h the junction can have several slip solutions. It keeps the
  hysteresis rule (KB §3.4): it sticks until the stick force exceeds Y, then
  slips while a root of u + k·Y·h(u) = |a| exists.

h was chosen by attack quality: `string_lab guettler` starts the bow from
rest at constant acceleration (0.25–8 m/s²) and force (0.05–3.2 N) and counts
attacks that reach Helmholtz motion within 50 ms of the first slip. Over five
grids (open D at β 0.1 and 0.06, G3, A4 at β 0.08, E5), out of 210 attacks:

| Model | Perfect (Helmholtz from the first slip) | Within 50 ms |
|---|---|---|
| Thermal 2003 | 0 | 50 |
| Classic | 23 | 71 |
| Enhanced, h = 0.6 + 0.4·0.1/(0.1+u) | 22 | 65 |
| **Enhanced, h = 0.8 + 0.2·0.6/(0.6+u)** | **29** | **112** |

Through the synth (`instrument_lab hold`, G3–C6, three velocities) attacks
reach −6 dB of the steady level 5–10 ms sooner, and bow-change gaps are
shorter (e.g. D4 mf: 30 → 15 ms). The Schelleng map below keeps its wedge and
Demoucron's points with slightly less flattening; `calibrate_force.py`
straddles 0 on every row. The string comes out a little darker above 4.5 kHz
(2–4 dB), so the radiation target was re-measured (Output).

**Thermal: Woodhouse 2003 (KB §3.2a).** μ depends only on contact
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

**Bow width (KB §3.5).** The hair ribbon touches the string over a few mm,
so the bow is two contact points ~6 mm apart (2–5 samples at 96 kHz: the
wave speed on a string is 2 × scale × open pitch on any stop), each with its
own friction and contact temperature. The outer edge carries 80% of the force
(tilted hair) [T]: with 50/50 the low-force, far-from-the-bridge corner of
the Schelleng map (pp) turned to multiple slipping. The points can slip at
slightly different moments (differential slipping), which adds 3–5 dB of
noise between the harmonics at 5–10 kHz (A4 mf). Where the bow is within a
few samples of the bridge (very high notes), it's one point.

**Rosin noise (KB §6.4):** during slip, μ × (1 − A·u) with u uniform in [0, 1).

### Measured with `string_lab` (open D, v_b = 0.13 m/s, 96 kHz)

Enhanced Thermal with the bow width (`LAB_WIDTH=3 string_lab sweep 1 62 0.13 enhanced`):

```
beta \ F    0.02  0.04  0.08  0.15  0.30  0.60  1.20  2.40  4.80
   0.020     r-99   r-99   r-99   m -1   m -2   m -4   H -5   H-13   r +1
   0.030     r-99   r-99   m -1   m -1   m -3   H -4   H -9   r-27   r+99
   0.045     r-99   r-99   m -1   m -2   H -3   H -7   r-15   r+28   r+99
   0.065     r-99   m -0   m -1   H -3   H -5   H-11   r-31   r -4   r-99
   0.090     r-99   m -1   H -2   H -4   H -7   r-18   r-84   r-99   r-99
   0.130     r -0   m -1   H -2   H -5   H-10   r-38   r+61   r-99   r+99
   0.180     m -0   H -1   r -3   H -6   H-13   r-89   r+99   r -9   r-99
```

Thermal 2003, one contact point (Phase 1–5):

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
- Overlapping keys **slur**: the same bow, and the left hand changes the note
  (below). Keys further apart than `Slur Range` (1–24 semitones, default
  12) take a new bow instead, so only close notes slur. Letting go of the top
  key goes back to a key still held, by the same rule; in pizzicato it
  re-sounds nothing.
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

**Attack swell (`Attack Swell`, default 0.75; the perceptual-distance work).**
A bowed note grows into its level. Measured on the Philharmonia violin (arco,
0.25–1 s notes, onsets aligned): the first 60 ms sit 12–13 dB under the
note's peak at pp–mp, ~7 dB at mf, 3–5 dB at f–ff, and the level is within
3 dB after ~200 / ~100 / ~68 ms (median), whatever the note's length. The
synth was at full level in 32–42 ms at every dynamic. Now each stroke from
rest starts D0·(1 − t/T)² dB under its level, D0 = 15 dB at d ≤ 0.45 falling
45 dB per unit of d to 4 dB, T = 0.36 s falling to 0.26 s, times the control
[T: fitted]. 40% of the deficit is a slower bow (the bow accelerates through
each stroke, KB §11.5) and 60% a contact point further from the bridge, on the
level law (L = 7.7 ln(vb/β)); the force follows the players' line for both, and
the soft-playing force lift (the margin) uses the momentary dynamic. A new bow
mid-phrase (détaché) gets 40% of the depth over half the time, and none in
passages faster than ~0.12 s between keys; slurs don't start strokes. Only
arco. Measured at 1 (0.75): soft notes within 3 dB after 176 (160) ms, first
60 ms −10.2 (−8.5) dB; mf 121 (103) ms, −6.7 (−5.2); loud 58 (50) ms, −3.0
(−2.4). The strings stay in Helmholtz motion: at pp the G string's first
100 ms multi-slipped on main (2.3–2.5 slips per period) and does less now
(1.2–1.4 at 1); after 100 ms every window of 12 notes G3–E6 is within
0.9–1.1 slips per period at 0.75 (at 1, G3 and A#3 stay at ~1.3 until
~200 ms). Perceptual distance: 53.4 → 41.6 (attack 72.4 → 47.3), 390 of 448
notes nearer.

**Bow changes.** The bow position along the hair is tracked (62 cm). A long
note changes bow at the tip or the frog when `Auto Bow Change` is on; off
(the default since the slurs PR, the owner's choice) gives an endless bow.
The labs keep it on (`LAB_AUTO=0` turns it off in `instrument_lab`). A key-driven bow change (détaché) reverses in `Bow Change`
(default 20 ms) at constant acceleration. An automatic change (the bow ran
out) takes the same time but follows a half cosine through zero, like a
tremolo reversal, so less of it is spent near zero speed. Each automatic
change varies (KB §11.2): its time ±20%, the new stroke's force ±6%, and
where the bow turns next, 3–8% from the end. Force dips towards 20% at zero speed (0.2 + 0.8 (v/v_t)²):
players lighten the bow here, and the KB reports that a deeper dip shortens
the gap.

Measured above 1 kHz (where the body boosts) at each change, over 35 held
notes (D4–A5, five velocities): mean dip 12.5 dB, more than 6 dB down for
15 ms on average (30 ms at most), with changes about every 4–5 s at mf.

Rounded automatic changes (instrument_lab hold, `LAB_HF=1`, G3–E6 at three
velocities): the gap more than 6 dB down fell from a median of ~25 ms
(constant acceleration) to ~15 ms, with a shallower dip. Keeping the force
on through the turn, as tremolo does, made no measurable difference (floors
0.2–1.0 all gave the same gap at 20 ms), and slower round turns (40–100 ms)
lengthened the gap to 25–60 ms.

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

**The left hand in a slur** (KB §11.2 "Left-hand articulation", "Shifts";
player doc §3.3). Until this PR every slurred change slid the pitch over
~10–20 ms, even a half step or a slur to and from an open string: a small
portamento and a whoosh of winding noise on every note. A player slides only
when the hand changes position. So the performer tracks the hand's place
(the first finger's, in semitones above the open string; 2 is first
position; the same on every string) [T]:
- **Within reach** (from one below the hand's place, a low first finger, to
  five above, an extended fourth), or to or from the open string: a **finger
  change**. The pitch switches in 2 ms (a finger landing, hammer-on) or 3 ms
  (lifting) with a fast S-curve (KB: 1–3 ms), with no slide, no lighter
  finger and no winding noise. The landing finger taps the string and a
  lifting one plucks it sideways: a 1.5 ms raised-cosine velocity pulse at
  the finger end, 0.05 m/s (landing) or 0.09 m/s (lifting) × (0.5 +
  velocity), random sign [T level]. The landing tap is the smaller because a
  hammer-on mostly moves the other polarisation. Measured: the onset of a
  change rises 1–2 dB above `main`'s (0–6 dB above the steady note, from the
  string re-settling); no clicks.
- **Out of reach: a shift** (Flesch's L-type). Going up, the hand moves so
  the note lands on the third finger; going down, on the second. The old
  finger slides with the hand, then the new finger lands (or, going down,
  the old one lifts off onto it). If the slide reaches the note, it is a
  same-finger slide. A note after an open string or a new bow moves the hand
  silently.
- **Measured** (`tests/slurs.json`-like phrases, 12 ms pitch windows): the
  time from leaving the old pitch by 40 cents to within 40 cents of the new
  went from a median of 10–11 ms to 4 ms (the window's limit) for finger
  changes. A slurred A4→E5 (open string to fourth finger, slow keys) was a
  59 ms slide up from the open string; it is now 3 ms. On a forced A string
  a two-octave scale shifts twice per octave each way.

**Shifts** (KB §11.2 "Shifts, portamento"). The slide takes 8 ms + `Shift
Time` × min(1, slide/5 st) × (1.6 − 1.2·velocity) [T]: a gentle key slides
slowly (×1.4 at 0.2), a hard one quickly (×0.4 at 1). It follows a
minimum-jerk curve, 10t³ − 15t⁴ + 6t⁵, the usual model of a human
point-to-point reach (Flash & Hogan 1985), which starts and lands more gently
than the cubic S-curve it replaces [T]. A 5-semitone shift (B4→G5 on the A
string, velocity 64) measures 40 ms, the fast end of the KB's 50–150 ms. The
finger's speed along the string,
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
Defaults 22 cents after 0.1 s (until the shimmer PR, 16 cents after 0.25 s,
the VSCO player): both recorded players vibrate from the onset, VSCO's at
12–21 cents and the Philharmonia player's at ~31 (5–95% of the pitch, held
notes), so the default sits between them, moderate like the other habits.

**Bow wander (`Bow Wander`, default 0.6; the shimmer PR).** A real bow is not
steady: the stick and hair are springy and the string kicks them, the hand
has its own tremor (KB §3: force fluctuations reach ~10%), and the player
corrects what they hear and feel after a reaction time. The performer keeps
three deviations, in log units, of bow speed, contact (β) and force, each
x' = −1.5 x − 6 x(t − 0.12 s) + noise, stepped every 4 ms [T]. It wanders
with a spectrum peak at 1.7 Hz and ~0.2 s correlation, with a little
overshoot, not jitter. Spread at Wander 1: speed 0.20, contact 0.22, force
0.08 (fitted to the Philharmonia recordings' common level wobble, 2.26 dB:
the synth gives 2.28 at 1, 1.60 at 0.6). Speed and contact move the force
along the players' line, so the note stays in the Helmholtz window; the force
also wobbles on its own. Arco and harmonics only. It fades out from 10 to 20
semitones above the open string: up there the window is narrow and players
hold the bow steady near the bridge (B6 broke into octave jumps at full
wander). Held notes G3–E6 at three dynamics: 1 of 900 100-ms windows outside
0.9–1.1 slips per period at 0.6, 6 at 1 (0 without). It moves the open
strings' pitch by up to ~1 cent (the flattening depends on force).

**Intonation (the player's ear).** Bowing flattens the pitch, by about 10 cents
at mf here. On stopped notes, the time between slip onsets is compared with
the wanted period, and the finger moves to correct it. The correction is
`Intonation` × 1.6% of the error per period, within ±60 cents, and only while
the motion is steadily periodic (±4%). Open strings can't be corrected by
a finger: they carry a fixed bow allowance instead (above).

### Player habits (instrument-research `player_technique_and_responsiveness.md`)

What players measurably do, so a keyboard sounds like a musician without
extra controls. Each one has an amount, on at moderate settings by default.

**`Intonation Style`** (0 equal temperament … 1 expressive, default 0.7;
player doc §3.2). It sets the target that the intonation loop above aims for:
- **Open strings in pure fifths** around A: G −3.9, D −2.0, E +2.0 cents.
- **Stopped notes by fifths from the open strings** (Pythagorean). A fifth is
  1.955 cents wide, so each note's place on the chain of fifths sets its
  tuning. This raises leading tones in the usual keys: C# +7.8 in D, G# +9.8
  in A. Notes matching an open string match it exactly, so it rings in
  sympathy (player doc: the player's tuning cue).
- **Which spelling.** A note's place on the chain depends on the key. The key
  comes from a decaying count of recent pitch classes (~7 notes) against the
  Krumhansl–Kessler major profile. Each note is spelled within 5 fifths flat
  to 6 sharp of the key's centre, so minor-key leading tones are sharps.
- **Double stops and chords pure** against one reference note: an open
  string if the chord has one, else the lowest note at its melodic tuning.
  Major thirds come out 13.7 cents narrow, minor sixths 13.7 wide, fifths
  2 wide.
- **Measured** with Intonation Style 1, vibrato off, against Intonation
  Style 0:
  - D-major scale: E +3, F# +6, G −4, B +4, C# +8 cents.
  - D–F# third: −13; E–C and F#–D sixths: +14.
  - Open strings: at Intonation Style 1, the open D lands at −2 cents (its
    pure-fifth target) since the open-string allowance fix (before: −10).
- **Measured behaviour, not lore:** the player doc's rule that a crescendo
  shouldn't speed up the bow is teaching advice. Demoucron measured bow speed
  rising 10 → 50 cm/s across crescendos (KB §11.5), as the assistant already
  does, so it was not adopted.

**`Vibrato Shaping`** (0 the same on every note … 1 by context, default 0.7;
player doc §3.1, KB ref 65 / P15):
- **Register:** wider higher up, ×(1 + 0.25 per octave above A4), from 0.8×
  to 1.35×.
- **Fast passages:** narrower. If the key came within ~0.12 s of the last
  one, the vibrato is ~30%. It opens up over 0.5 s if the note is held, so a
  long note ending a run gets its full vibrato.
- **Long strokes:** it keeps widening, by 25% over the first 1.5 s.
- **Measured** (5–95% pitch range, Shaping 0 vs 0.7):
  - B4 at 0.5–1 s: 15 vs 13 cents; later: 15 vs 15.
  - B6: 15 vs 18.
  - B4 straight after a run, at 0.2–0.5 s: 7 vs 5; at 0.7–1.3 s: 17 vs 15.
- **Rate:** faster on metrically weak notes, ±4% (below).

**Double stops** put more weight on the lower string: ×1.12 lower, ×0.9
upper (player doc §2.6) [T]. In `instrument_lab chord`, both strings of
D–A, G–D, A–E and E–C slip once per period.

### Meter and tempo

Each note's position comes from the kit (beats are quarter notes; the
transport gives the bar start and time signature). `model/Meter.h` turns it
into a metric strength [T]:

| Position | Strength |
|---|---|
| downbeat | 1 |
| mid-bar strong beat (4/4 beat 3, 12/8) | 0.7 |
| beat (dotted quarter in 6/8, 9/8, 12/8) | 0.5 |
| subdivision (half a beat; a third in compound meters) | 0.25 |
| finer | 0.1 |

Played timing isn't quantised: a note within a tenth of a beat (at most a
third of the grid step), early or late, counts. The meter applies only while a
transport plays (a DAW, or the page's Play), since the kit's clock always
runs. Otherwise these rules are off, and tempo-dependent ones use the time
between keys.

**`Metric Emphasis`** (default 0.5; player doc §4, KTH metric rules) [T]:
- **Velocity:** ×(1 + 0.5 · amount · (strength − 0.4)), so +15% on the
  downbeat and −4% on subdivisions at the default. On repeated eighths in
  4/4: downbeat +1.3 dB, beat 3 +0.6, subdivisions −0.5 dB.
- **Bowing to the meter** (player doc §2), for strokes from the air:
  - a down-bow (frog to tip) on a strong beat (≥ 0.7), or on any beat that
    starts a phrase after a rest of 0.25 s or more;
  - an up-bow on a pickup that starts a phrase (off the beat, after a rest);
  - only if 40% of the bow is left that way. With less, a long note runs
    out of bow and changes mid-note, which sounds like an extra note. The
    first version allowed 20% and re-took pickups to the tip, so each
    downbeat after a pickup rebowed;
  - with 80 ms or more in the air, the bow is re-taken first: to the frog
    for a down-bow, to the middle for a pickup up-bow, so the downbeat after
    it has most of the bow.

  Otherwise notes alternate, unless the bow is nearly at one end. Checked
  with a C++ harness on the performer, replaying 3/4 phrases with pickups:
  - pickups are up-bow from mid-bow;
  - every downbeat is down-bow, with ~60% of the bow;
  - mid-phrase notes alternate;
  - no bow changes mid-note, except in a 3.4 s slurred passage, which
    rebows with Metric Emphasis at 0 too.
  - limit: separate quarters through a 3/4 bar can land the next downbeat on
    an up-bow (the bow is just past the 40% rule). Players hook two up-bows
    or re-take there (`bowing_lab`; docs/PLAN.md "Ideas").
- **Leading tones** rise up to +3 cents more on weak notes (player doc §3.2),
  scaled by `Intonation Style`.
- **Vibrato rate** goes ×(1 + 0.12 · Shaping · (0.5 − strength)): faster on
  weak notes (Timmers & Desain, KB ref 65).

**Bow weight** (player doc §1.4): the hand balances the bow's weight
imperfectly. Force is +6% at the frog and −6% at the tip, plus ±3% per
stroke, so down-bows from the frog start a little heavier [T].

**Spiccato length** follows the note rate (the time between keys): contact
and stroke ×0.7 at 9 notes/s, up to ×1.5 at 3/s [T].

**`Portamento`** (default 0.3; player doc §3.3) [T]:
- Applies to slurred shifts of a third or more, after a slow key gap
  (0.3–0.8 s); notes within the hand's reach never slide.
- The slide is up to ×7 longer at 1: none in fast passages.
- A slurred B4→G5 on the A string (a shift), keys 1 s apart: 40 ms at 0,
  106 ms at 0.3, 264 ms at 1. Keys 0.25 s apart: 39 ms at any setting.

## Articulations (KB §11)

`Articulation` (Arco / Martelé / Spiccato / Tremolo / Pizzicato / Harmonics),
also on keyswitches F1–A#1. Martelé, spiccato and tremolo replace the
assistant's bow speed and force with a stroke template from Demoucron's
measured strokes (KB §11.5); the per-string force factors (impedance,
position, body load, trim) still apply on top. Templates set the bow speed
directly (it is already smooth) and turn off the automatic bow change.

- **Martelé.** The bow pinches, still, for 0–60 ms (more when loud) at
  0.8–2.7 N; then a stroke of 0.25–0.33 s whose speed rises to 0.5–1.2 m/s
  (42% of the stroke) and falls to rest, while the force drops to 0.25–0.45 N
  over 0.2 s with the bow's ~13 Hz ripple. At rest the bow damps the string.
  Separate strokes, never slurred. The pinch delays the first sound: ~60 ms
  at soft velocities.
- **Spiccato.** A ~75 ms force bell (rise 45%), a half-sine stroke of 0.14 s
  with peak speed 0.08–0.78 m/s, then the bow is in the air. The bow sits
  further from the bridge (β 0.18 → 0.08 from p to f). Demoucron's peak
  forces (0.5–3 N) are scaled ×0.4 onto this string: at ×1 the notes went
  20–40 cents flat (crushed). At ×0.4 they are within ~15 cents, most within 10.
- **Tremolo.** Near-sinusoidal bow speed at `Tremolo Rate` reversals per
  second (VSCO median 12.2/s, KB §11.2), ±12% rate and ±15% force jitter per
  stroke; down-bows lighter than up-bows (×0.8 / ×1.2).
- **Sautillé** (B1). The bow bounces by itself (Guettler & Askenfelt 1997,
  KB §11.5): its speed is a sine at half the note rate, one note per half
  cycle starting at the reversal, and the force a cosine at the note rate,
  clipped at 0 (briefly in the air), peaking 53° of its cycle after the
  reversal, the phase their simulations found "perfect". The cycle is the
  time between key presses (60–200 ms). At 9 notes/s the notes are within
  ~10 cents and dip 8–12 dB between notes.
- **Pizzicato.** The bow lifts and a force pulse acts at the pluck point
  (β 0.2 ± 0.05, KB §11.2): it rises over 12–24 ms to 0.6–3.6 N (velocity),
  then lets go. The release ramp is 1–2.5 ms but never longer than ~0.35 of
  the period: a ramp as long as the period cancels the fundamental, which
  made open-E plucks 10–20 dB weak and uneven. Force, rise, release and
  point vary per pluck (KB §11.2: identical plucks sound mechanical). Keys
  pressed together strum, 12 ms apart. Open strings sound at pitch (no
  bow-flattening allowance). A quick key-up damps the string with the
  hand; `Finger Off` lifts the finger. Plucked notes peak about as loud as
  an mf bowed note.
  - **Second polarisation (Phase 6).** The pluck's angle (14–46° from the
    bowing plane, random) splits it between the horizontal string and a
    second, vertical one: an unbowed copy of the string on a rigid bridge,
    with 2.5× the decay time, 1.5 cents sharp (slow beating), whose bridge
    force goes straight to the output at the same gain. The horizontal
    polarisation gives its energy to the body quickly; the vertical one rings
    on: the two-stage decay (KB §4, §11.2). Fitted to VSCO-2-CE solo-violin
    pizzicato (forte, A3–A6), comparing the early decay rate, the late one and
    the level 1 s after the pluck. The recordings' tails (~25–35 dB/s on
    almost every note) include the room, so the synth was measured with its
    default room. Mean error, early and late rates in units of 30 dB/s plus
    level in 10 dB: 1.71 with one polarisation, 1.02 now. Open strings
    plucked hard ring for more than 3 s. A damping hand stops both.
- **Harmonics.** A light finger touches the string at 1/k of its vibrating
  length from the nut-side end. In the waveguide the bow–nut segment is split
  at that point by a resistive junction, R = 4·Z0, gliding in over ~10 ms:
  v_t = 2Z(a1 + a2)/(2Z + R), with a1, a2 the incoming waves. It is
  transparent at R = 0. The touch point allows for the nut filter's delay.
  Each note uses a natural harmonic if one exists (an open string's 2nd,
  3rd or 4th partial: touched at 1/2, 1/3, 1/4), else an artificial one: a
  stopped note two octaves, a twelfth or an octave below, touched at 1/4,
  1/3 or 1/2 of the stopped length, lowest position first.
  - **Bowing a harmonic.** The string vibrates in k segments, like a string
    1/k as long, so β is divided by k, and the force is ×0.3 (octaves and
    twelfths) or ×0.15 (two octaves). With the arco force the motion falls
    back toward the fundamental (D6 on the D string came out a series at
    2.6 × f0); at force ×0.45–0.6 the harmonics were still 10–30 cents flat.
  - **Intonation** aims for k slips per fundamental period.
  - Measured through the synth (open and stopped, A5–B6): within ±5 cents,
    the fundamental 33–65 dB down; B6 still ~15 cents flat.

## Output (KB §12)

**Modelled (default): `model/Radiation.h`.** A bank of body modes driven by
the total bridge force (KB §12.3a):
- 16 per octave from 180 Hz to 1 kHz (Q 22), and 48 per octave from 1 to
  10 kHz (Q ~80, falling slowly), with a little random jitter: 199 modes.
  Until the shimmer PR, 24 per octave at Q ~40 (120 modes). Above 1–2 kHz the
  body's modes overlap and its response is effectively random (KB §5); how
  fast it changes with frequency is set by the modes' bandwidth. At Q 40
  (2.5% wide) a vibrato partial barely changed level, so the synth's
  band-against-band shimmer was a third of the recordings' (below). Q 80 is
  a loss factor of ~1.25%, mid-range for violin plates; the density keeps the
  modes overlapping (~0.9) rather than ringing as separate peaks. Fit sd
  0.57 dB (was 1.09); browser speed unchanged within noise.
- Each is a peak-normalised band-pass. The microphones share the modes and
  mix them with their own residues: the residue toward the mic's direction
  (KB §12.3a: shared poles, direction-specific residues).
- **Low band (below 600 Hz):** a monopole, the same residue in every
  direction (KB §5.5, §12.1).
- **High band (above 1.2 kHz, blended over 600–1200 Hz; Phase 7a):** each
  mode has a random radiation pattern: real orthonormal spherical harmonics
  up to order ⌈ka⌉ + 1 (a = 0.2 m: order 6 at 1.2 kHz, 17 at 4.3 kHz, 39 at
  10 kHz) with random Gaussian coefficients. It is smooth at 1 kHz and
  changes every few degrees at 8 kHz. On top, the measured overall shape
  (KB §12.1, Pätynen & Lokki) [T]: above 1 kHz (full from 4 kHz) the treble
  beams toward the front, 35° up, at 4.5 dB × (cos angle − 1), and at 2–6.3
  kHz it is up to 3 dB weaker toward the floor. Each pattern has unit mean
  power over the sphere, so the fitted levels are the same in every
  direction on average.
- **The mics (Phase 7a):** a spaced pair of omnis 1.5 m from the violin at
  `Mic Angle` (azimuth in the player's frame: 0 in front, +90° on the
  player's left) and `Mic Elevation` (0 level, 90° overhead), `Stereo Width`
  × 60 cm apart across the mic's view. Each hears the body from a slightly
  different direction, so overlapping modes interfere differently in each
  ear: the response changes from semitone to semitone and differs left and
  right ("directional tone colour"). At the default place (0°, 30°), 1–8 kHz:
  left and right differ by 6.0 dB RMS on average (the earlier random
  per-channel residues: 7.5) and each has 4.6 dB of fine structure per 1/3
  octave (4.6). Below 600 Hz they differ by 1–2 dB at 250–500 Hz (the high
  modes' skirts; the KB has the 500 Hz octave lateral, not omni). Moving the
  mic re-evaluates the patterns (~80k multiply-adds) at most every 16
  control ticks (~5 ms), about 8 µs per tick natively while a knob moves;
  the residues glide. The mic is always far (1.5 m): distance, mic types and
  the near field come in 7b–7d.
- **Directivity** (`radiation_lab directivity`: one omni at 200 directions,
  re the sphere mean): 250 and 500 Hz omni within ±1 dB; 2 kHz front and up
  strongest (back −4.6, below −6.4 dB); 4 kHz front +2, back −3.2, below −3.5;
  8 kHz front-up +6.7, back −7.2 (KB: "strongly forward, only the front
  within −3 dB"; bottom elevations weaker). The directivity index varies by
  4.9 dB from semitone to semitone over 1–4 kHz (KB: 2.2–8.1 dB for the
  violin's class). 1 kHz is random (the KB's diagonal lobes aren't shaped).
- **Positions** (a phrase, band levels re 200–1000 Hz, change from the
  pre-7a output): default within ±1 dB to 8 kHz; overhead −1 to −2 dB at
  2–8 kHz; the player's left +4 at 1–2 kHz, −2 to −3 above; behind −10 dB at
  2–4 kHz, −6 at 4–8; below −3 dB at 2–8 kHz.
- **The player (Phase 7b, KB §12.3d) [T]:** the head and chin (above the
  violin, toward the player's right) and the torso (behind and below) shade
  the treble in their directions: up to 6 and 8 dB above ~1.5 kHz (fading in
  from 700 Hz), over cones of ~40° and ~50°. Toward the head −4.7 to −6 dB at
  2–8 kHz re the sphere mean, toward the torso −12 to −15 (with the
  directivity there); the default place is unchanged (±0.3 dB per band).
- **Distance (Phase 7b, `Mic Distance`, 0.4–10 m; closer behaves as 0.4 m
  until the near field, 7c):** the pair's directions spread or narrow
  (60 cm apart: ±31° at 0.5 m, ±2° at 8 m), and the direct sound falls as
  1/r from the reference 1.5 m (each mic's own distance). The room doesn't,
  so its share grows 6 dB per doubling. The synth then re-gains the total as
  an engineer would: power × (1 + W m²) / (d² + W m²), d the direct gain re
  1.5 m, m the Room mix, W the space's measured wet/direct power at 100%
  (`tools/room_power.py`: Room 0.315, Chamber 0.139, Hall 0.179, Cathedral
  0.195, Plate 0.204). Loudness stays within 0.3 dB from 1.5 to 10 m (Room 20
  or 60%); at 0.5 m +2.4 dB, as the pair sees the violin from ±31°. In the
  Hall at Room 20% the room is ~30 dB under the direct sound at 0.5 m, 21.5
  at 1.5 m, 13 at 4 m, 7 at 8 m; Room 100% at 1.5 m (−7.5 dB) is what a real
  hall of that size gives (critical distance ~3.6 m, KB §12.3e: −7.6 dB).
  No time of flight (8 m would add 23 ms of latency) and no air absorption
  (under 1 dB at 8 kHz below 10 m). The floor reflection is left for the
  cello (Phase 8): for a standing violinist it is minor, and the VSCO
  calibration already contains the recordings' own floor.
- **The room's feed (Phase 7b, KB §12.3e):** the reverb hears the violin's
  power response (the sphere average), not the mics' view: two more channels
  from the same modes, each mode at its mean level with a random sign
  (decorrelated left and right). Its treble is lower by the directivity
  index: the reverb at Room 100% is 3.6 dB darker at 1–2 kHz, 7.3 at 2–4,
  7.0 at 4–8, 8 at 8–16 kHz than when it was fed the mics' signal. The
  other outputs still feed the room what they play. Cost: the radiation now
  mixes four channels; the browser page went from 9.3× to 8.8× real time.
- **Low cut:** a 6th-order Chebyshev high-pass (0.5 dB ripple) at 245 Hz.
  The violin radiates little below its A0 air mode (~280 Hz) and is weak at
  the open G (KB §12.7): below A0 the air in the f-holes and the plates move
  in antiphase and cancel, like a vented speaker box, so the fall is steep.
  Until the low-notes PR it was a 4th-order Butterworth at 230 Hz. The
  modes' band-pass skirts fall only 6 dB/oct below them, so the modes just
  above A0 leaked into the open-G region (`radiation_lab`: 198 Hz band 10 dB
  over the target re 250 Hz). Against the VSCO notes (aligned on harmonics
  3–8, mean of p and f) the G3 fundamental was +17 dB and A3's +8 dB, which
  with the Helmholtz sawtooth gave an "electronic" low end; now +2.7 and
  +1.7 dB. C4 and E4 are unchanged (C4's fundamental is ~10 dB weaker than
  the recording's: its A0 region differs from the library curve). The
  fundamental re the strongest harmonic at mf: C3 −29 → −59 dB, E3 −17 →
  −40, G3 −12 → −26, A3 0 → −4. Loudness is unchanged (±0.5 dB, C3–E4).

**Force-dependent brightness (the low-notes PR).** A real string brightens
with bow force: more force sharpens the Helmholtz corner (KB §2 "corner
rounding"; §11.5: log centroid rises linearly with log force). The string
here barely does: at fixed v_b and β, 0.4 → 1.6 N on G3 (all Helmholtz)
raises 4–8 and 8–16 kHz by under 3 dB and doesn't move the centroid
(`string_lab tone`), because the lumped loss filters set the corner. The
VSCO notes brighten by +3..+15 dB above 4 kHz from p to f; the synth's p was
up to 12 dB too fizzy at 8–16 kHz on low notes (what read as "bow noise",
though its inter-harmonic noise is below the recordings') and its f a little
dull. So the output (after the bridge, outside the loop: bowing is
untouched) is tilted by 8 dB per octave of bow force re mf [T], half on a
first-order shelf at 2.5 kHz and half at 7 kHz, clamped to ±9 dB. The force
is the players' line from `computeTargets`: f ∝ e^(3.62·dynamic) ×
2^(1.5·pressure) × 2^(2.09·contact), so aftertouch and bowing nearer the
bridge brighten too, as the player doc says they should (§1). It is zero at
dynamics 0.557 (mf, the middle of the p/f pair the radiation was fitted
on), so the radiation target still holds (`radiation_target.py` unchanged
within ~1 dB per band), and neutral when not bowing. Measured over G3–E5:
the p→f spread above 4 kHz is within 2 dB of the recordings (median; it
was 7–8 dB short), and p and f each within 2.6 dB of them per band. G3 and
C4 at p remain ~8 dB bright at 8–16 kHz (at about −47 dB re the total).

**Calibration.** At `prepare`, at the default mic place, the residues are
fitted (30 + 10 iterations, ~5 ms; a mode near a null of its pattern toward
the calibration place moves its band slowly, so the detail pass needs 30) so the channels' mean power, smoothed, follows a target curve: 1/12
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

**Re-measured in Phase 6** for the enhanced friction (its string is 2–4 dB
darker above 4.5 kHz): the same pipeline, after which the per-band residual
(`radiation_target.py … - 3`) matches Phase 5's within ~1 dB.

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
- **Body character** (KB §5.8). Research on Stradivari and other fine
  violins found no single "Strad sound". It did map which response regions
  listeners and players care about, so these are offsets (dB) from the
  measured violin, applied to the radiation modes by frequency. They are
  cheap, glide, and the default is the measured response, bit for bit.
  - `Air Resonance`: A0 (~280 Hz), a bump of σ 0.2 octave. A0 output is the
    one measured correlate of rated quality (KB ref 109).
  - `Nasality`: 0.8–1.5 kHz (centre 1.1 kHz, σ 0.4 octave). Factory
    violins concentrate their energy here; the most pleasant violin in a
    blind Cremona test was weak here (KB ref 114).
  - `Brilliance`: the bridge hill's level, at Bridge Hill's frequency.
  - `Softness`: a cut above ~4 kHz (4th-order shelf). Negative adds air.
  - `Voice`: moves the modes below 3 kHz (fading out by 4.5 kHz) by ±10%,
    shifting F1–F3 together. Tai et al. measured Stradivari formants ~8/6/4%
    above other old Italians (KB ref 112).

  `radiation_lab character` measures them per third-octave band:
  - Air Resonance +9 gives +6 dB at 250 Hz.
  - Nasality +9 gives +7 to +8 dB from 0.8 to 1.3 kHz.
  - Brilliance +9 gives +6 to +9 dB from 2 to 3.2 kHz.
  - Softness 12 gives −6 dB at 4 kHz and about −10 dB above 6 kHz.
  - Voice ±10% moves the bands around 400–800 Hz by 5–7 dB.
  - Boosting a mid region also lifts the band below 200 Hz by up to ~2 dB,
    through the modes' low tails, where the 230 Hz high-pass already cuts.

  Four presets are labelled approximations, not claims about any
  instrument (KB §5.8: ≥3–6 dB or ~6% steps to be audible). On a test
  phrase, against the default:

  | Preset | 200–350 Hz | 350–700 Hz | 0.8–1.5 kHz | 1.5–2.2 kHz | 2.2–3.5 kHz | 3.5–5 kHz | 5–10 kHz |
  |---|---|---|---|---|---|---|---|
  | Cremonese, Dark & Vocal | +3.7 | −1.9 | −2.2 | +1.3 | −1.9 | −3.1 | −2.6 |
  | Cremonese, Bright & Vocal | +2.4 | +0.6 | −2.7 | +3.6 | +2.8 | +0.3 | +0.4 |
  | Modern Soloist | +1.2 | −0.1 | −0.8 | +2.7 | +4.6 | +2.4 | +2.0 |
  | Student Factory Violin | +0.7 | +0.3 | +4.7 | −1.2 | −4.8 | −3.9 | −4.6 |

  The controls act on the Modelled output only: the Library Mic outputs are
  recordings' responses, and Pickup has no body. They change what the body
  radiates, not how it loads the strings; a body whose stronger, less
  damped low modes also change playability (KB §5.8's "character vs ease")
  is left for later.

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
./build/string_lab guettler <string> <note> <beta> [classic|enhanced]  # attacks from rest (Guettler plane)
  env: LAB_WIDTH=<samples> LAB_SHARE, LAB_HFLOOR LAB_HSPEED LAB_TDROPE (enhanced friction)
./build/radiation_lab character [a0 nasal brilliance softness voice%] # body controls per band
./build/radiation_lab directivity                                  # one omni around the violin: per octave, per semitone
python3 tools/room_power.py                                         # each Space's wet/direct power (the distance trim)
./build/bowing_lab <metric> [bpm num den] < events                   # bow direction per note, mid-note rebows
  instrument_lab env also: LAB_FRICTION=0|1|2, LAB_BOWWIDTH=<m>, LAB_AVARY
python3 tools/open_strings.py | harmonics_tuning.py | intonation_check.py | vibrato_extent.py
python3 tools/pizz_decay.py <VSCO Pizz dir>                          # two-stage decay vs recordings
python3 tools/note_pitch.py <scenario> [wav]                         # pitch per note, peak below
LAB_HF=1 tools/bow_change_gap.sh [note]                              # automatic bow change gap
```

What each check should show, and the latest numbers: [EVALUATION.md](EVALUATION.md).

## Known limits (next phases)

- No snap (Bartók) pizz, ricochet, col legno or left-hand pizz yet. The
  vertical polarisation doesn't touch the bridge solve (no sympathy through
  it) and isn't bowed.
- The enhanced friction's constants are tuned here, not the paper's.
- Spiccato is a fixed thrown stroke; sautillé bounces at the key tempo but
  there's no bow-bounce degree of freedom (ricochet, KB §11.1).
- The bridge has one direction (bowing). Pizzicato's vertical polarisation
  is a separate string on a rigid bridge, straight to the output.
- Body controls change the radiated sound only, not how the body loads the
  strings (KB §5.8 "character vs ease": docs/PLAN.md "Ideas").
- Bowing to the meter can land a downbeat on an up-bow after separate notes
  in odd meters (above). Rules that need the next note (crossing lead,
  phrase timing) aren't possible from a live keyboard without latency.
- Browser and native renders diverge on chaotic passages (last-digit maths
  differences grow); CI compares a well-behaved one (docs/EVALUATION.md).
- The highest notes have few samples per slip even at 96 kHz (KB §6.4); they
  slip longer and flatten more (A6: ~30 cents uncorrected).
- Cost: all four strings run while the bridge moves (two bow points on a
  bowed string; plucked strings add a vertical copy), plus 45 bridge modes
  at 96 kHz and 120 radiation modes at the host rate. The browser renders
  ~7× faster than realtime with the room (~12× without), measured with the
  kit's `bench-web.cjs` on a Release build. Earlier figures here came from an
  unoptimised local build. The radiation's modes are plain arrays and the
  page uses WebAssembly SIMD; the kit's reverb is ~40% of the browser's cost.
- Start-up: in the browser, `prepare` runs when audio starts. The radiation
  fit takes ~4 ms natively; notes in the first ~300 ms after audio starts
  may be missed (mostly the kit's reverb set-up). Calibration was 24 ms
  until it precomputed its matrices, and the kit's browser test then missed
  its first chord.
- Radiation has one fixed mic position. No placement presets (close,
  overhead, room) or near-field sources yet (KB §12.3b–c).
