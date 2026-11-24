# Plan

A physically modelled bowed string synth, violin first, then cello, then
possibly viola and bass. Physics: [10d6/instrument-research](https://github.com/10d6/instrument-research)
(the KB). Platform: [synthkit](https://github.com/10d6/synthkit). Model
details: [MODEL.md](MODEL.md).

## Decisions

- **One plugin** (`string-model`, plugin code `Stmd`, vendor `Tend`) with an
  `Instrument` switch. Violin now; cello, viola and bass are added as
  choices, each an `InstrumentDef` (strings, losses, body, bow) in
  `model/defs/`.
- **A regular keyboard** is the main controller:
  - velocity sets the attack and per-note dynamics
  - the mod wheel / CC 11 sets dynamics
  - aftertouch sets bow pressure
  - CC 74 sets the contact point (tasto ↔ ponticello)
  - pitch bend moves the finger
  - overlapping keys slur
  - keyswitches choose the string (C1–E1) and the articulation (F1–B1)
  - a playing transport (DAW, or the page's Play) gives the meter: accents
    and bowing follow it; without one, those rules are off.
- **Player habits on by default, at moderate settings** (the owner's choice):
  Intonation Style 0.7, Vibrato Shaping 0.7, Metric Emphasis 0.5,
  Portamento 0.3. Each has a control to turn it down.
- **Auto Bow Change off by default** (the owner's choice): long notes are an
  endless bow unless it is turned on.
- **Plugin builds are off for PRs** (the owner's request). They run for `v*`
  tags (which publish a release) and when CI is run by hand with "plugins"
  ticked, for milestones.
- **The whole instrument runs in `processGlobal`**, so the strings can share
  a bridge. The kit's voices only track keys.
- `model/` is plain C++20 with no synthkit dependency, so it can be tested
  offline (`tools/string_lab`, `tools/instrument_lab`).

## Phases

| # | What | Status |
|---|---|---|
| 0 | Scaffold from the synthkit template, CI, Pages | done |
| 1 | One bowed string you can hear: waveguide + thermal friction (+ classic for A/B), torsion, gesture layer with playability assistant, slurs/détaché/bow changes, vibrato, intonation, string choice, library-mic output, room | done (#1) |
| 2 | Four strings on a shared bridge: modal bridge admittance Y(z) solved exactly with the strings, sympathetic ringing, fingers lifting off unused strings, double stops and rolled chords, force calibration against the coupled instrument, string DC-loss fix | done (#2, #3) |
| 3 | Body and output: stereo modal radiation calibrated against real recordings (CC0), bridge hill, mute, body resonance, width; library-mic and pickup kept | done (#4) |
| 4 | Gesture polish: release types (lift / stop on string / finger off, or by key-up speed), shifts with finger loss and winding noise, velocity-controlled slide speed, string keyswitches | done (#5) |
| 5 | Articulations: martelé, spiccato and tremolo from Demoucron's measured strokes, pizzicato (force pulse at the pluck point; strummed chords), natural and artificial harmonics (touch junction), articulation keyswitches | done (#6) |
| 6 | Refinement: enhanced thermal friction for attacks (Woodhouse–Galluzzo 2025 structure, now the default), two-point bow width, pizzicato's second polarisation (fitted to recordings), sautillé, radiation re-measured, CPU (radiation as arrays, WebAssembly SIMD), a VST3 milestone build (CI run by hand) | done (#8) |
| 6a | Body character (KB §5.8): Air Resonance, Nasality, Brilliance, Softness, Voice, four body presets | done (#9 → #10) |
| 6b | Player habits (instrument-research `player_technique_and_responsiveness.md`): Intonation Style (Pythagorean from the open strings, key estimated, double stops pure), Vibrato Shaping, double-stop weighting | done (#11) |
| 6c | Meter and tempo: Metric Emphasis (accents, bowing to the meter with re-takes), bow weight frog/tip, spiccato length from the note rate, Portamento | done (#12) |
| 6d | Open strings in tune under the bow (allowance only while bowed); evaluation tools moved into `tools/`; docs and lessons (`docs/EVALUATION.md`) | done (#13) |
| 6e | Slurs: the left hand's position (finger changes in 2–3 ms with a tap or pluck; L-type shifts on a minimum-jerk curve), Slur Range; Auto Bow Change off by default; info text for every control | done (#14) |
| 6f | Low notes: a sharp low cut below A0 (G3's fundamental was 17 dB too strong), brightness that follows bow force (p darker, f brighter, as in the recordings) | done (#15) |
| 7a | Directional body: per-mode radiation patterns (random spherical harmonics + the measured front-up beam), a spaced omni pair you place with Mic Angle and Mic Elevation, the default reproducing the earlier sound | done (#16) |
| 7b | Distance and the room: Mic Distance (1/r direct, the room stays, re-gained), the room fed by the power response, player shadowing | done (#17) |
| — | The perceptual distance to the Philharmonia recordings (tools, #18); the attack swell, its first fix (#19) | done |
| — | Shimmer: a sharper, denser upper body (48/oct, Q 80), vibrato defaults 26 cents after 0.1 s, the vibrato's centre wanders (no more ring) (Bow Wander tried and left out) | done (#20) |
| — | String losses: decay rates by frequency, not by note (loss shelves fitted to the Philharmonia pizzicato's per-partial Q, `tools/partial_q.py`); stopped notes' treble rings as long as the recordings'; everything that rests on the string recalibrated | done (#21) |
| — | Bow brightness: long notes keep their contact point (Demoucron), so the string's own brightening with force survives; the output tilt does only the rest (8 → 5 dB per octave of force) | done (#22) |
| — | Attack rise: the swell's depth and length per dynamic, fitted to the recordings' rise per dynamic | done (#23) |
| — | Left hand: open-string notes stopped on the string below so they take vibrato (Avoid Open Strings); Portamento keeps the string so slides happen; harmonics' bow position and timbre | done (#24) |
| — | A key arriving twice (a doubled MIDI input) is ignored: it restarted sautillé strokes | done (#25) |
| — | Wrap-up before the cello: docs, reference numbers, the measurement scripts as tools (brightness spread, string brightness vs force, fundamental strength, perceptual breakdown, performer fuzz), the Phase 8 checklist (**this PR**) | |
| 7c | On hold (the owner's choice, after 7b): close mics: the near field (equivalent sources) and the bow-noise source, mic types with proximity effect, presets. (Swapped with the old 7b: at 1.5 m a cardioid's proximity boost starts ~20 Hz, so mic types only matter close.) | |
| 7 | Mic placement and room: close / studio / overhead / room positions (KB §12.3–12.4), near-field sources and bow noise for close mics, the room fed by the body's power response (darker, smoother), the room's level per output | |
| 8 | Cello: Kaplan string data, body with a strong ~175 Hz mode (the wolf note should appear by itself, plus a "wolf eliminator"), heavier bow. Then viola/bass | |

## Next

1. **Phase 8: cello** (a new conversation; then viola/bass). The owner
   chose to start it after the wrap-up PR. Phase 7c (close mics) is on hold.
   See "Starting the cello" below for what carries over and what doesn't.
2. Phase 7c (close mics: near field, bow-noise source, mic types and
   proximity, presets) when the owner asks for it.

### Starting the cello

What carries over unchanged: the waveguide, friction, bridge solve, loss
shelves, radiation machinery, room, performer, every tool. What has to be
made or measured again, roughly in this order (each step's command is in
CLAUDE.md, its reference numbers in EVALUATION.md):

1. **`InstrumentDef`** (`model/defs/Cello.h`, like `Violin.h`): strings
   (Kaplan data, KB §2.1: Z₀, ZR, `hf`), scale length (~0.69 m), `LossDef`,
   bridge modes and dense band (a strong ~175 Hz mode: the wolf should
   appear by itself), radiation data, low cut, `forceRef`/`vbRef`/`betaRef`,
   `forceMax`, `forceTrim`, `ffLift`, `ppLift`, `openTuneCents`, winding,
   pizzicato polarisation. Then an `Instrument` selector (the `instrument`
   parameter exists).
2. **Data.** Cello recordings to calibrate against: VSCO-2-CE has solo
   cello (extend instrument-research's `fetch_vsco.sh`, which fetches
   violin folders only); the Philharmonia set has cello (ask the owner for
   the zip, decode outside the repo, never commit; the tools take `--data`
   and default to violin file names, so `perceptual.py`,
   `partial_q.py`, `harmonic_timbre.py`, `fundamental_strength.py` need a
   prefix option).
3. **String losses:** `partial_q.py` on cello pizzicato → `LossDef`.
4. **Schelleng map and force:** `string_lab sweep`, then
   `calibrate_force.py` (its notes are violin strings: give it the cello's)
   → `forceRef`, caps, trims, lifts. Bow speeds and forces are higher on
   the cello (heavier bow, KB §7).
5. **Radiation target:** the pipeline in MODEL.md "Output"
   (`radiation_target.py` on VSCO cello → `combine_target.py` →
   `gen_radiation.py`), residual within ±3 dB; then `room_power.py`.
6. **Open strings and harmonics:** `open_strings.py`,
   `harmonics_tuning.py` (both list violin notes), `harmonic_timbre.py`.
7. **Check by ear and by score:** `brightness_spread.py`,
   `perceptual.py` against the cello recordings (its yardsticks again).

**Violin-specific constants still hard-coded** (move them into
`InstrumentDef` when the cello needs a different value, as `ffLift` was):

| Where | What | Violin fit |
|---|---|---|
| `Performer::computeTargets` | the dynamics law: v_b/β = 0.6 e^{3d}, v_b = 0.1 e^{1.4d}; the soft-playing margin 0.1 + 0.8 (1 − d)² | Demoucron, violin |
| `Performer` | `kLongVb` 0.22 m/s (long-note bow), sustain law | Demoucron's half notes |
| `Performer::swellDepth/Length` | the attack swell table per dynamic | Philharmonia violin rise times |
| `Performer::inReach`, `handFor` | the hand spans ~5 semitones; positions | violin; a cellist's hand spans ~3 in low positions |
| `Performer::tryChord` | chords span a fifth across adjacent strings | violin |
| `Performer::chooseString` | slurs stay within a fifth; Avoid Open Strings stops the note 7 semitones up the string below; Portamento keeps the string up to an octave | violin fingering (cellists reach a fourth/fifth with shifts or extensions) |
| `Performer` | spiccato/sautillé β 0.18 − 0.1 d; harmonics force ×0.3/0.15, `kTouchR`; vibrato depth 26 cents, register law (note − 69) | Demoucron / violin players |
| `Performer` | open-string allowance falls to 70% at ff; Intonation Style tables | violin |
| `Instrument` | `kTiltDb` 5 (brightness tilt, its drive 5.23 (d − 0.557) + 1.5 p + 2.09 c), `kHarmLP` 3 | VSCO violin p/f; Philharmonia harmonics |
| `src/Synth.cpp` | `kRoomPower` (per Space), keyswitch range | measured on the violin |
| `model/Radiation.h` | body radius a = 0.2 m for the radiation patterns (order ⌈ka⌉+1), the front-up beam, player shadowing | violin |

## Ideas not done yet (with why)

- **From the perceptual distance (Philharmonia, docs/EVALUATION.md), by size:**
  1. ~~Soft attacks~~: done (the attack swell): 53.4 → 41.6, attack 72 → 47.
  2. ~~Shimmer~~: mostly done (the shimmer PR): band against band 0.85 → 1.56 dB
     (recordings 2.55). Bow Wander (a drifting bow under the player's
     corrections) was tried and left out: the level sagged, and at a steady
     level the owner wasn't convinced. The rest: rooms (the recordings'
     studio adds some), players' larger vibrato, their note shaping.
  3. Vibrato: defaults moved to 26 cents after 0.1 s; its centre now
     wanders, so it no longer rings (ring 11.7 → 8.3 dB, recordings ~7).
     Notes that would be open strings are now stopped on the string below
     (left-hand PR).
  6. Fundamentals around 400–450 Hz (G4–A4 on the D string) are weak: A4's
     is 15–29 dB under the recordings' re partials 2–4
     (`tools/fundamental_strength.py`; mp included / mf–f only). Body
     radiation below B1−, and the open A loading the bridge at 440 Hz.
  4. The calibration target: VSCO's mics are ~7 dB brighter above 500 Hz than
     Philharmonia's; the synth follows VSCO. Choose (or average) deliberately.
  5. ~~Attack rise~~: per dynamic now within ~15 ms of the recordings except
     mp (174 vs 201) and the note-length effect a keyboard can't know.

- **Brightness from the bow, not the output:** the string now gives about
  half of the recordings' p→f brightening; the tilt (5 dB per octave of
  force) does the rest. The other half may be in the friction (slip-pulse
  width) or the dynamics mapping (VSCO's p/f may span more than vel 45/110).
- **The open G at the softest dynamics** sits at the edge of its window (a
  pp trim on the G keeps it clean); its allowance spans 5 cents p→ff.
- **Open strings' treble** (2–4 kHz) still rings longer than in the few
  measured plucks (Q 478 vs 273): open and stopped strings share the
  string's own rates.

- **"Character vs ease"** (KB §5.8): a body whose stronger, less damped low
  modes also make it harder to play. The body controls change only the
  radiated sound today.
- **Bit-identical browser and native builds**: own `pow`/`exp`/`sin` in
  `model/`, so the browser-vs-native check can cover chaotic passages again
  (it uses `tests/chord.json` because of chaos; `docs/EVALUATION.md`).
- **Odd-meter bowing**: separate quarters through a 3/4 bar can land the next
  downbeat on an up-bow (the bow is just past the 40% rule). Players hook
  two up-bows or re-take there.
- **Attack style** (player doc §2.1): the direction of attack imperfection
  (choked/articulate vs loose/soft) as a style control.
- **Hair tilt** as a colour/onset control (player doc §1.2), via the bow
  width's force split; overlaps Softness.
- **String-crossing lead, bow/finger asynchrony, KTH phrase timing**: they
  need the next note, which a live keyboard can't give without latency.
- **A "window width" control** (player doc §6.3): from fully assisted to raw
  physics, for expressive controllers; MPE and breath profiles.
- **Ricochet** and a real bow-bounce degree of freedom (KB §11.1); snap
  (Bartók) and left-hand pizzicato; col legno.
- **The kit's reverb** is ~40% of the browser's CPU: a synthkit PR (wasm SIMD
  for its FFT convolution).

## How each phase is checked

- synthkit's checks on every PR: render `--check`, the stress test,
  browser-vs-native (plugin builds only at milestones).
- The evaluation recipes and reference numbers are in
  [EVALUATION.md](EVALUATION.md).
- Model checks:
  - `string_lab sweep` (a Schelleng wedge; Demoucron's pp/mf/ff points inside)
  - pitch per note within a few cents on stopped notes (`tools/analyze.py`)
  - attack transients within the KB's limits (≤ 90 ms multiple slipping,
    ≤ 50 ms creak)
  - spectral comparison against the VSCO-measured response
- Listening on the PR's Pages preview.
