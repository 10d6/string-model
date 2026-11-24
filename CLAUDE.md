# String Model — notes for Claude

String Model is a synth built on synthkit (the `synthkit/` submodule, pinned).
The owner plays it and gives feedback; Claude writes the code and opens PRs.
The owner tests via the PR's GitHub Pages preview, and installs the plugin zips
from CI at milestones.

## Ground rules

1. **`synthkit/API.md` is the contract.** Work in this repo needs only that
   page plus this repo. Don't read synthkit's source to build the synth; if
   the page is missing something, note it and fix the doc in a synthkit PR.
2. **Don't edit `synthkit/` here.** Kit changes go to the synthkit repo; this
   repo then moves the submodule to the new commit in its own PR.
3. **Real-time safety** (API.md §4a): no allocation, locks, I/O or exceptions
   in `start`, `release`, `render`, `processGlobal`, `processEvents`, `tune`,
   `allocateVoice`. Allocate in `prepare`. No `throw`/`catch` anywhere.
4. **Stable identifiers.** Parameter IDs, `synth.json` `id`, `pluginCode`,
   `vendorCode` and `bundleId` never change once released. Patches store real
   units.
5. **Verify by rendering** before every PR: `--check` passes, and listen to
   (or look at) the renders when the change is audible.
6. Escape hatches and custom state (API.md §4, §5a) are the way to do unusual
   things. A feature moves into the kit when a second synth needs it.
7. **Porting a browser sketch?** Use the Web Audio blocks (`sk::wa`, API.md
   §8a), which match the browser's nodes sample for sample, and compare the
   port's renders with the sketch's. Input from the page that isn't a
   parameter is a message (§9b); a look of the synth's own is `ui/page.js`
   (§9a), with `ui/style.css` for styling either page.

## Fresh workspace

A new session starts without the toolchains. Install them first (Ubuntu; the
same script CI uses):

```
git submodule update --init
synthkit/tools/ci/deps.sh native && synthkit/tools/ci/deps.sh web && synthkit/tools/ci/deps.sh plugin
```

Playwright/Chromium for the browser test: usually preinstalled (try
`NODE_PATH=$(npm root -g)`), else `npm install --no-save playwright@1.48.2 &&
npx playwright install --with-deps chromium`. synthkit's CLAUDE.md has more
(pluginval, a local JUCE checkout).

## Commands

