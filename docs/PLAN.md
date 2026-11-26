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
| 6e | Slurs: the left hand's position (finger changes in 2–3 ms with a tap or pluck; B-type shifts on a minimum-jerk curve), Slur Range; Auto Bow Change off by default; info text for every control | done (#14) |
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
| — | Wrap-up before the cello: docs, reference numbers, the measurement scripts as tools (brightness spread, string brightness vs force, fundamental strength, perceptual breakdown, performer fuzz), the Phase 8 checklist | done (#26) |
| — | Portamento (the owner: "sounds very synthy"): slides speed into the note (Yang & Chew: violinists' inflection after the midpoint), the vibrato pauses through a shift, the bow eases off 3 dB, and a portamento glides into the note (the new finger takes over part-way) instead of stopping a third short; A/B'd by ear one change at a time, all four kept | done (#33) |
| — | Legato on one string (the owner: an artificial attack, too much high end): a landing finger is a touch that firms up over 25 ms (a lift-off frees the string over 20), the string re-laid out around it; measured first, ruled out by measurement or by ear: a high-frequency burst, rough slipping, the tap, a dip at the change, velocity within slurs, the bow's place; a 30 ms pitch glide also cured it but swept on leaps | done (#34) |
| — | The cello's attacks and sustains measured by register against the Philharmonia cello and bigcat (`tools/attack_sustain.py`, EVALUATION.md): the attack opens like a filter (treble 170–180 ms late on the low strings) and is too clean, the held tone too clean, the C string's fundamental strong and its stopped notes flat, the top slow; the junction's sampling ruled out (192 kHz changes nothing). No change to the sound | done (#35) |
| — | Recorded onsets on the cello (Bow Catch): each stroke adds bigcat's onset (CC0) with the note taken out, equalised into bridge force, following the synth's own build-up (the recording's noise-to-tone ratio); the C string's treble arrives 141 → 56 ms late (Philharmonia, soft), C3–B3 58 → 10; then the cello's swell made shallower (×0.35/0.75, by ear): the treble's lag gone on every string; the violin unchanged | done (#36) |
| — | The C string: its stopped notes in tune (the finger corrects the bow's flattening, +8.5 cents while bowed; 6-12 cents flat → within ±2.5, F#2 still wolf-pulled from mf; the G string left for the wolf) and its fundamentals 6 dB lower below 85 Hz (against Iowa +5.6 → +2.9 dB; D#2-F#2 don't follow: the A0 mode's skirt) | done (#37) |
| — | The held tone's noise (Bow Noise): the measured missing part (125 Hz-2 kHz, 10-25 dB) added as octave-band noise in bursts at each slip, re the note's own level; against Iowa and bigcat −10.3 → −0.1 dB median | this PR |
| 7c | On hold (the owner's choice, after 7b): close mics: the near field (equivalent sources) and the bow-noise source, mic types with proximity effect, presets. (Swapped with the old 7b: at 1.5 m a cardioid's proximity boost starts ~20 Hz, so mic types only matter close.) | |
| 7 | Mic placement and room: close / studio / overhead / room positions (KB §12.3–12.4), near-field sources and bow noise for close mics, the room fed by the body's power response (darker, smoother), the room's level per output | |
| 8 | Cello: Kaplan string data, body with a strong ~175 Hz mode (the wolf note should appear by itself, plus a "wolf eliminator"), heavier bow. Then viola/bass. Seven PRs, below ("Phase 8: the cello") | in progress |
| 8.1 | Plumbing: `InstrumentDef` with the instrument's body shape and hand; both instruments prepared, a real-time-safe Instrument switch (fade, ring-out); per-instrument library-mic IR (the cello section's, CC0), radiation shape, body-control centres, room power; a first cello from the KB; labs/tools take the instrument; the violin bit-identical | done (#27) |
| 8.2 | Strings and the bowing window: losses fitted to the Iowa pizzicato; the stick test (KB §7.3) in `instrument_lab` and `calibrate_force.py`; checks against the published cello data (Galluzzo's D window, flattening, ALFs); the measured cello rosin tried and not kept; force ∝ Z₀, per-string contact limits from the coupled windows, a slower pp bow; the wolf on F3 and a **Wolf Eliminator**; open strings; `iowa.py`, `wolf_check.py`, `window_map.py`, `schelleng_check.py`; the violin bit-identical | done (#28) |
| 8.3 | Body and wolf: the measured cello modes (A0 100, T1 140, the wolf mode 185, 209, 281, 433 Hz; hill 1.35 kHz); **per-string mode strengths** (a 4×4 bridge solve; the violin keeps the scalar one); `wolf_check.py` with the six acceptance tests (five pass); a gentler body-force term so the G string's wolf shows; losses, playability, open strings rechecked; the violin bit-identical | done (#29) |
| 8.4 | Pizzicato: `pizz_decay.py --iowa` measures the cello against the Iowa pizzicato; the violin's second-polarisation values kept (the best compromise with the per-partial losses); no change to the sound; a Windows VST3 milestone build from this branch (**this PR**) | |

## Next

1. **Phase 8: the cello**, PR by PR (below). Then viola and bass.
2. Phase 7c (close mics: near field, bow-noise source, mic types and
   proximity, presets) when the owner asks for it.

## Phase 8: the cello

### The recordings

**VSCO-2-CE has no solo cello**, only a cello section (`Strings/Cello
Section/`: susvib, pizzT, spic, trem). A section's chorus and distant mics
make it a poor reference for losses, vibrato or brightness, so the solo
sources are (all outside the repo, never committed):

| Set | What | Licence | Use |
|---|---|---|---|
| University of Iowa MIS, cello (2012 edit), mono | anechoic, close mic; arco pp/mf/ff and pizzicato pp/mf/ff, every string, chromatic runs in one file per string/range | "without any restrictions" | fitting (losses, pizzicato, radiation's upper band) |
| Philharmonia cello (the owner's zip) | 890 MP3s: arco-normal pp/mp/p/f/ff with lengths, harmonics, col legno, trills, spiccato, staccato, tremolo; only 2 pizzicato | free to use, not to share as samples; KB tier C ("unclear for fitted artefacts") | listening and measuring only (the perceptual distance, attacks, vibrato), never fitted |
| VSCO-2-CE cello section | susvib, pizzT, spic, trem | CC0 | the library-mic IR and the first radiation target (KB §12.7); tremolo rates |

Decoded to WAV in `~/data/iowa/cello/wav/{arco,pizz}` and
`~/data/phil-cello/wav` (the Iowa files hold a run of notes each:
`tools/iowa.py` finds them).

The cello's research is instrument-research's
`docs/cello_modelling_data.md` (Nov 2026): measured body modes, rosin,
Galluzzo's D-string window, flattening and ALFs, wolf acceptance tests,
cellists' bowing. **The mdw Vienna datasets (CC BY, Zenodo) are on hold** at
the owner's request (licence questions): the checks below use the published
numbers instead, and nothing is fitted to those datasets.

### The PRs

1. **Plumbing** (8.1, done): the violin renders bit-identical to before.
2. **Strings and the bowing window** (8.2, PR #28; MODEL.md "The cello",
   EVALUATION.md). Revised after the cello research:
   - Losses fitted to the Iowa pizzicato (1,400 partials).
   - The stick test (stick fraction, slips per period, jitter; KB §7.3) is
     the regime test in `instrument_lab` and `calibrate_force.py`. A
     periodicity test of the synth's output (`playability.py`, removed)
     passed notes that slipped an extra time every few periods.
   - Checked against the published cello data: Galluzzo's D window (the
     model's centre within 6%, 2.4x too narrow), flattening and ALFs (as
     measured). The measured cello rosin (mu_s 0.65) put the window far too
     high in this friction model, so the cello keeps the violin's rosin.
   - Force ∝ Z₀ (supported: Beigi & Conneely's F_min ~ Z^1). The contact
     point stays between per-string limits from the coupled windows (a
     floor rising with bow speed, a ceiling where windows break up); a 5
     cm/s pp bow on the D and A. My first law (compressed toward the
     bridge) sent the C's ff the wrong way.
   - Dynamic range vs the Iowa arco: D/A/G within ~3 dB; the C 4 dB vs 13,
     the model's main cello limit (its narrow windows).
   - The wolf (175 Hz, 0.13 s/kg), the Wolf Eliminator, open strings.
3. **Body and wolf** (8.3, done; MODEL.md "The cello"). What it found:
   - The measured modes went in (A0 100, T1 140, the wolf mode 185 Hz at
     0.17 s/kg, 209, 281, 433 Hz; the hill at 1.35 kHz). The cello doc's
     dense band from ~400 Hz ([D]) didn't: it made the open strings'
     partials at 500 Hz-1 kHz decay twice as fast as the Iowa pizzicato's,
     so the band starts at 800 Hz as before.
   - Per-string strengths for the wolf and 209 Hz modes: a 4×4 bridge
     solve (`Bridge::solveWeighted`), checked against the scalar one with
     unit weights (-111 dB). The cello runs at 10x real time natively
     (14x), 5.9x in the browser (6.6x).
   - The wolf passes five of the six acceptance tests; the worst note does
     not rise as the bow nears the bridge (test 3). Bowing pinned from the
     first sample locks the wolf note into multiple slipping; pinned after
     the performer's attack it beats at 5-6 Hz (measured 4-14).
   - The assistant's body-force term pressed 2.8x harder on the wolf note
     and hid the G string's wolf (a cellist leaning into it); at half the
     sensitivity the G wolfs at mf and the C at every dynamic, and the
     calibration improved (mean margin 0.62, every cell clean).
   - Not done: tailpiece modes (optional, cello doc §3.3).
4. **Pizzicato** (8.4, done). `pizz_decay.py --iowa`: with the violin's
   second-polarisation values the cello scores 0.69 against the Iowa
   pizzicato (mf/ff, 122 notes; the violin vs VSCO 0.98): the first stage
   slower than the recordings' (-42 vs -68 dB/s), the second faster (-22 vs
   -15). A weaker, longer second polarisation fits the two stages better
   (0.64) but pulls the per-partial decays off the recordings (1-2 kHz Q 181
   vs 293): the 8.2 loss fit leans on the second polarisation. Kept the
   violin's values; a joint refit (losses, second polarisation, the pluck's
   shape) goes with 8.6. The Iowa pp notes barely decay (the recordings'
   noise floor): fitted to mf/ff only. The Philharmonia's cello pizzicato is
   two C2 phrases: listening only.
5. **Radiation and output** (8.5, done; MODEL.md "The cello"). A solo,
   anechoic library-mic response fitted from the Iowa arco
   (`body_response.py` on all 24 runs, tracked frame by frame: no need to
   segment; split-half fine detail r 0.86) replaces the cello section's for
   the Library Mic outputs (mono: the recordings are) and the target's low
   band; the upper band measured against the Iowa arco
   (`radiation_target.py --iowa`), the low band corrected by the Modelled
   output's own residual (`combine_target.py --residual`): every half
   octave 125 Hz-9 kHz within ±3 dB. Directivity: omni below ~300 Hz, the
   cellist's torso as a shelf on each mic (front/back 10 dB in the 500 Hz
   octave, 17 at 1 kHz), the beam from 300 Hz. Not reached: the 500 Hz
   octave leans down rather than forward (one mode's lobe), and the
   maximum directivity's spread across notes is 3.7-5 dB (cello class
   1.2-3.2; more smoothing narrows the stereo). `room_power.py`, four cello
   presets (Old & Open, New & Tight, Close Mic, Wolf Tamed; `"instrument":
   1`). Not done (optional): the stage floor through the endpin (§3.4).
6. **The cellist's hands and attacks** (8.6, done; MODEL.md "The cello").
   Attacks from the air land with twice the note's force, settling over
   50 ms, ±20% per note (Galluzzo's ~4x the sustaining minimum): judged in
   periods with `instrument_lab attacks`, median 21 → 13, within 10 periods
   3 → 83 of 240. The attack swell gets a cello table fitted to the Iowa
   arco's onsets (cellists start mf-f much softer). Thumb position from the
   octave. Not done, on purpose: slow-start stiction (the performer never
   starts that slowly), a cello vibrato rate (the literature's rates are the
   violin's), bow mass (nowhere in the model), cello bounce strokes (the
   templates have no gravity to lose), Intonation Style tables (the logic
   is generic). The joint refit of losses, second polarisation and pluck:
   tried (vertical 0.5 at 4x T60, losses 0.85x-1.3x): the second stage
   then matches (−14 vs −15 dB/s) but the first stays too slow (−47 to −51
   vs −68 dB/s) and the per-partial Q bands move off the recordings; a
   faster first stage needs more low-frequency damping at the bridge, which
   would mean redoing the bowing calibration. Kept as it was.
7. **Evaluation and wrap-up.** `open_strings`, `harmonics_tuning`,
   `brightness_spread`, `fundamental_strength`, `perceptual.py` against the
   Philharmonia cello (measuring only; yardstick Iowa vs Philharmonia);
   cello reference numbers; presets and info text; a milestone plugin
   build.

**After 8.6 (the owner: soft attacks, the top and bottom's sustains):** measured first
(EVALUATION.md "Attacks and sustains by register"). Candidates, beyond the physics too, in
order: ~~a full-band onset under the swell~~ (done: recorded onsets; what's left on G2–B2 is the model's
own harmonics darkening through the swell) (the residual of bigcat's recorded onsets, CC0,
harmonics removed, added as bridge force so it goes through the body, mics and room; or a
modelled bow-noise source); held-tone noise by string and dynamic (Rosin Noise is the wrong
shape); the C string's fundamentals and stopped pitch; a pitch-tracking spectral correction
from the recordings' source × body (10d6/strings) if the extremes still fall short. The
Philharmonia recordings are for measuring only; nothing from them ships.

Later, beyond Phase 8: the C string's narrow windows and dynamic range
(friction-model work: the mdw data would be the test if its use is
cleared); string-type presets (four measured G strings, a steel G; cello
doc §2.1-2.2); a rosin control; a "new strings" preset; Mansour's
body-aware minimum force in the assistant (cello doc §4.2).

### Decisions

- **Switching instruments stops the notes playing** (the owner left it to
  Claude): the old instrument fades over 30 ms and its body rings out; a
  switch mid-fade waits for it. No allocation: every instrument is prepared
  in `prepare`.
- **Presets are whole instruments.** A patch stores only what differs from
  the defaults, so a preset without `instrument` is a violin preset: the four
  body presets stay violin presets, and the cello gets its own (PR 4). The
  body controls themselves are offsets that work on either instrument.
- **The wolf is on by default** (Wolf Eliminator 0), as the body makes it:
  F3 on the G string won't speak at pp, and beats at mf-ff (worse on the C).
  The owner may prefer an eliminator fitted by default, as most cellists
  have; ~0.5 cures the G string, 1 the C.
- **Bridge Hill** stays the violin's frequency (its range and default are
  released); other instruments scale it to their own hill (cello ×0.62).
- **String** and its keyswitches name strings lowest to highest
  ("G / C" … "E / A"). The keyswitches (C1–B1) sit just below the cello's
  C2; the bass will need them moved.

### Violin-specific constants still hard-coded

(Move them into `InstrumentDef` when the cello needs a different value, as
`ffLift`, the hand and the radiation shape were.) Already per instrument:
the hand's reach and shift landing (`HandDef`), chord span, vibrato's
register centre; the radiation's size, monopole band, treble beam and body
controls' centres (`RadiationShape`); library-mic IR, room power.

| Where | What | Violin fit |
|---|---|---|
| `Performer::computeTargets` | the dynamics law: v_b/β = 0.6 e^{3d}, v_b = 0.1 e^{1.4d}; the soft-playing margin 0.1 + 0.8 (1 − d)² | Demoucron, violin |
| `Performer` | `kLongVb` 0.22 m/s (long-note bow), sustain law | Demoucron's half notes |
| `Performer::swellDepth/Length` | the attack swell table per dynamic | Philharmonia violin rise times |
| `Performer::chooseString` | slurs stay within a fifth; Avoid Open Strings stops the note 7 semitones up the string below; Portamento keeps the string up to an octave | violin fingering (a cellist reaches a fifth up the string with a shift to fourth position) |
| `Performer` | spiccato/sautillé β 0.18 − 0.1 d; harmonics force ×0.3/0.15, `kTouchR`; vibrato depth 26 cents | Demoucron / violin players |
| `Performer` | open-string allowance falls to 70% at ff; Intonation Style tables | violin |
| `Instrument` | `kTiltDb` 5 (brightness tilt, its drive 5.23 (d − 0.557) + 1.5 p + 2.09 c), `kHarmLP` 3 | VSCO violin p/f; Philharmonia harmonics |
| `Radiation` | the head and torso shadows, the floor cut, the beam's strength | the violinist's posture |

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
