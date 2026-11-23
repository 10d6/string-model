# Evaluation: how to check the model, reference numbers, lessons

How each part of String Model is measured, the numbers it measured at last
(so a change can be compared), and what we learned doing it. Run everything
from the repo root after `cmake --build build` (Release). Python tools take
`RENDER=path/to/stringmodel_render` to measure another build.

## Reference numbers (as of Phase 7a, after PRs #10–#15)

| What | Command | Last result |
|---|---|---|
| Render checks | `./build/stringmodel_render --check . --out build/renders` | all pass (includes block-size independence, 25 random patches) |
| Stress | `python3 synthkit/tools/stress.py build/stringmodel_render 300 --tail 5` | 0 of 300 ring on. With the default 3 s tail, passages that hit the pizzicato keyswitch can fail on open strings still decaying at −78 dBFS: natural, not stuck |
| Schelleng map | `./build/string_lab sweep 1 62 0.13 enhanced` (add `LAB_WIDTH=3` for the bow width) | wedge holds Demoucron's pp/mf/ff points (map in MODEL.md) |
| Attacks (Guettler plane) | `./build/string_lab guettler 1 62 0.1 enhanced` and the grids `1 62 0.06`, `0 55 0.1`, `2 69 0.08`, `3 76 0.1` | over the five grids (210 attacks): 29 perfect, 112 within 50 ms (thermal 2003: 0 / 50; classic: 23 / 71) |
| Force window | `python3 tools/calibrate_force.py` (~25 min) | every row straddles 0 (margin ≥ 0.2) |
| Radiation residual | `python3 tools/radiation_target.py "<VSCO>/Strings/Solo Violin/Arco Vib" build/stringmodel_render - 3` | matches Phase 5 within ~1 dB per band. The bands at 1.26, 2, 3.2 and 4 kHz were already off since Phase 3/5 (few notes per band) |
| Body controls | `./build/radiation_lab character` | Air +9 → +6 dB at 250 Hz; Nasality +9 → +7–8 dB at 0.8–1.3 kHz; Brilliance +9 → +6–9 dB at 2–3.2 kHz; Softness 12 → −10 dB above 6 kHz; Voice ±10% moves 400–800 Hz bands 5–7 dB |
| Open strings in tune | `python3 tools/open_strings.py` | within ±1 cent at mf, ±3.9 from pp to ff (E at ff; −3.4 with `bow.auto` on, as before: its bow change fell in the window) |
| Harmonics | `python3 tools/harmonics_tuning.py` | within ±3 cents (B6 −5), fundamental 30–65 dB down |
| Intonation Style | `python3 tools/intonation_check.py` | style 1 vs 0: C# +8, F# +6, B +4, G −4; double stops pure (third −14, sixths +14) |
| Vibrato Shaping | `python3 tools/vibrato_extent.py` | B6 wider (20 → 22 cents); B4 after a fast run 5 cents, full (18) within ~1 s |
| Pizzicato decay | `python3 tools/pizz_decay.py "<VSCO>/Strings/Solo Violin/Pizz"` | score 0.93 (1 polarisation: 1.71) |
| Articulation pitch | `python3 tools/note_pitch.py tests/spiccato.json` (any scenario) | spiccato/sautillé within ~10–15 cents; the first note from rest can read flat in an 80 ms window |
| Bow-change gap | `LAB_HF=1 tools/bow_change_gap.sh 69` | A4 mf: ~15 ms more than 6 dB down |
| Directivity | `./build/radiation_lab directivity` | 250/500 Hz omni ±1 dB; 4 kHz front +2, back −3.2, below −3.5; 8 kHz front-up +6.7, back −7.2 dB; semitone-to-semitone directivity index varies 4.9 dB (KB 2.2–8.1) |
| Default mic vs VSCO | `tools/radiation_target.py <Arco Vib> build/stringmodel_render - 3`, half-octave means | within ±4.1 dB, RMS 2.5 dB (pre-7a 2.3; the largest is now 2.2–3.2 kHz, +4.1) |
| Low end vs VSCO | fundamental (h1) re harmonics 3–8, synth − real, mean of p and f | G3 +2.7, A3 +1.7, C4 −10.7, E4 −1.9 dB (was +17.1, +7.9) |
| Brightness vs dynamics | f − p band levels above 4 kHz, synth − real, median G3–E5 | 4–8 kHz −2.1, 8–16 kHz −0.4 dB (was −7.3, −8.0) |
| Slurs | `./build/stringmodel_render tests/slurs.json` + a 1 ms-hop pitch track (12 ms windows) | finger changes 2–4 ms (was 10–14), shifts 5–11 ms in a fast scale; onsets 1–2 dB above `main`'s, no clicks |
| Bowing to the meter | `./build/bowing_lab 0.5 92 3 4 < events` (see the file's header) | pickups up-bow from mid-bow, downbeats down-bow, alternation, no mid-note rebows in phrases |
| Browser speed | `node synthkit/web/test/bench-web.cjs build-web/web/stringmodel.html tests/chord.json` (Release build!) | ~7× realtime with the room, ~12× without |
| Browser vs native | CI (`tests/chord.json`, limit −40 dB) | ~−70 dB |

`<VSCO>`: the VSCO-2-CE repository (CC0), fetched with instrument-research's
`tools/fetch_vsco.sh`; for pizzicato, a sparse checkout of
`Strings/Solo Violin/Pizz/`. Never commit the recordings.

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
  strings get no vibrato: test vibrato on stopped notes.
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
- **A new rule can open an old path.** Slur Range made "back to the key still
  held" take a new bow when it was a wide leap; in pizzicato that started a
  bowed stroke that never lifted (the stress test caught it at −40 dBFS 5 s
  after release). `main` had the same bug with Legato Slurs off. Run the
  stress test after any change to the key logic, and find the cause with a
  replay of its passage into the model (`#define private public`, print the
  performer's state).
- **CodeQL** flags `float * float` converted to `double` in the labs: cast
  one operand first (`static_cast<double>(a) * b`).
