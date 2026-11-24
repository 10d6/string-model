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

## Instruments (Phase 8)

Each instrument is an `InstrumentDef` (`model/InstrumentDef.h`; the data in
`model/defs/Violin.h`, `model/defs/Cello.h`, listed in order of the
Instrument switch in `model/defs/Instruments.h`): its strings, losses,
bridge modes and dense band, bow and force law, output levels, radiation
target, and two smaller parts:
- `RadiationShape` (`model/Radiation.h`): the mode bank's lowest mode, the
  monopole band, the body's effective radius, the treble beam's elevation,
  and where Air Resonance and Nasality act (violin: 180 Hz, 600 Hz, 0.2 m,
  +35°, 280 Hz, 1.1 kHz; cello: 60 Hz, 300 Hz, 0.4 m, −11°, 102 Hz, 660 Hz;
  KB §5.4, §12.1, §12.3a).
- `HandDef` (`model/InstrumentDef.h`, used by `model/Performer.h`): the left hand's reach around its place,
  where a shift lands, the widest chord, vibrato's register centre (violin:
  −1/+5 semitones, third/second finger, a fifth, A4; cello in the neck
  positions: −1/+4, a minor third with an extension, a fourth-ish chord span
  of 5 semitones, A3) [T].

The synth prepares every instrument in `prepare` (strings, bridge,
radiation, library-mic IRs) and plays one. Switching stops the notes: the old
instrument's bridge force fades over 30 ms and its body (radiation or
library-mic IR) rings out over 0.25 s with no input, then it is reset, so a
switch needs no allocation. The labs test the violin unless
`SM_INSTRUMENT=cello` is set (`tools/LabInstrument.h`).

