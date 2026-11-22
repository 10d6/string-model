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
python3 synthkit/tools/stress.py build/stringmodel_render 300   # random playing: every note must stop (API.md §10)

# the model, offline (docs/MODEL.md)
./build/string_lab sweep 1 62 0.13                 # Schelleng map, open D
./build/string_lab tone 2 71 0.3 1.2 0.05 x.wav    # one stroke: pitch, slips/period, temperature
./build/instrument_lab hold 69 12 0.7              # held note: bow changes, dips, attack time
./build/instrument_lab chord 62 69                 # double stop: force and slip rate per string
./build/instrument_lab sympathy 67                 # each string's level: sympathetic ringing
python3 tools/calibrate_force.py                   # Helmholtz window vs the playability assistant
./build/radiation_lab                              # radiation vs its target, stereo difference per band
python3 tools/analyze.py build/renders/legato-scale.wav   # spectrogram, level, pitch per note

# browser page (API.md §11)
cmake -S . -B build-web -G Ninja -DCMAKE_TOOLCHAIN_FILE=synthkit/cmake/wasm32-wasi.cmake && cmake --build build-web
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

## CI

Every PR: native checks, the browser page (Chromium test + browser-vs-native
comparison), a Pages preview linked on the PR, and plugin builds for Windows,
macOS and Linux (plugin check, pluginval, auval) with a zip per OS. A `v*` tag
publishes a release. The steps live in `synthkit/tools/ci/`.
