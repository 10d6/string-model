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
| 7b | Distance and the room: Mic Distance (1/r direct, the room stays, re-gained), the room fed by the power response, player shadowing (**this PR**) | |
| 7c | Next: close mics: the near field (equivalent sources) and the bow-noise source, mic types with proximity effect, presets. (Swapped with the old 7b: at 1.5 m a cardioid's proximity boost starts ~20 Hz, so mic types only matter close.) | |
| 7 | Mic placement and room: close / studio / overhead / room positions (KB §12.3–12.4), near-field sources and bow noise for close mics, the room fed by the body's power response (darker, smoother), the room's level per output | |
| 8 | Cello: Kaplan string data, body with a strong ~175 Hz mode (the wolf note should appear by itself, plus a "wolf eliminator"), heavier bow. Then viola/bass | |

## Next

1. **Phase 7: mic placement and room** (the owner asked for it after Phase 3).
   Plan agreed with the owner: 7a directional body and placement; 7b distance
   and the room (power-response feed, player shadowing); 7c close mics (near
   field, bow-noise source, mic types and proximity, presets). The floor
   reflection moved to Phase 8 (cello). Placement controls stay simple (a few knobs, no placement view);
   the default place is the pre-7a sound.
2. **Phase 8: cello** (then viola/bass). Expect: a new `InstrumentDef`
   (Kaplan string data, body, bow), the radiation measured against VSCO
   cello recordings with the same pipeline, `calibrate_force.py` and the
   Schelleng map redone for it, the player habits checked (cellists start
   strokes nearer the string, vibrate differently: player doc §2.1, §3.1).

## Ideas not done yet (with why)

- **From the perceptual distance (Philharmonia, docs/EVALUATION.md), by size:**
  1. Soft attacks: the recordings swell pp–mp notes over 150–190 ms (first
     100 ms 5–7 dB under the sustain); the synth is at full level in ~30 ms.
     A performer habit (bow speed/force ramp by dynamic; the "attack style"
     idea below). Attack window 73 % against 23 % between takes.
  2. Shimmer: the recordings' sustain fluctuates 2.9 dB per band, the
     synth's 1.6, and a wider vibrato barely helps (1.6). Candidates: the
     bow's own unsteadiness (force/speed drift), sharper body peaks.
  3. Vibrato style: this player ~31 cents p-p from the onset; the synth's
     default is VSCO's calmer 16 cents after 0.25 s. Players also finger notes
     that would be open strings when they want vibrato.
  4. The calibration target: VSCO's mics are ~7 dB brighter above 500 Hz than
     Philharmonia's; the synth follows VSCO. Choose (or average) deliberately.

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