**The cello** (8.1 the KB alone; 8.2 strings, losses, playability; 8.3 the
measured body, per-string mode strengths, the wolf's acceptance tests):
- Strings: Kaplan (Z₀ 1.46 / 1.00 / 0.67 / 0.56 kg/s, KB §2.1), Z_R 3.3 Z₀
  (KB §2.4), scale 0.69 m.
- Losses fitted to the Iowa MIS pizzicato (every partial's decay, pp/mf/ff,
  all strings; `tools/partial_q.py --iowa`): σ_L 1.4/s, σ_H 24/s (the C ÷
  0.8), f_T 900 Hz; a stopping finger adds 2.9 / 5/s. Median Q per band
  within ~15% of the recordings' up to 2 kHz (EVALUATION.md). Far less
  treble loss than the violin: cello partials ring long.
- **Body modes, measured** (cello doc §3.1, §8): A0 100 Hz (Q 40, weak at
  the bridge), T1 140 Hz (weak), the wolf mode 185 Hz (Q 50, 0.17 s/kg at
  the C: m_eff 0.25 kg, in the measured 0.12–0.36), 209 Hz, 281 Hz, and the
  measured corpus mode at 433 Hz (Q 53, m_eff 180 g); the bridge hill at
  1.35 kHz (Q 3, 0.06 s/kg; the cello bridge's in-situ resonance, 1.2–1.5
  kHz on three cellos) over a dense band from 0.8 to 5 kHz every ~100 Hz.
  The cello doc's guess of a dense band from ~400 Hz ([D]) made the open
  strings' partials at 500 Hz–1 kHz decay twice as fast as the Iowa
  pizzicato's; from 800 Hz they are within ~25% (`partial_q.py`).
- **Per-string mode strengths** (`BridgeMode::w`, `Bridge::solveWeighted`):
  Zhang et al. found the wolf mode strongest at the C string's notch,
  falling toward the A, and the 209 Hz mode the reverse. Weights 1 / 0.9 /
  0.75 / 0.6 and 0.6 / 0.75 / 0.9 / 1 (C..A; residue ∝ w²) [T: the order is
  measured, the ratios aren't]. Each weighted mode is driven by Σ w_s F_s
  and moves string s's end by w_s times its velocity: a 4×4 admittance,
  solved as u = (I + Y0 Z)⁻¹(Y0 S + h) with the inverse precomputed when the
  coefficients change. With all weights 1 it matches the scalar solve to
  −111 dB (float rounding); the violin has none and keeps the scalar path.
  Native speed 10× real time (14× before), the browser 5.9× (6.6×).
- **The wolf** comes from the 185 Hz mode through the bridge solve: Z₀|Y|
  there is 0.25 on the C, 0.14 on the G, 0.07 on the D (the violin's
  strongest, its B1 modes, ~0.06). As the performer plays (`wolf_check.py
  --simple`), F#3 wolfs on the G string at mf (rough, beating) and on the C
  at every dynamic (sub-harmonic at pp, multiple slipping beating at 6–8 Hz
  at mf–ff); its neighbours and the D string are clean. The cello doc's six
  acceptance tests (§5; `wolf_check.py`, bow pinned after the performer's
  attack):
  1. it wolfs only within ±25 cents of the mode: pass;
  2. its beating gets faster with bow speed and slower with force (beat
     rates 5–6 Hz; measured 4–14): pass, wherever it beats;
  3. the worst note rises as the bow nears the bridge: **not reproduced**
     (from β 0.13 to 0.09 it stays near the mode; at 0.06 it falls);
  4. the Wolf Eliminator removes it: pass;
  5. odd harmonics beat more than even ones (Firth): pass (4.7 vs 1.7 dB);
  6. lower string impedance shrinks its range: pass (3 pitches → 1 at
     Z₀ × 0.7).
  Pinning the bow from the first sample started the wolf note in steady
  multiple slipping; pinned after the attack, it beats.
- **Wolf Eliminator** (`body.wolf`, 0–1; `InstrumentDef::wolfMode`,
  `Bridge::setDamper`): a mass damper on that mode, as a real eliminator
  clamped on a string's afterlength: its Q ÷ (1 + 3 a), its frequency
  × (1 − 0.03 a), its mass kept, so its peak admittance falls with its Q.
  0.5 cures the G string and the C at mf–ff (the C's pp at F#3 stays weak
  even at 1: its narrow pp window). The radiation's own modes don't follow
  it. Greyed for the violin, which has no `wolfMode`.
- **Friction: the violin's rosin**, deliberately. The cello doc (§4.1)
  has two measurements of cello rosin, static μ ≈ 0.65 and sliding
  friction falling hyperbolically to 0.35. Fitting the enhanced model's
  speed terms to them (`hSpeed` 0.05, `hFloor` 0.6, within 0.02 from 0.2 to
  25 cm/s) put the D string's Helmholtz window far above Galluzzo's
  measured one (`tools/schelleng_check.py`: F_min 5×, F_max 1.6× his; the
  thermal model alone 13× and 3×). With the model's own rosin (μ 1.2 cold,
  0.35 hot) the window's centre matches his within 6%, but it is 2.4× too
  narrow (F_min 1.7×, F_max 0.7×). The thermal model's ambient μ is a
  cold-contact value, and Galluzzo also measured μs roughly doubling (to
  ~1.3) when the bow starts slowly, so the measured 0.65 isn't this
  model's parameter. Widening the window is friction-model work for later
  (the CC BY mdw Vienna data would be the test, held back for now).
- **Published acceptance tests**, on the open G alone (`string_lab tone`):
  - pitch flattening in Helmholtz motion −1 to −30 cents, growing toward
    the top of the window and at slow bow speeds (measured −6 typical, −50
    to −60 at worst, cello doc §4.4);
  - ALFs: at 5 cm/s, forces above 2–4 N drop to half or a third of the slip
    rate (measured: mostly above 4 N, ~f₀/2), none at 20 cm/s (as measured).
- **The playability assistant** (`tools/calibrate_force.py --instrument
  cello`, Helmholtz by the stick test of KB §7.3: stick fraction, slips per
  period, jitter): clean at Pressure 0 with a mean margin of 0.62 (the
  violin 0.62; 0.57 before 8.3's gentler body-force term), every cell
  clean at Pressure 0. An earlier test of the synth's output by periodicity
  (`playability.py`, removed) passed notes that slipped an extra time every
  few periods, as the KB warns. The changes from the violin's law, all
  `InstrumentDef` fields the violin leaves at its own values:
  - force across strings × (Z₀/Z_ref)^1.0 (`forceZExp`; the violin's 1.39
    asked 2× too much on the C; the window's centre grows as Z₀, and Beigi
    & Conneely's F_min as Z^1);
  - the body-force term `bodyForceK` 4 (the violin's 25 counted nearly every
    cello note: Z₀|Y| ~0.04 is common on these strings; 8 pressed 2.8×
    harder on the wolf note and hid the G string's wolf);
  - **the contact point between per-string limits** (`betaFloorA/B`,
    `betaCeil`) from the coupled instrument's windows (`tools/window_map.py`,
    β, speed and force pinned): β ≥ A + B·v_b (a fast bow near the bridge
    can't hold Helmholtz motion on a heavy string at any force: F_min ~
    Z₀²v_b/β²; C 0.04 + 0.2 v_b, G 0.05 + 0.1 v_b, D 0.05 + 0.12 v_b, A
    0.065 + 0.1 v_b) and β ≤ the ceiling where windows break up (C 0.09, G
    0.13, D/A 0.15). Cellists in repertoire average β 0.15–0.22 (cello doc
    §6); this model's windows break up there;
  - the bow's speed: 5 cm/s at pp (`vbPP`, `vbGrowth`: ~0.33 m/s at ff),
    the C and G at the violin's 0.10 m/s pp (`vbPPScale`: at 5 cm/s their
    windows are one Pressure step wide);
  - force trim 0.85 on the C; ff lifts 0.4 / 0.6 / 0.6 / 1.0, pp lift 0.1
    on the G; forces capped at 20 N.
  - Pinning speed and force from the first sample (`window_map.py`) can
    start a note in multiple slipping that then persists: Galluzzo's
    starting force is ~4× the sustaining one. The performer's own attack
    (`calibrate_force.py`) is the reference.
- **Dynamic range** (pp → ff, a held note on each string, Modelled output):
  C 4, G 10, D 11, A 11 dB; the Iowa arco recordings (anechoic, the same
  mic for every dynamic) C 13, G 13, D 10, A 9 dB; the violin ~20 dB on
  every string. The C's pp sits at its contact ceiling, the price of a
  playable pp on its narrow windows: the model's main limit on the cello.
- Open strings: the allowance 13 / 16 / 12 / 11 cents (C..A), within ~6
  cents p..ff except the open A at pp and ff (+6.5, −6.5)
  (`open_strings.py --instrument cello`).
- Radiation: the CC0 cello-section library curve alone (KB §12.7); the
  Library Mic outputs the section's responses. Levels: as loud as the
  violin through the Library Mic (Smooth); Modelled 1.5 dB above it, as on
  the violin.

## String (KB §6.1, §10)

Four one-way fractional delay lines (4-point Lagrange) carry velocity waves:
bow→nut, nut→bow, bow→bridge and bridge→bow. The string's own losses are
lumped into the terminations as first-order loss shelves (below).

- **Lengths:** the bridge side is β × the note's period. It doesn't move with
  vibrato, because vibrato moves only the finger (KB §11.2). The filters'
  phase delay at the pitch is subtracted from the loop, so the loop is in
  tune. Both lengths glide over about 1 ms.
- **Losses: decay rates by frequency, not by note** (KB §2.3, Valette–Cuesta:
  1/Q = air + viscous ∝ f² + thermoelastic). Each partial decays at
  σ(f) = σ_L + (σ_H − σ_L)·f²/(f² + f_T²) per second, whatever note it
  belongs to [T]:
  - the string's own: σ_L 2.6/s, σ_H 40/s ÷ the string's `hf` (G 0.55,
    D 0.7, A 0.85, E 1), f_T 2 kHz; 10% at the bridge end, 90% at the nut or
    finger end;
  - a stopping finger adds 7.5/s low, 18/s high (KB §2.3: η_F × 3). A
    sliding finger presses lighter: its share × (1 + 1.5 v).
  Each end is a first-order shelf (`LossShelf`, `model/Delay.h`), designed
  per note so one round trip (one period) applies exp(−σ(f)/f0) at every f.
  Fitted with `tools/partial_q.py`: each partial's decay after a pluck
  (Philharmonia pizzicato, initial 25 dB), as Q = π f τ. The bridge and body
  add their own losses in the synth, so the fit was iterated on the totals.
  Median Q, real / synth:

  | | 150–500 Hz | 0.5–1 kHz | 1–2 kHz | 2–4 kHz | 4–8 kHz |
  |---|---|---|---|---|---|
  | stopped | 177 / 182 | 260 / 266 | 304 / 322 | 355 / 412 | 651 / 699 |
  | open | 444 / 450 | 444 / 468 | 398 / 407 | 273 / 478 | 1420 / 1038 |
  | stopped, before (low-passes) | / 148 | / 128 | / 120 | too fast | too fast |

  Until this change the losses were one-pole low-passes (bridge 9 kHz, nut
  12 kHz, finger 5 kHz) with g from a T60 (open 2.2 s, stopped 0.9 s). A
  low-pass takes the same fraction on every pass, and a high note makes
  more passes per second, so stopped notes' treble died in milliseconds
  instead of ringing ~0.5 s: the recordings' decay rates level off at
  25–30/s above ~2 kHz. Open strings' 2–4 kHz partials still ring longer
  than the (few) measured ones.

  A top low-pass above the measured range (to round the Helmholtz corner)
  was tried and dropped: it made the treble's damping depend on the note
  again (stopped 4–8 kHz Q 255 vs 651).
- **The loop's gain at DC must not exceed its gain at the fundamental.**
  The shelves' gain falls with frequency, so this holds by construction.
  Phase 1 raised g to make up for the low-passes. On high notes that pushed
  the loop's DC gain to ~1, and the waveguide then held a DC wave: a uniform
  slope that a string fixed at both ends can't have. The bow's one-way drag
  fed it, and it barely decayed (the open E "rang" for ~15 s at DC, hidden by
  the output DC blocker but offsetting the velocity the bow sees). Now g
  wasn't raised. Two fixes that failed, for the record:
  - Blocking DC in the loop breaks Helmholtz motion. The bow's mean drag is
    held by a static deflection, which in a waveguide is a DC wave in each
    segment.
  - Making the ends lossless only at DC made a ~5 Hz resonance.
- **Open strings carry a bow allowance while bowed [T].** Bowing flattens a
  string, and no finger can correct an open one. Violinists tune while
  bowing, so the performer raises an open string the bow is on by G +15.5,
  D +10.3, A +11, E +9.2 cents at p–mf (`openTuneCents`), falling smoothly
  to 70% by ff, where the faster bow with its force at the cap flattens the
  string less. Earlier values: G +16.5, D/A/E +6.5 at every dynamic (the
  loss shelves, when long notes still slowed); G +15.5, D +11.5, A +14,
  E +16 (low-pass losses). It eases in and out over
  ~30 ms as the bow lands and leaves. Harmonics, bowed much lighter, get
  0.55× of it. Unbowed open strings sit at true pitch, so they ring in
  sympathy with stopped notes. `tools/open_strings.py`: D, A, E within
  ±1.7 cents; G +3.1 at p, −0.7 at mf, −2.3 at ff.
  - Until the open-strings PR the allowance (+7/+4/+9/+12) applied to every
    open string. It was held to 60% so unbowed strings could still ring,
    which left bowed open strings 4–10 cents flat.
  - In tune, an open string's partials that coincide with another open
    string's (D's 3rd = A's 2nd) come out 2–4 dB lower: they interact
    through the bridge. Open notes are 0.7–1.9 dB quieter than before
    (none of it with body coupling off).
- **Tuning:** the shelves delay low partials a little more than high ones
  (~0.4 vs ~0.1 samples). The loop is tuned at the partial that sounds: the
  fundamental, or the k-th for a harmonic touched at 1/k.
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

**High-frequency loss per string [T]:** the string's high decay rate is
divided by G 0.55, D 0.7, A 0.85, E 1 (until the loss shelves, the
low-pass corners were multiplied by them). Thicker, wound strings lose their high
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

Enhanced Thermal with the bow width and the loss shelves (`LAB_WIDTH=3 string_lab sweep 1 62 0.13 enhanced`):

```
beta \ F    0.02  0.04  0.08  0.15  0.30  0.60  1.20  2.40  4.80
   0.020     r-99   r-99   r+85   m +6   m +4   m +2   m +0   H -1   r-22
   0.030     r-99   r-99   m +5   m +4   m +2   m +1   H -1   H-11   r +5
   0.045     r-99   r-99   m +4   m +2   m +0   H -1   H -7   r-60   r+99
   0.065     r-99   r-99   m +2   m +0   H -1   H -6   r-22   r+30   r-99
   0.090     r-99   m +1   m +0   H -1   H -4   H-13   r-83   r-51   r-99
   0.130     r-99   m +0   H -1   H -3   H -7   r-28   r+79   r+99   r+99
   0.180     r +0   m -1   H -2   H -5   H-14   r-88   r+31   r-11   r-99
```

The window's low-force edge moved up with the loss shelves (Schelleng's
minimum force rises as the string's damping falls), and flattening is
smaller. Before them:

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
- **A key arriving twice** (a note-on for a key already held, within 30 ms
  of its press) is ignored: a keyboard the browser sees as two MIDI inputs
  (with "All MIDI inputs"), or a doubled MIDI route, sends every key twice,
  and a real key can't go down twice without coming up. In separate strokes
  (sautillé, spiccato, martelé) the repeat restarted the stroke, a doubled
  attack, and its zero gap read as a very fast passage; in arco it slurred
  inaudibly. A fast sautillé passage with every event doubled 2 ms later now
  renders bit-identical to the single one. The kit's voice count still
  shows 2 per key (its silent voices follow the raw keys).
- The string is chosen per note: the highest string whose open note is at or
  below it. Through a slur, the current string is kept if the note is within
  a fifth above its open note.
  - **Open strings can't take vibrato** (`Avoid Open Strings`, default on;
    the left-hand PR). With vibrato on, arco or martelé, and the keys at
    least 0.25 s apart, a note that would be an open string (D4, A4, E5) is
    stopped with the fourth finger on the string below; fast passages keep
    the open string, as players do. The Philharmonia's D4, A4 and E5 swing
    15–70 cents like the stopped notes around them, so its players stopped
    them too. The open string then rings in sympathy at the note's pitch
    and loads the bridge there: the stopped A4's fundamental is ~7 dB
    weaker than the open A's (−10.9 vs −3.4 dB re partials 2–4; −4.3 with
    the bridge decoupled), level with G4 and G#4 on the same string (−8,
    −10). The labs and `open_strings.py` turn the habit off.
  - **A slide keeps the string:** with `Portamento` above 0, a slow (≥ 0.3 s)
    slurred leap of a third or more from a stopped note stays on the string,
    up to an octave above its open note, and shifts (below).
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
  F is capped at 5 N × the high-position factor below (3 N flat until the
  loss shelves: the less damped string needs more force low on the G at ff,
  and up the G and D strings the window has no upper edge up to 20 N).
  Forces here are this model's, trimmed to its windows; they run above
  players' (0.5–2.5 N).
