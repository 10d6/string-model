# Evaluation: how to check the model, reference numbers, lessons

How each part of String Model is measured, the numbers it measured at last
(so a change can be compared), and what we learned doing it. Run everything
from the repo root after `cmake --build build` (Release). Python tools take
`RENDER=path/to/stringmodel_render` to measure another build.

## Reference numbers (as of the wrap-up PR, #26: the violin before Phase 8)

| What | Command | Last result |
|---|---|---|
| Render checks | `./build/stringmodel_render --check . --out build/renders` | all pass (includes block-size independence, 25 random patches) |
| Stress | `python3 synthkit/tools/stress.py build/stringmodel_render 300 --tail 5` | 0 of 300 ring on. With the default 3 s tail, passages that hit the pizzicato keyswitch can fail on open strings still decaying at −78 dBFS: natural, not stuck |
| Schelleng map | `./build/string_lab sweep 1 62 0.13 enhanced` (add `LAB_WIDTH=3` for the bow width) | wedge holds Demoucron's pp/mf/ff points, all inside (ff was at the flattened edge); low-force edge higher, less flattening (map in MODEL.md) |
| Attacks (Guettler plane) | `./build/string_lab guettler 1 62 0.1 enhanced` and the grids `1 62 0.06`, `0 55 0.1`, `2 69 0.08`, `3 76 0.1` | over the five grids (210 attacks): 31 perfect, 103 within 50 ms (before the loss shelves 29 / 112; thermal 2003: 0 / 50; classic: 23 / 71) |
| Force window | `python3 tools/calibrate_force.py` (~25 min) | every row straddles 0 (margin ≥ 0.2; B3 ff −0.4..+1.0; the open G at d0 measured with the G pp trim) |
| Radiation residual | `python3 tools/radiation_target.py "<VSCO>/Strings/Solo Violin/Arco Vib" build/stringmodel_render - 3` | target re-measured for the loss shelves; half-octave bands within ±3 dB (MODEL.md). The bands at 1.26, 2, 3.2 and 4 kHz were already off since Phase 3/5 (few notes per band) |
| Body controls | `./build/radiation_lab character` | Air +9 → +6 dB at 250 Hz; Nasality +9 → +7–8 dB at 0.8–1.3 kHz; Brilliance +9 → +6–9 dB at 2–3.2 kHz; Softness 12 → −10 dB above 6 kHz; Voice ±10% moves 400–800 Hz bands 5–7 dB |
| Open strings in tune | `python3 tools/open_strings.py` | D, A, E within ±1.7 cents at every dynamic; G +3.1 / −0.7 / −2.3 at p / mf / ff |
| Harmonics | `python3 tools/harmonics_tuning.py` | within ±3 cents (B6 −1), fundamental 35–68 dB down |
| Harmonic timbre | `python3 tools/harmonic_timbre.py --data <phil wav>` | (synth harmonic − arco) − (real harmonic − arco), partials 2–6: −0.5 / +1.9 / +2.6 / −2.1 / −2.0 dB, mean 1.8 (before the left-hand PR +0.7 / +5.6 / +9.9 / +6.7 / +12.2, 7.0) |
| Portamento | `python3 tools/portamento_glide.py` | glide (10–90%) on the leaps that shift: 0–12 ms at 0, 78–116 at 0.3, 178–292 at 1 (before the left-hand PR none slid at any setting) |
| Intonation Style | `python3 tools/intonation_check.py` | style 1 vs 0: C# +8, F# +6, B +4, G −4; double stops pure (third −14, sixths +14) |
| Vibrato Shaping | `python3 tools/vibrato_extent.py` | shape 0 / 0.7: B4 26 / 20 cents, B6 27 / 32; B4 after a fast run 15 / 14, then 31 / 28 (vibrato default 26 cents) |
| Pizzicato decay | `python3 tools/pizz_decay.py "<VSCO>/Strings/Solo Violin/Pizz"` | score 0.98 (before the loss shelves 0.92–0.93; 1 polarisation: 1.71) |
| Articulation pitch | `python3 tools/note_pitch.py tests/spiccato.json` (any scenario) | spiccato/sautillé within ~10–15 cents; the first note from rest can read flat in an 80 ms window |
| Bow-change gap | `LAB_HF=1 tools/bow_change_gap.sh 69` | A4 mf: ~15 ms more than 6 dB down |
| Directivity | `./build/radiation_lab directivity` | 250/500 Hz omni ±1.5 dB; 4 kHz front +2.2, front-up +4.4, back −2.1, below −2.1; 8 kHz front-up +5.9, back −1.3 dB; semitone-to-semitone directivity index varies 7.1 dB (KB 2.2–8.1). (Since 8.1 the lab uses the synth's own random seed; before, it measured another draw: front-up +6.7, back −7.2, 4.9 dB.) |
| Directivity, cello (first version) | `SM_INSTRUMENT=cello ./build/radiation_lab directivity` | 250 Hz omni; 500 Hz top +4.8, bottom −5.0, front −1.2, back −0.2 (KB: forward, front/back > 10 dB at 400–500 Hz: not yet); 4 kHz front +5.1, back −5.2; semitone-to-semitone index varies 4.8 dB (KB, the cello's class: 1.2–3.2) |
| Distance trim | render a phrase at 1.5–10 m, Room 20/60% | total level within 0.3 dB (0.5 m: +2.4) |
| Room feed | wet (Room 100% − 0%) band levels re 200–1000 Hz | 3.6/7.3/7.0/8 dB darker at 1–2/2–4/4–8/8–16 kHz than the 7a feed |
| Perceptual distance | `python3 tools/perceptual.py --data <phil wav>` (2.5 min) | score 37.8 (attack 40.1, early 34.3, sustain 31.1; #22: 40.2 / 45.0 / 34.4 / 31.1; before the attack swell 53.4 / 72.4 / 40.0 / 32.1); rise 96 ms (recordings 84; per dynamic pp–ff 166/166/174/78/52/34 vs 153/176/201/75/50/34); shimmer 1.7 dB (recordings 2.9); ring 8.8 dB (recordings ~7); yardsticks: another take 20.5, another violin 35.4 |
| Shimmer split | held p/mf/f notes, 1–1.5 s, after 0.35 s, 0.5–8 kHz | band against band ~1.6 dB (recordings 2.55; before the shimmer PR 0.85); level wobble 1.54 (2.29), overtone balance 1.36 (2.39) |
| Default mic vs VSCO | `tools/radiation_target.py <Arco Vib> build/stringmodel_render - 3`, half-octave means | within ±4.1 dB, RMS 2.5 dB (pre-7a 2.3; the largest is now 2.2–3.2 kHz, +4.1) |
| Low end vs VSCO | fundamental (h1) re harmonics 3–8, synth − real, mean of p and f | G3 +2.7, A3 +1.7, C4 −10.7, E4 −1.9 dB (was +17.1, +7.9) |
| Brightness vs dynamics | f − p band levels re 0.5–2 kHz, median G3–E5, vel 45/110 (VSCO Arco Vib) | 2–4 / 4–8 / 8–16 kHz: synth +3.4 / +5.9 / +8.4 (#22: +3.3 / +5.9 / +9.0, before D4/A4/E5 were stopped notes), VSCO +2.5 / +6.4 / +8.0 (`tools/brightness_spread.py`). With the tilt off +1.3 / +2.9 / +3.4: about half from the string itself (before the bow-brightness PR +0.5 / +0.6 / −0.2) |
| Partial decay (Q) | `python3 tools/partial_q.py --data <phil wav>` (plucks, initial 25 dB per partial) | median Q real / synth, stopped: 177/182, 260/266, 304/322, 355/412, 651/699 at 0.15–0.5, 0.5–1, 1–2, 2–4, 4–8 kHz; open 444/450, 444/468, 398/407, 273/478, 1420/1038 (before the loss shelves stopped 148, 128, 120, then too fast to measure) |
| Doubled input | the same fast sautillé passage with every event repeated 2 ms later | renders bit-identical to the single passage (main before the fix: difference 3.3 dB above the signal) |
| Brightness spread | `python3 tools/brightness_spread.py --vsco <Arco Vib> [render …]` | f − p at 2–4 / 4–8 / 8–16 kHz re 0.5–2 kHz, G3–E5: synth +3.4 / +5.9 / +8.4, VSCO +2.5 / +6.4 / +8.0 (same as the Brightness vs dynamics row; this is its tool) |
| String brightness vs force | `python3 tools/force_brightness.py [enhanced classic thermal]` | ×4 force at fixed v_b, β (Helmholtz throughout): G3 +3.0 / +8.8 / +7.7 dB, A4 +0.9 / +2.9 / +9.2 (enhanced, no bow width) |
| Fundamental strength | `python3 tools/fundamental_strength.py --data <phil wav>` | 1st partial re 2–4, synth − real: G3 −2.7, D4 −3.2, A4 −29 (mf/f takes; −15 with mp), E5 −1.3 dB (PLAN Ideas) |
| Key handling | `./build/performer_fuzz [n] [doubled]` | 0 of 140 passages bad, single and doubled input (every articulation) |
| Slurs | `./build/stringmodel_render tests/slurs.json` + a 1 ms-hop pitch track (12 ms windows) | finger changes 2–4 ms (was 10–14), shifts 5–11 ms in a fast scale; onsets 1–2 dB above `main`'s, no clicks |
| Bowing to the meter | `./build/bowing_lab 0.5 92 3 4 < events` (see the file's header) | pickups up-bow from mid-bow, downbeats down-bow, alternation, no mid-note rebows in phrases |
| Browser speed | `node synthkit/web/test/bench-web.cjs build-web/web/stringmodel.html tests/chord.json` (Release build!) | ~7× realtime with the room, ~12× without |
| Browser vs native | CI (`tests/chord.json`, limit −40 dB) | ~−70 dB |

`<VSCO>`: the VSCO-2-CE repository (CC0), fetched with instrument-research's
`tools/fetch_vsco.sh`; for pizzicato, a sparse checkout of
`Strings/Solo Violin/Pizz/`. Never commit the recordings.

## The perceptual distance (Philharmonia recordings)

`tools/perceptual.py`, ported from 10d6/piano (`docs/TESTING.md` there): how far
the synth sounds from a real violin, from a simple ear model (ERB gammatone
bands, Zwicker loudness, a ~1 dB tolerance), without listeners.

**The data.** The Philharmonia Orchestra's violin samples (1502 MP3s, 96 kbit/s
mono; arco-normal on 49 notes G3–G7 at six dynamics pp–ff in 0.25/0.5/1/1.5 s,
plus most articulations). Free to use, including commercially, but **not to be
shared as samples**: never commit them, as with VSCO; only measured numbers go
in the repo. The owner supplies the zip; decode once with ffmpeg to WAV (e.g.
`~/data/phil/wav`, or `--data` / `$PHIL_WAV`). Their top octave is cut at
16–20 kHz by the encoder: the measure stops at 15 kHz.

**How notes are compared.** Each recording sets the synth note's length (its
held part); the dynamics map to velocity pp 20, p 40, mp 60, mf 80, f 100,
ff 120; the synth is mono (L+R), level-matched per note over the held part, at
the default settings (Room 20%). Windows, each aligned on its own signal's onset:
attack 0–80 ms and early 80–250 ms frame by frame; the sustain as each band's
mean (vibrato won't line up frame by frame); the release (0.3 s from where the
level falls 10 dB) frame by frame, reported but **not scored**. Beside the
score: **rise** (ms to within 3 dB of the sustain) and **shimmer** (the
sustain's per-band level fluctuation, dB, 1–8 kHz), which the averaged sustain
can't see.

**Yardsticks** (`--yardstick`; score = mean of attack, early, sustain):

| Pair | score | attack | early | sustain | release |
|---|---|---|---|---|---|
| another take (the 0.5 s vs the 1 s recording, same note and dynamic) | 20.5 | 23.4 | 19.3 | 10.4 | 52.6 |
| one dynamic step up | 26.5 | 34.2 | 24.3 | 10.9 | 53.9 |
| another violin and studio (VSCO vs Philharmonia, 26 notes p/f) | 35.4 | 42.5 | 35.1 | 25.5 | 85.9 |

The release differs by ~50 % between two takes (a player ends a 0.5 s and a
1 s note differently), so it can't tell a fault from a habit.

**Validation.**
- The low-notes PR (#15), G3–C4: after it, 37 of 58 notes nearer (median −1.1
  points); the low-band errors it fixed shrink (early +6.6 → +0.3, release
  +14.4 → +2.5). ✓
- Anchor, the Pickup output (no body): 53.0 against the Modelled 53.2; on the
  26 VSCO notes 53.9 against 49.8. **A blind spot:** the averaged sustain
  (Pickup 25.1, Modelled 29.0, another violin 25.5) can't hear the body. The
  ERB bands are wider than the body's fine peaks (the piano found the same for
  its body's ring). Judge body changes with `radiation_lab` and by ear.
- Calibration: VSCO is brighter than Philharmonia in the sustain (+6.9 mid,
  +6.8 high); the synth (calibrated on VSCO) is off by about the same
  (+7.8, +6.0). Much of the sustain distance is the two libraries' mics.

**Baseline (Phase 7b main, 448 notes, 0.5 and 1 s):** score 53.4 (attack 72.4,
early 40.0, sustain 32.1; release 74.3; 7a: 53.2, the same within noise). Rise: recordings 84 ms, synth 26 ms
(median); by dynamic, pp–mp 153–186 ms against 31–34 ms (the player swells
soft notes: their first 100 ms peak 5–7 dB under the sustain; the synth's is
+0.5 to +1.6 dB over), mf–ff 34–65 ms against 20–28. Shimmer: recordings
2.9 dB, synth 1.6 (a single dry mic 1.3; with the Philharmonia player's 31-cent
vibrato from the onset 1.6; the Pickup 1.9). Vibrato: this player ~31 cents
p-p (5–95 %), from the onset; the synth 17 (VSCO's calmer player), after
0.25 s, none on open strings. Signed: the attack is too loud in the mids
(+41) and highs (+15) for its sustain.

## Techniques

- **Measure through the synth, not only the labs.** The harmonics were in
  tune in a lab with fixed bow settings but 40 cents flat in the synth,
  because the performer's force and β were wrong for a touched string. Labs
  isolate; the render is the truth.
- **A/B against the base build.** Build `origin/main` in a worktree and render
  the same scenario with both:
  ```
  git worktree add -f /tmp/base origin/main && cd /tmp/base && rm -rf synthkit \
    && ln -s ~/string-model/synthkit synthkit && cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release && cmake --build build --target stringmodel_render
  ```
  `cmp` of the two WAVs proves a "default unchanged" claim (body controls).
  Move the base to the current `main` before each comparison (`git -C
  /tmp/base checkout -f <sha> && git -C /tmp/base submodule update`, then
  rebuild): a stale base once mixed an older PR's change into an A/B.
  `perceptual.py --compare base.json new.json` then shows where a change
  lands, per dynamic (`--by-note` per note name).
- **Long runs:** `calibrate_force.py` takes ~25 min and an environment
  restart kills foreground and background jobs alike. Run it with `nohup …
  > file &` on a frozen copy of the lab (`cp build/instrument_lab /tmp/…`,
  so rebuilding doesn't change it mid-run) and poll the file.
- **Separate the mechanism from the amount.** When a change has a side
  effect, apply the new mechanism with the old numbers. Example: in-tune open
  strings came out 1–2 dB quieter. With the new mechanism and the old amounts
  the level matched `main`; with body coupling off both matched. So the cause
  was coincident partials of in-tune open strings interacting through the
  bridge, a consistent physical result (scenario floor adjusted, with a note).
- **Bisect through scenario parameters.** A scenario's `params` apply to both
  the browser and the native render, so switching features off one at a time
  in the scenario finds which one moves a browser/native difference.
- **Pitch measurement.** Median of zero-padded FFT peaks near harmonics 1–3
  (`tools/evalkit.py`). Avoid windows with an automatic bow change: it shows
  as a 20 ms, +35 cent blip (turn `bow.auto` off for pitch tests). Open
  strings get no vibrato: test vibrato on stopped notes. Since the left-hand
  PR, slow D4/A4/E5 are stopped on the string below by default: tests of
  open strings set `finger.avoidOpen` 0 (`open_strings.py` does; the labs
  turn it off unless `LAB_AVOIDOPEN=1`).
- **Level per region:** long-term spectra (Welch) per band, against a
  reference render.
- **Recordings as ground truth, with care.** The VSCO pizzicato tails decay
  at a near-uniform 25–35 dB/s: that's partly their room, so the synth is
  measured with its own room on. Fit with a single score over several
  measures and notes, not one number.
- **Before/after clips for the owner.** Render the same passage both ways,
  convert with `ffmpeg -b:a 192k`, and send both; say what to listen for.
  The owner judges by ear on the PR's Pages preview.

## Lessons

- **Bowing is chaotic.** Last-digit differences grow on long or unstable
  passages:
  - browser vs native, whose maths libraries differ (`pow`, `exp`, `sin`):
    an 8 s held note ends −9 dB apart; the legato scale drifted from −71 to
    −34 dB as unrelated habits moved its slurred notes. CI compares
    `tests/chord.json`, noted in `ci.yml`;
  - host block size, before parameters were smoothed on the performer's own
    clock (`smoothing(0)`);
  - any A/B: judge over several notes, strings and dynamics (medians, grids),
    not one note; keep margins on test expectations.
- **Model what players do, not what they say** (player doc §4). The
  "crescendo shouldn't speed up the bow" rule is pedagogy; Demoucron measured
  bow speed rising 10 → 50 cm/s, so it wasn't adopted. Likewise brightness
  near the bridge comes from the extra force, not the position.
- **When a paper's constants aren't available, tune by a measurable
  criterion and tag it [T].** The enhanced friction's h(u) was chosen by
  attack quality on Guettler grids, then checked against the Schelleng map
  and the force calibration.
- **Watch what a rule does to the next note.** The first "bowing to the
  meter" re-took pickups to the tip and forced the downbeat down-bow with 20%
  of the bow left: it ran out mid-note and rebowed (an extra note). A
  harness that logs mid-note reversals (`bowing_lab`) catches this.
- **Allowances should apply only where they're true.** The open-string
  flattening allowance applied to every open string, so it had to stay at 60%
  to keep unbowed strings ringing in sympathy. Applied only while bowed, it
  can be full.
- **Benchmark Release builds.** The CLAUDE.md browser command once had no
  build type: the local page was -O0, and its "1.8× realtime" was wrong
  (CI was optimised all along).
- **Recalibrate after spectral changes.** The enhanced friction darkened the
  string 2–4 dB above 4.5 kHz; re-running the radiation pipeline
  (`radiation_target.py` → `radiation_measured.csv` → `combine_target.py` →
  `gen_radiation.py`, MODEL.md "Output") restored the residual.
- **Plugins:** Linux pluginval once segfaulted in "Fuzz parameters"; it
  didn't reproduce locally (CI's seed, six others, and an ASan/UBSan build
  were all clean) and passed on re-run. Build sanitizer plugins with `-j 2`
  (the compiler runs out of memory otherwise).
- **"Buzzy" can mean the wrong thing is loud, not that there's noise.** The
  low notes sounded like too much bow noise, but their noise between
  harmonics was *below* the recordings'. Two spectral faults did it: a G3
  fundamental 17 dB too strong under a sawtooth (synth-like), and a top
  octave that didn't darken at p. Compare per harmonic and per band against
  the VSCO notes, p and f separately, before reaching for the noise knobs.
  And check a filter in place: the radiation's own fit hid how much its
  band-pass skirts leaked below A0 until `radiation_lab` was read relative
  to the 250 Hz band.
- **Trust what the owner hears, then find it in the data.** "A ring like
  beating" in the vibrato turned out to be a steady line at each partial's
  centre pitch, visible at once in a zoomed long-term spectrum and twice as
  prominent as in the recordings; measuring the recordings' vibrato showed
  the cause (their centre wanders). The perceptual score couldn't see it
  (the sustain is averaged): the tool now reports it as ring.
- **Matching a number isn't matching the sound.** The first Bow Wander
  matched the recordings' level wobble (2.28 against 2.26 dB) and improved
  the perceptual score by 2 points, and the owner heard an unsteady player:
  the recordings' wobble is mostly their player shaping notes and vibrato
  through the body, not the bow sagging. Ask what a number is made of before
  fitting to it, and let the owner's ear overrule the score.
- **Split a measure before trusting it.** The synth's sustain shimmered half
  as much as the recordings'. Splitting it showed the bigger gap was bands
  moving against each other (vibrato through the body's peaks), not the whole
  note's level (the bow), and that it wasn't noise between harmonics either:
  the harmonic bands shimmer more. A sharper body did most of it; the bow
  wander the rest. The common wobble only matched once the wander's spread
  was doubled: a 0.6–1 s window hides some of a 0.2 s-correlated drift.
- **Test the whole range.** The stability check stopped at E6; the vibrato
  tool's B6 caught the wander breaking high notes. (Notes above ~C7 already
  fail the slip count without it.)
- **The swell had to pass the physics check as well as the distance.** It
  lowered the score at once, but counting slips per period through the attack
  (a C++ harness on `Instrument`) showed pp notes on the G string turning to
  multiple slipping 100–200 ms in (far from the bridge, light force). Lifting
  the soft-playing force by the momentary dynamic cured it. Check new gestures
  on the string, not only on the recordings.
- **A new rule can open an old path.** Slur Range made "back to the key still
  held" take a new bow when it was a wide leap; in pizzicato that started a
  bowed stroke that never lifted (the stress test caught it at −40 dBFS 5 s
  after release). `main` had the same bug with Legato Slurs off. Run the
  stress test after any change to the key logic, and find the cause with a
  replay of its passage into the model (`#define private public`, print the
  performer's state).
- **CodeQL** flags `float * float` converted to `double` in the labs: cast
  one operand first (`static_cast<double>(a) * b`).
- **Losses per pass are losses per note.** A filter at a termination takes
  the same fraction on every pass, and a high note makes more passes a
  second: the stopped notes' treble died in milliseconds instead of ringing
  ~0.5 s. Specify damping as decay rates per second by frequency and design
  each note's filters from them (`LossShelf`); `partial_q.py` shows the
  difference at once. A "small" top low-pass brought the problem straight
  back.
- **Less damping raises the minimum bow force** (Schelleng): after the loss
  shelves, ff on the G string double-slipped at the old 3 N cap. Re-run
  `calibrate_force.py` after any loss change and expect to move the caps and
  lifts.
- **Before adding an effect, check whether the physics already does it and
  something cancels it.** The string brightened with force after the loss
  shelves, but the assistant's long-note rule (slow the bow, creep to the
  bridge) evened p and f out; `force_brightness.py` (string alone) against
  `brightness_spread.py` (whole synth) found it. Fixing the rule, from
  Demoucron's data, let the output tilt shrink from 8 to 5 dB.
- **When the physics is too fragile, fake it outside the loop and say so.**
  Harmonics need a very light bow; the finger pad's real damping, modelled
  in the string, stopped them speaking. A low-pass on the output while a
  string is touched matches the recordings (harmonic_timbre.py 7.0 → 1.8 dB)
  without touching the bowing. Same reasoning as the brightness tilt.
- **Compare differences when the references differ.** Philharmonia's mics
  are much darker than VSCO's, which the radiation follows, so "synth vs
  Philharmonia" made every note look too bright. "Harmonic minus ordinary
  note", real against synth, cancels mics and body.
- **Check that a control does something.** Portamento measured 0–12 ms
  glides at every setting (string choice never allowed a shift); harmonics
  were at the right pitch with an ordinary note's timbre. A sweep of the
  control through its range, measuring what it should change
  (`portamento_glide.py`), is cheap and should exist for every habit.
- **The perceptual score has a floor that isn't zero.** At ~38 the synth is
  as far from the Philharmonia notes as another violin and studio (35.4).
  Below that, lowering it fits that player, instrument and room; pick work
  the owner can hear instead (docs/PLAN.md).
- **Robust to MIDI quirks.** A keyboard the browser saw as two inputs sent
  every key twice; the synth restarted sautillé strokes on the repeat. A
  real key can't go down twice, so the performer ignores a held key's
  repeat within 30 ms; `performer_fuzz doubled` keeps it so.
