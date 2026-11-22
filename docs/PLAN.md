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
  - keyswitches choose the string (C1–E1) and the articulation (F1–A#1).
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
| 6 | Refinement (**this PR**): enhanced thermal friction for attacks (Woodhouse–Galluzzo 2025 structure, now the default), two-point bow width, pizzicato's second polarisation (fitted to recordings), sautillé, radiation re-measured, CPU (radiation as arrays, WebAssembly SIMD), a VST3 milestone build (CI run by hand) | |
| 7 | Mic placement and room: close / studio / overhead / room positions (KB §12.3–12.4), near-field sources and bow noise for close mics, the room fed by the body's power response (darker, smoother), the room's level per output | |
| 8 | Cello: Kaplan string data, body with a strong ~175 Hz mode (the wolf note should appear by itself, plus a "wolf eliminator"), heavier bow. Then viola/bass | |

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