- **Calibrated against the coupled instrument** (`tools/calibrate_force.py`).
  The script sweeps Pressure for notes on every string at four dynamics and
  reports the range with clean Helmholtz motion. Four corrections followed:
  - **A margin**, most at pp: log₂ F += 1.5 (0.1 + 0.8 (1 − d)² + k d²).
    Soft notes sat at the bottom of the window; with the loss shelves, so
    did ff close to the bridge on the lower strings: k = G 0.6, D 0.3,
    A 0.3, E 0 (`ffLift`; the E is near the top of its window at ff). Since
    the long-note change the open G at pp double-slipped now and then with
    the full pp lift, so the G's is 0.3 instead of 0.8 (`ppLift`).
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
notes, 22 on halves, 54 on quarters, with the bow 25–40 mm from the bridge
throughout). From 0.25 s into a stroke, over 0.6 s, the bow slows to
`Long-Note Bow Speed` (default 1) × the attack speed, and to no more than
0.22 m/s (Demoucron's half notes). β stays where the attack put it, as Demoucron's players kept
theirs, so the slowed note settles softer (7.7 ln of the slowdown, dB).
With the default, p–mf notes (0.12–0.2 m/s) keep their attack speed and
f–ff long notes slow to 0.22 m/s (ff ~−4.5 dB).
- **Why (the bow-brightness PR):** until then the default was 0.6 and β
  moved toward the bridge to keep v_b/β, the loudness, the same, no closer
  than β 0.06. At p that moved the bow a lot (β 0.17 → 0.10) and
  brightened it; at f it was already at the floor, so it only slowed and
  dulled. The string's own p→f brightening (it has some since the loss
  shelves) was erased: +0.5 / +0.6 / −0.2 dB at 2–4 / 4–8 / 8–16 kHz with
  the output tilt off, against +1.3 / +2.9 / +3.4 without it (VSCO +2.5 /
  +6.4 / +8.0). Slowing at the same β instead (default 0.6) sagged every
  long note ~4 dB, and the perceptual distance got worse (nearer on 157 of
  448 notes); no slowing was nearer on 269.
