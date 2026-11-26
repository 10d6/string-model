// Fuzz the performer's key handling (no synthkit): random fast playing in
// every articulation (overlapping keys, chords, simultaneous events, quick and
// gentle key-ups, the release mode and articulation changing mid-phrase),
// then let go of everything and check that within a second the bow is off
// every string (nothing stuck), then press one key and check that exactly
// one string is bowed and no chord is left. With "doubled", every event also
// arrives a second time 2 ms later (a keyboard the browser sees as two MIDI
// inputs): the result must be the same as without. SM_INSTRUMENT=cello
// plays the cello (tools/LabInstrument.h).
//
//   ./build/performer_fuzz [passages per articulation, default 50] [doubled]
//
// Found with it: nothing stuck (PR #25's double notes were a doubled input,
// now ignored in Performer::noteOn). Specific cases, and each bug found, go
// in tests/key_tests.cpp.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include "LabInstrument.h"
#include "../model/Instrument.h"

using namespace sm;

int main(int argc, char** argv) {
  const int perArt = argc > 1 ? std::atoi(argv[1]) : 50;
  const bool doubled = argc > 2 && !std::strcmp(argv[2], "doubled");
  const char* names[] = {"arco", "martele", "spiccato", "tremolo", "pizzicato", "harmonics", "sautille"};
  int bad = 0, total = 0;
  for (int art = 0; art <= 6; ++art) {
    for (int seed = 0; seed < perArt; ++seed) {
      std::mt19937 rng(static_cast<unsigned>(seed * 7 + art));
      std::uniform_real_distribution<float> u(0.f, 1.f);
      Instrument inst;
      inst.prepare(48000.0, labInstrument());
      PerformerControls pc;
      InstrumentControls ic;
      pc.articulation = art;
      auto& p = inst.performer();
      auto run = [&](float sec) {
        for (int i = 0; i < static_cast<int>(sec * 48000.f); ++i) inst.process(pc, ic);
      };
      auto on = [&](int n, float v) {
        p.noteOn(n, v, pc);
        if (doubled) { run(0.002f); p.noteOn(n, v, pc); }
      };
      auto off = [&](int n) {
        const float rel = u(rng);  // above 0.7 a quick key-up: Auto stops the bow
        p.noteOff(n, rel, pc);
        if (doubled) { run(0.002f); p.noteOff(n, rel, pc); }
      };
      std::vector<int> down;
      for (int ev = 0; ev < 200; ++ev) {
        // Now and then the player changes the release, or the articulation
        // (a keyswitch) mid-phrase; it comes back for the next passage.
        if (u(rng) < 0.03f) pc.releaseMode = static_cast<int>(u(rng) * 4.f);
        if (u(rng) < 0.03f) pc.articulation = u(rng) < 0.5f ? art : static_cast<int>(u(rng) * 7.f);
        if (u(rng) < 0.55f || down.empty()) {
          const int n = 55 + static_cast<int>(u(rng) * 36.f);
          bool isDown = false;
          for (int d : down) isDown |= d == n;
          if (!isDown) { on(n, 0.3f + 0.6f * u(rng)); down.push_back(n); }
        } else {
          const int i = static_cast<int>(u(rng) * static_cast<float>(down.size()));
          off(down[static_cast<std::size_t>(i)]);
          down.erase(down.begin() + i);
        }
        if (u(rng) < 0.8f) run(0.02f + 0.08f * u(rng));  // else simultaneous events
      }
      for (int d : down) off(d);
      run(1.f);
      // Every key is up: the bow has left the strings (they may still ring).
      bool lifted = !p.bowing() && p.heldCount() == 0;
      for (int s = 0; s < 4; ++s) lifted = lifted && p.forceOn(s) == 0.f;
      pc.articulation = art;
      pc.releaseMode = 0;
      on(69, 0.7f);
      run(0.2f);
      int bowed = 0;
      for (int s = 0; s < 4; ++s) bowed += p.bowedString(s);
      const bool ok = lifted && (art == 4 ? bowed == 0 : (bowed == 1 && p.chordSize() <= 1));  // pizzicato: no bow
      ++total;
      if (!ok) {
        ++bad;
        std::printf("%s seed %d: %sbowed strings %d, chord %d\n", names[art], seed, lifted ? "" : "STUCK after all keys up; then ", bowed, p.chordSize());
      }
    }
  }
  std::printf("%d of %d passages bad%s\n", bad, total, doubled ? " (doubled input)" : "");
  return bad ? 1 : 0;
}
