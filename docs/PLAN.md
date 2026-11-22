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
  - keyswitches for articulations come with Phase 5.
- **The whole instrument runs in `processGlobal`**, so the strings can share
  a bridge. The kit's voices only track keys.
- `model/` is plain C++20 with no synthkit dependency, so it can be tested
  offline (`tools/string_lab`, `tools/instrument_lab`).

## Phases

| # | What | Status |
|---|---|---|
| 0 | Scaffold from the synthkit template, CI, Pages | done |
| 1 | One bowed string you can hear: waveguide + thermal friction (+ classic for A/B), torsion, gesture layer with playability assistant, slurs/détaché/bow changes, vibrato, intonation, string choice, library-mic output, room | done (#1) |
| 2 | Four strings on a shared bridge: modal bridge admittance Y(z) solved exactly with the strings, sympathetic ringing, fingers lifting off unused strings, double stops and rolled chords, force calibration against the coupled instrument, string DC-loss fix | **this PR** |
| 3 | Body and output (next): modelled radiativity (low modes + statistical high band × bridge hill, decorrelated L/R), mute, keep the library-mic option, pickup | |
| 4 | Gesture polish: release types (lift / stop on string / left-hand release), portamento styles, bow-speed budget on long notes, string keyswitches/CC | |
| 5 | Articulations: martelé, spiccato/sautillé (Demoucron's bow oscillator), tremolo, pizzicato (two polarisations), harmonics (touch junction), keyswitches | |
| 6 | Refinement: enhanced rosin model for attacks (Woodhouse–Galluzzo 2025), finite-width bow noise, CPU work, a VST3 milestone build for Ableton | |
| 7 | Cello: Kaplan string data, body with a strong ~175 Hz mode (the wolf note should appear by itself, plus a "wolf eliminator"), heavier bow. Then viola/bass | |

## How each phase is checked

- synthkit's checks on every PR: render `--check`, the stress test,
  browser-vs-native, plugin builds.
- Model checks:
  - `string_lab sweep` (a Schelleng wedge; Demoucron's pp/mf/ff points inside)
  - pitch per note within a few cents on stopped notes (`tools/analyze.py`)
  - attack transients within the KB's limits (≤ 90 ms multiple slipping,
    ≤ 50 ms creak)
  - spectral comparison against the VSCO-measured response
- Listening on the PR's Pages preview.