- **The 0.22 m/s cap:** an ff bow held at its attack speed (0.4 m/s,
  β 0.034) heats the rosin until no force holds Helmholtz motion (A4: clean
  only near 2 N, raucous from 2.8 N, between 1 and 1.5 s). Players can't hold
  it either: it spends the bow in ~1.5 s. At 0.25 m/s B3 at ff still needed
  more than the 5 N cap.

**Attack swell (`Attack Swell`, default 0.75; the perceptual-distance work).**
A bowed note grows into its level. Measured on the Philharmonia violin (arco,
0.25–1 s notes, onsets aligned): the first 60 ms sit 12–13 dB under the
note's peak at pp–mp, ~7 dB at mf, 3–5 dB at f–ff, and the level is within
3 dB after ~200 / ~100 / ~68 ms (median), whatever the note's length. The
synth was at full level in 32–42 ms at every dynamic. Now each stroke from
rest starts D0·(1 − t/T)² dB under its level, times the control, with D0 and
T per dynamic (interpolated; d = 0.24 … 0.79 are velocities 20 … 120 at the
default Velocity → Dynamics) [T: fitted to the per-dynamic rise and attack
distance, the attack-rise PR]:

| | pp (0.24) | p (0.35) | mp (0.46) | mf (0.571) | f (0.681) | ff (0.792) |
|---|---|---|---|---|---|---|
| D0, dB | 18.5 | 20 | 23 | 12 | 6.6 | 5.9 |
| T, s | 0.36 | 0.36 | 0.356 | 0.2 | 0.268 | 0.26 |
| rise, synth / recordings, ms | 166 / 153 | 166 / 176 | 174 / 201 | 78 / 75 | 52 / 50 | 34 / 34 |