```
cmake -S . -B build -G Ninja && cmake --build build
./build/stringmodel_render --check . --out build/renders
./build/stringmodel_render tests/chord.json --out x.wav
./build/stringmodel_render --params
python3 synthkit/tools/stress.py build/stringmodel_render 300 --tail 5   # random playing: every note must stop (API.md §10); plucked open strings ring > 3 s

# the model, offline (docs/MODEL.md)
./build/string_lab sweep 1 62 0.13                 # Schelleng map, open D
./build/string_lab tone 2 71 0.3 1.2 0.05 x.wav    # one stroke: pitch, slips/period, temperature
./build/instrument_lab hold 69 12 0.7              # held note: bow changes, dips, attack time
./build/instrument_lab chord 62 69                 # double stop: force and slip rate per string
./build/instrument_lab sympathy 67                 # each string's level: sympathetic ringing
python3 tools/calibrate_force.py [--instrument cello]   # Helmholtz window (stick test) vs the playability assistant
./build/radiation_lab                              # radiation vs its target, stereo difference per band
python3 tools/analyze.py build/renders/legato-scale.wav   # spectrogram, level, pitch per note
python3 tools/partial_q.py --data ~/data/phil/wav         # each partial's decay (Q) after a pluck vs the Philharmonia: the string's losses
./build/string_lab guettler 1 62 0.1 enhanced     # attacks from rest: perfect / within 50 ms
./build/radiation_lab character                    # body controls per third-octave band
./build/radiation_lab directivity                  # radiation patterns: per octave band, semitone to semitone
python3 tools/room_power.py [--instrument cello]    # each Space's wet/direct power (after changing the room or radiation level)
SM_INSTRUMENT=cello ./build/instrument_lab hold 50 4 0.7   # any lab on the cello (default: the violin)
python3 tools/analyze.py x.wav --fmin 58           # the cello's low notes need the lower pitch floor
python3 tools/window_map.py                          # the cello's coupled windows: contact point x force, speed pinned
python3 tools/schelleng_check.py                     # the cello D string's window vs Galluzzo's measurement
python3 tools/wolf_check.py                         # the cello's wolf: the six acceptance tests (cello doc §5)
python3 tools/wolf_check.py --simple --string 1     # the wolf as played: beat, rate, regime per note and dynamic
python3 tools/partial_q.py --instrument cello --iowa ~/data/iowa/cello/wav/pizz --dyn pp,mf,ff   # cello losses vs Iowa
python3 tools/open_strings.py --instrument cello    # cello open strings under the bow
python3 tools/pizz_decay.py --iowa ~/data/iowa/cello/wav/pizz   # cello pizzicato's two-stage decay vs Iowa
printf '2.5 69 1 .6\n2.95 69 0 .5\n3 74 1 .6\n4 74 0 .5\n' | ./build/bowing_lab 0.5 92 3 4   # bow direction, rebows

# evaluation scripts (docs/EVALUATION.md has the reference numbers)
python3 tools/open_strings.py                      # open strings in tune under the bow
python3 tools/harmonics_tuning.py                  # natural/artificial harmonics
python3 tools/harmonic_timbre.py --data ~/data/phil/wav   # harmonics' timbre vs the Philharmonia (harmonic vs arco, partials 2-6)
python3 tools/portamento_glide.py                  # Portamento: glide times on slow slurred leaps
python3 tools/brightness_spread.py --vsco "<VSCO>/Strings/Solo Violin/Arco Vib" [render ...]   # f - p colour vs the recordings
python3 tools/force_brightness.py                  # the string alone (string_lab): brightness vs bow force
python3 tools/fundamental_strength.py --data ~/data/phil/wav   # 1st partial re 2-4 per note vs the recordings
./build/performer_fuzz 50 && ./build/performer_fuzz 50 doubled  # key handling: random fast playing, then one key -> one note
python3 tools/intonation_check.py                  # Intonation Style: scale, double stops
python3 tools/vibrato_extent.py                    # Vibrato Shaping
python3 tools/pizz_decay.py "<VSCO>/Strings/Solo Violin/Pizz"   # two-stage decay vs recordings
python3 tools/note_pitch.py tests/spiccato.json    # pitch per note of any scenario
LAB_HF=1 tools/bow_change_gap.sh 69                # automatic bow change gap
python3 tools/perceptual.py --data ~/data/phil/wav [--json out.json]   # perceptual distance to the Philharmonia recordings (2.5 min)
python3 tools/perceptual.py --compare a.json b.json [--by-note] # two builds: note by note, per dynamic
python3 tools/perceptual.py --yardstick             # recording vs recording

# browser page (API.md §11)
cmake -S . -B build-web -G Ninja -DCMAKE_TOOLCHAIN_FILE=synthkit/cmake/wasm32-wasi.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-web   # Release: without it the page is unoptimised (-O0)
node synthkit/web/test/browser-test.cjs build-web/web/stringmodel.html build-web/test tests/chord.json
node synthkit/web/test/bench-web.cjs build-web/web/stringmodel.html tests/chord.json   # browser speed

# plugins (API.md §12)
cmake -S . -B build-plugin -G Ninja -DSYNTHKIT_PLUGIN=ON -DSYNTHKIT_WEB_UI_DIR=$PWD/build-web/web && cmake --build build-plugin
```

## This synth

- Read `docs/MODEL.md` before changing the model and `docs/PLAN.md` for the
  phases. Physics claims come from 10d6/instrument-research's KB (cite §).
- `model/` stays free of synthkit (plain C++20) so the labs can test it.
- The instrument runs in `processGlobal`; the kit's voices are silent key
  trackers. Don't move sound into voices: the strings will share a bridge.
- Parameters the instrument reads use `smoothing(0)` and are smoothed in the
  performer on its own clock: bowing is chaotic, so kit smoothing (per
  control block) makes renders depend on the host block size.
- The radiation target was measured against VSCO-2-CE recordings (CC0;
  fetch with 10d6/instrument-research `tools/fetch_vsco.sh`, never commit
  them). After changing the string's spectrum, rerun
  `tools/radiation_target.py <Arco Vib dir> build/stringmodel_render - 3`:
  every half-octave band should stay within about ±3 dB.
- After changing the string or friction, rerun `string_lab sweep` and check
  the Schelleng map still holds Demoucron's pp/mf/ff points, and update
  MODEL.md. After changing the string, bridge or assistant, rerun
  `tools/calibrate_force.py`: every row should straddle 0 with margin.
- After changing the string's losses, the bridge or the body, rerun
  `tools/partial_q.py`: the stopped/open median Q per band should stay near
  the recordings' (docs/EVALUATION.md).
- After changing the string, friction, the brightness tilt or the
  performer's dynamics law, rerun `tools/brightness_spread.py` (f − p within
  ~1 dB of VSCO per band). After changing the key logic, run
  `performer_fuzz` (single and doubled) and the stress test.
- After changing friction, string, bridge or the open-string allowance, also
  run `tools/open_strings.py` and `tools/harmonics_tuning.py`; after changing
  the performer's bowing rules, `bowing_lab` (no mid-note rebows); after
  pizzicato or string changes, `tools/pizz_decay.py`. Compare with the
  reference numbers in `docs/EVALUATION.md` and update them.