mf starts deep and recovers fast: the recordings are ~7 dB down at 60 ms yet
within 3 dB by 75 ms. Until this PR D0 was 15 dB at d ≤ 0.45, falling 45 dB per
unit of d to a 4 dB floor, and T 0.36 → 0.26 s: f had no swell left (30 ms vs
50), mf was slow (104 vs 75), the soft notes not deep enough. The recordings'
soft notes also swell faster when short (pp: 114 ms on 0.5 s notes, 184 on
1 s); a keyboard doesn't know a note's length, so the table sits between. 40% of the deficit is a slower bow (the bow accelerates through
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
notes nearer. With the per-dynamic table (attack-rise PR) soft attacks
(G3–G6, velocity 20) keep everything below the note 13–60 dB down from the
first 20 ms, as on main.

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
- **Lighter finger:** the finger's decay rates are × (1 + 1.5 v) [T]. The
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
dynamics, except G5 on the G at p and mf (already so before the loss
shelves). High on the E, G6 at ff and C7 at mf now hold; A6 at ff does not
(nor did it before).

**Vibrato (stopped, bowed strings).** `Vibrato Depth` is peak-to-peak at mf,
scaled 0.6–1.4× by dynamics; the rate is ×0.9–1.1. It ramps in over 350 ms
after `Vibrato Delay`, with ±5% rate and ±15% depth drift per cycle.
Defaults 26 cents after 0.1 s (until the shimmer PR, 16 cents after 0.25 s,
the VSCO player): both recorded players vibrate from the onset, VSCO's at
~11–21 cents and the Philharmonia player's at ~31–36, so the default sits
between them, moderate like the other habits.

**The vibrato's centre wanders (the shimmer PR).** The owner heard a ring in
the synth's vibrato, like beating, that real vibrato doesn't have. A vibrato
around a perfectly fixed centre leaves each partial a steady line at its
centre pitch (the frequency modulation's carrier): the per-cycle rate and
depth drift smear the sidebands but not that line, so it rings against the
moving ones. Measured (`tools/perceptual.py`, ring: the line's level over
the vibrato's smear, partials 2–8): synth 11.7 dB, recordings ~7. Real
players' centres wander: 2.2 cents (std, on a ~0.17 s moving mean) on the
Philharmonia violin, 3.4 on VSCO's, against 0.6 for the synth (the tracker's
noise). Their cycle-to-cycle irregularity disagreed (Philharmonia 3% period,
5% width, like the synth's; VSCO 30%, 46%, partly its narrow vibrato), so it
isn't the consistent difference. Now the centre drifts, an
Ornstein-Uhlenbeck process (τ 0.25 s, 2.5 cents std) scaled with the
vibrato (none on a straight note) [T]: measured 2.7 cents. With the wider
default, ring 8.3 dB.

**Bow wander (tried and left out, the shimmer PR).** A delayed-correction
loop drifting bow speed, contact and force. Letting speed and contact drift
apart made the level sag (an unsteady player); moving them in step shifted
the overtone balance at a steady level, but the owner wasn't convinced either
way: the recordings' sustain variation may have better explanations (the
player's note shaping, vibrato through the body, the room). Not in the synth.

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
- Until the left-hand PR it almost never sounded: string choice moved most
  such leaps to another string (it kept a slur on the string only within a
  fifth of the open note), so no shift happened. Now a player who slides
  keeps the string (Gesture layer, string choice). `tools/portamento_glide.py`
  (10 slow slurred leaps; those that cross strings or leave an open string
  don't slide): at 0 every glide 0–12 ms; at 0.3, 78–116 ms on the five that
  shift; at 1, 178–292 ms.
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
    1/k as long. β is divided by √k (until the left-hand PR by k, which put
    the bow as far along the segment as a stopped note's and gave the
    harmonic a stopped note's spectrum), and the force is ×0.3 (octaves and
    twelfths) or ×0.15 (two octaves). With the arco force the motion falls
    back toward the fundamental (D6 on the D string came out a series at
    2.6 × f0); at force ×0.45–0.6 the harmonics were still 10–30 cents flat.
  - **Intonation** aims for k slips per fundamental period.
  - Measured through the synth (open and stopped, A5–B6): within ±5 cents,
    the fundamental 33–65 dB down; B6 still ~15 cents flat. Since the
    left-hand PR (`harmonics_tuning.py`): within ±3 cents, B6 −1.
  - **Timbre (the left-hand PR).** A harmonic sounds purer than a stopped
    note at the same pitch. In the Philharmonia recordings (harmonic vs
    arco-normal at the same pitch, so mics and body cancel) partials 3–6 are
    5–12 dB weaker; the synth's harmonic had the stopped note's spectrum
    (`tools/harmonic_timbre.py`: +0.7 / +5.6 / +9.9 / +6.7 / +12.2 dB at
    partials 2–6). The physical reason is the finger pad (~1 cm, not the
    KB's point): the sounding partial's multiples have their nodes under it
    but not quite at its edges, so higher ones are damped more. Modelled
    that way (an extra loss shelf on the touched string) it stopped the
    light bow a harmonic needs: the notes broke, even with more force. So,
    like the brightness tilt, the output is low-passed instead, outside the
    loop: 2-pole, Q 0.707, at 3 × the sounding pitch, crossfaded in over
    ~20 ms while a bowed string is touched [T]. Now −0.5 / +1.9 / +2.6 /
    −2.1 / −2.0 dB (mean 1.8; 7.0 before).

## Output (KB §12)

**Modelled (default): `model/Radiation.h`.** A bank of body modes driven by
the total bridge force (KB §12.3a):
- 16 per octave from 180 Hz (the cello: 60 Hz; `RadiationShape`) to 1 kHz
  (Q 22), and 48 per octave from 1 to 10 kHz (Q ~80, falling slowly), with a
  little random jitter: 199 modes (the cello 225).
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
- **Low band (below 600 Hz; the cello 300 Hz):** a monopole, the same
  residue in every direction (KB §5.5, §12.1).
- **High band (above 1.2 kHz, blended over 600–1200 Hz; Phase 7a):** each
  mode has a random radiation pattern: real orthonormal spherical harmonics
  up to order ⌈ka⌉ + 1 (violin a = 0.2 m: order 6 at 1.2 kHz, 17 at 4.3
  kHz, 39 at 10 kHz; cello a = 0.4 m, capped at 39 from ~5 kHz) with random
  Gaussian coefficients. It is smooth at 1 kHz and
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
(`string_lab tone`) with the low-pass losses. With the loss shelves it does:
×4 force at fixed v_b and β gives +3 / +8 / +8 dB at 2–4 / 4–8 / 8–16 kHz on
G3, +1 / +3 / +9 on A4 (the bow width barely matters). In the instrument,
with the tilt off and since the long-note change (Gesture layer), the p→f
spread is about half the recordings' (+1.3 / +2.9 / +3.4 dB; VSCO +2.5 /
+6.4 / +8.0), so the tilt does the rest at 5 dB per octave of force (it was
8). The
VSCO notes brighten by +3..+15 dB above 4 kHz from p to f; the synth's p was
up to 12 dB too fizzy at 8–16 kHz on low notes (what read as "bow noise",
though its inter-harmonic noise is below the recordings') and its f a little
dull. So the output (after the bridge, outside the loop: bowing is
untouched) is tilted by 5 dB per octave of bow force re mf [T], half on a
first-order shelf at 2.5 kHz and half at 7 kHz, clamped to ±9 dB. The force
is the players' line from `computeTargets`: f ∝ e^(3.62·dynamic) ×
2^(1.5·pressure) × 2^(2.09·contact), so aftertouch and bowing nearer the
bridge brighten too, as the player doc says they should (§1). It is zero at
dynamics 0.557 (mf, the middle of the p/f pair the radiation was fitted
on), so the radiation target still holds (`radiation_target.py` unchanged
within ~1 dB per band), and neutral when not bowing. Measured over G3–E5
(median f − p, 2–4 / 4–8 / 8–16 kHz): +4.0 / +6.4 / +7.4 dB, VSCO +2.5 /
+6.4 / +8.0 (with no tilt and the old long-note rule the synth was 7–8 dB
short above 4 kHz). G3 and
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

**Re-measured for the loss shelves** (the string is several dB brighter
above 4 kHz; before, the modelled output came out ~7 dB too bright above
6 kHz). Modelled minus real per half-octave band from 350 Hz to 11 kHz:
−2.0, −2.7, +0.6, +2.5, +2.5, +1.6, −2.9, −2.8, −0.6, −2.8 dB.

**Result (before the loss shelves):** Modelled minus real, per half-octave band from 350 Hz to
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
python3 tools/combine_target.py <library csv> <measured csv|-> <out csv> [--from=HZ]  # the radiation target ("-": the library curve alone)
./build/instrument_lab release <note> <1 lift|2 stop|3 finger off> [relVel]   # decay and the pitch left ringing
  env: LAB_HF=1 (measure above 1 kHz), LAB_DYN, LAB_PRESSURE, LAB_COUPLING, LAB_MUTE, LAB_STRING, LAB_CHANGE, LAB_FLOOR,
       LAB_WIDTH, LAB_SUSTAIN, LAB_MINACC, LAB_AVOIDOPEN=1 (the open-string habit; off in the labs), LAB_TRACE=from,to
python3 tools/analyze.py render.wav [out.png] [--fmin 58]            # spectrogram, level, pitch per note (--fmin: the cello)
SM_INSTRUMENT=cello ./build/<any lab> ...                            # the labs on the cello (default: violin)
./build/string_lab guettler <string> <note> <beta> [classic|enhanced]  # attacks from rest (Guettler plane)
  env: LAB_WIDTH=<samples> LAB_SHARE, LAB_HFLOOR LAB_HSPEED LAB_TDROPE (enhanced friction)
./build/radiation_lab character [a0 nasal brilliance softness voice%] # body controls per band
./build/radiation_lab directivity                                  # one omni around the violin: per octave, per semitone
python3 tools/room_power.py [--instrument cello]                    # each Space's wet/direct power (the distance trim)
python3 tools/calibrate_force.py [--instrument cello]               # the assistant's place in each window (stick test, the string forced)
python3 tools/window_map.py [--vb 0.1,0.25]                          # the cello's coupled windows: beta x force, speed pinned
python3 tools/schelleng_check.py                                     # the cello D's window vs Galluzzo's measurement
python3 tools/wolf_check.py                                          # the wolf's six acceptance tests (cello doc §5)
python3 tools/wolf_check.py --simple [--string 1] [--notes 52-56] [--param body.wolf=0.5]   # the wolf as played: beat, rate, regime
python3 tools/partial_q.py --instrument cello --iowa <Iowa pizz wav dir> [--dyn pp,mf,ff]   # the cello's losses
python3 tools/iowa.py <Iowa wav> ...                                 # the notes found in Iowa MIS files
./build/bowing_lab <metric> [bpm num den] < events                   # bow direction per note, mid-note rebows
  instrument_lab env also: LAB_FRICTION=0|1|2, LAB_BOWWIDTH=<m>, LAB_AVARY
python3 tools/open_strings.py | harmonics_tuning.py | intonation_check.py | vibrato_extent.py
python3 tools/pizz_decay.py <VSCO Pizz dir>                          # two-stage decay vs recordings
python3 tools/note_pitch.py <scenario> [wav]                         # pitch per note, peak below
LAB_HF=1 tools/bow_change_gap.sh [note]                              # automatic bow change gap
python3 tools/partial_q.py --data <phil wav>                          # each partial's decay (Q) after a pluck
python3 tools/harmonic_timbre.py --data <phil wav> [render]           # harmonics vs arco, real vs synth
python3 tools/portamento_glide.py [render]                            # glide times at Portamento 0 / 0.3 / 1
python3 tools/brightness_spread.py --vsco <Arco Vib> [render ...]     # f - p colour, recordings vs synth
python3 tools/force_brightness.py [enhanced classic thermal]          # the string alone: brightness vs force
python3 tools/fundamental_strength.py --data <phil wav> [render]      # 1st partial re 2-4, recordings vs synth
./build/performer_fuzz [n] [doubled]                                  # key handling: one key -> one note
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