- The Philharmonia violin samples (the owner supplies the zip) are free to
  use but must not be shared as samples: never commit them. Decode to WAV
  outside the repo; `tools/perceptual.py` (docs/EVALUATION.md) measures
  against them. Run it before and after an audible change and compare
  (`--compare`); mind its blind spot (the body's fine structure).
- The performer's player habits (Intonation Style, Vibrato Shaping, Metric
  Emphasis, Portamento) follow instrument-research's
  `player_technique_and_responsiveness.md` (cite §). Model what players
  measurably do, not what they say (docs/EVALUATION.md "Lessons").
- Bowing is chaotic: judge changes over several notes/strings/dynamics,
  A/B against a base build in a worktree (at the current `main`), and keep
  margins in test expectations. docs/EVALUATION.md has the techniques and
  the lessons; read its Lessons before a model change.
- **Data outside the repo** (never commit it): the Philharmonia violin WAVs
  (the owner's zip, decoded to e.g. `~/data/phil/wav`; the tools take
  `--data`), VSCO-2-CE (`fetch_vsco.sh` from instrument-research: Arco Vib,
  Pizz and Trem for the solo violin), the KB (clone 10d6/instrument-research
  next to this repo). A fresh session has none of it: fetch VSCO, ask the
  owner for the Philharmonia zip.
- **Phase 8 (cello)** follows docs/PLAN.md "Phase 8: the cello": seven PRs,
  the recordings, the decisions, and the violin-specific constants still
  hard-coded. Each instrument is an `InstrumentDef` (`model/defs/`); the
  synth prepares them all and switches without allocating. A change meant
  for one instrument must leave the other's renders unchanged: compare
  `--check` renders' hashes with a base build (8.1 kept the violin
  bit-identical). On the cello, check the bowing window with
  `tools/calibrate_force.py --instrument cello` (the stick test; a
  periodicity test of the output passes irregular slipping, KB §7.3), and
  the wolf with `tools/wolf_check.py`. The cello's companion research is
  instrument-research `docs/cello_modelling_data.md`; the mdw Vienna
  (Zenodo) datasets are on hold at the owner's request (licence).
- **Cello data (outside the repo, never commit):** the Iowa MIS cello
  (anechoic; arco and pizzicato pp/mf/ff, a run of notes per file; free of
  restrictions, so it may be fitted for shipped parameters) decoded to
  `~/data/iowa/cello/wav/{arco,pizz}`, and the owner's Philharmonia cello
  zip decoded to `~/data/phil-cello/wav`. VSCO-2-CE has only a cello section
  (its library-mic curve is in `data/cello/`). A fresh session: ask the
  owner for the zips again (the workspace can't download them).

## Working with the owner

- **Branch from `origin/main` and open each PR against `main`.** Never stack
  a PR on another unmerged branch: GitHub retargets a stacked PR only if the
  branch below is deleted on merge, so #9 once merged into the Phase 6
  branch instead of `main` and needed #10. If work depends on an open PR,
  branch from it but target `main` (the PR then carries both commits).
- A plain `git fetch origin` may not update `origin/main` in this clone:
  use `git fetch origin main:refs/remotes/origin/main`.
- GitHub through `gh api` (REST; GraphQL is blocked):
  - open a PR: `gh api repos/10d6/string-model/pulls -f title=… -f head=BRANCH -f base=main -F body=@file`
  - edit its text: `gh api -X PATCH repos/10d6/string-model/pulls/N -F body=@file`
  - CI status: `gh api "repos/10d6/string-model/actions/runs?branch=BRANCH&per_page=1"`
  - a job's log: `gh api repos/10d6/string-model/actions/jobs/ID/logs`
  - re-run failed jobs: `gh api -X POST repos/10d6/string-model/actions/runs/ID/rerun-failed-jobs`
  - milestone plugin build: `gh api -X POST repos/10d6/string-model/actions/workflows/ci.yml/dispatches -f ref=BRANCH -F 'inputs[plugins]=true'` (zips in the run's Artifacts)
- The owner merges PRs, plays the Pages preview
  (`https://10d6.github.io/string-model/pr/N/`), and listens to before/after
  clips sent with each change. Describe changes in plain terms: what to
  listen for, what was measured.
- The owner's standing choices: plugin builds off for PRs; player habits on
  at moderate defaults; Auto Bow Change off by default; mic placement is Phase 7 and the cello Phase 8
  (docs/PLAN.md "Next").
- New parameters go at the end of `defineParams` (API.md doesn't say whether
  hosts key parameters by position); `ui/layout.json` places them on screen.

## CI

Every PR: native checks (`--check`, and `performer_fuzz` single and doubled), the browser page (Chromium test + browser-vs-native
comparison on `tests/chord.json`; chaotic passages diverge between the two
builds, see `ci.yml`) and a Pages preview linked on the PR. Plugin builds for Windows,
macOS and Linux (plugin check, pluginval, auval; a zip per OS) are off for
PRs at the owner's request: they run for `v*` tags (which publish a release)
and when CI is run by hand (Actions → CI → Run workflow, "plugins" ticked). The steps live in `synthkit/tools/ci/`.
