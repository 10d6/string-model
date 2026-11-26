// Every way a note starts and stops, checked without synthkit:
// - Keys (model/Keys.h): which key events reach the instrument and the kit's
//   silent voices (keyswitches, doubled input, repeated keys, MIDI channels,
//   all notes off).
// - Performer (model/Performer.h), through the Instrument, on the violin and
//   the cello: strokes, slurs and the return to a held key, chords, every
//   articulation and release mode, more keys than it remembers, unknown
//   releases, all notes off and reset.
// The synth's own routing (src/Synth.cpp: the queue, the pedals, Stop all
// sound) is covered by the render scenarios tests/keys-*.json.
//
//   ./build/key_tests          (exit 1 on any failure)
//
// Add a case here when a key-handling bug turns up; performer_fuzz covers
// random playing.
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "../model/Instrument.h"
#include "../model/Keys.h"
#include "../model/defs/Instruments.h"

using namespace sm;

static int failures = 0, checks = 0;
static const char* current = "";
#define CHECK(cond)                                                                \
  do {                                                                             \
    ++checks;                                                                      \
    if (!(cond)) {                                                                 \
      ++failures;                                                                  \
      std::printf("FAIL %s: %s (%s:%d)\n", current, #cond, __FILE__, __LINE__);    \
    }                                                                              \
  } while (0)
#define TEST(name) static void name()
#define RUN(name) (current = #name, name())

// ---------------------------------------------------------------------------
// Keys
using Route = Keys::Route;

TEST(keys_plain_press_and_release) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.down(60));
  CHECK(k.release(60, 0) == Route::Both);
  CHECK(!k.down(60));
}

TEST(keys_unknown_release_is_ignored) {
  Keys k;
  CHECK(k.release(60, 0) == Route::Ignore);
  CHECK(k.release(-1, 0) == Route::Ignore);
  CHECK(k.press(128, 0, 0.0, true) == Route::Ignore);
}

TEST(keys_keyswitch_press_and_release) {
  Keys k;
  for (int n = Keys::kKeyswitchLow; n < Keys::kKeyswitchLow + Keys::kKeyswitchCount; ++n) {
    CHECK(k.press(n, 0, 0.0, true) == Route::Keyswitch);
    CHECK(k.release(n, 0) == Route::Keyswitch);
  }
  // Just outside the range: played.
  CHECK(k.press(Keys::kKeyswitchLow - 1, 0, 0.0, true) == Route::Both);
  CHECK(k.press(Keys::kKeyswitchLow + Keys::kKeyswitchCount, 0, 0.0, true) == Route::Both);
  // Keyswitches off: the range plays.
  CHECK(k.press(26, 0, 0.0, false) == Route::Both);
  CHECK(k.release(26, 0) == Route::Both);
}

// The bug: a key pressed with Keyswitches off, released after turning them
// on, had its release swallowed and its note stuck.
TEST(keys_keyswitch_toggled_while_a_key_is_down) {
  Keys k;
  CHECK(k.press(26, 0, 0.0, false) == Route::Both);
  CHECK(k.release(26, 0) == Route::Both);  // decided at the press, not the release
  CHECK(k.press(26, 0, 1.0, true) == Route::Keyswitch);
  CHECK(k.release(26, 0) == Route::Keyswitch);  // turned off meanwhile: still a keyswitch
}

TEST(keys_doubled_input) {
  Keys k;
  CHECK(k.press(60, 0, 1.000, true) == Route::Both);
  CHECK(k.press(60, 0, 1.002, true) == Route::Ignore);  // the same press, again
  CHECK(k.release(60, 0) == Route::Both);               // lets go at the first release
  CHECK(k.release(60, 0) == Route::Ignore);             // the doubled release
  CHECK(!k.down(60));
  // A doubled keyswitch: both swallowed, both releases too.
  CHECK(k.press(30, 0, 2.0, true) == Route::Keyswitch);
  CHECK(k.press(30, 0, 2.001, true) == Route::Keyswitch);
  CHECK(k.release(30, 0) == Route::Keyswitch);
  CHECK(k.release(30, 0) == Route::Keyswitch);
}

// A sequencer's overlapping notes, or the same note on two channels: the
// instrument hears the release only when the last press goes up; the voices
// hear each.
TEST(keys_repeated_key_while_down) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 0, 0.5, true) == Route::Both);  // restrike
  CHECK(k.release(60, 0) == Route::Voices);         // one press up, the key still down
  CHECK(k.down(60));
  CHECK(k.release(60, 0) == Route::Both);
  CHECK(!k.down(60));
}

TEST(keys_doubled_window_edge) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 0, Keys::kDoubled - 1e-4, true) == Route::Ignore);
  CHECK(k.press(60, 0, Keys::kDoubled + 1e-4, true) == Route::Both);  // a restrike, not a double
}

TEST(keys_clear_forgets_every_key) {
  Keys k;
  k.press(60, 0, 0.0, true);
  k.press(62, 0, 0.0, true);
  k.press(26, 0, 0.0, true);
  k.clear();
  CHECK(k.downCount() == 0);
  CHECK(k.release(60, 0) == Route::Ignore);
  CHECK(k.release(26, 0) == Route::Ignore);  // nothing left to swallow: harmless either way
}

// The same note on two channels (a keyboard layering two channels, MPE): each
// channel's voice gets its own release, and the instrument hears the release
// only when both are up. Within 30 ms on another channel is not a double:
// a doubled input repeats the same message, on the same channel.
TEST(keys_two_channels) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 1, 0.001, true) == Route::Both);  // another channel: a voice of its own
  CHECK(k.release(60, 0) == Route::Voices);
  CHECK(k.down(60));
  CHECK(k.release(60, 1) == Route::Both);
  CHECK(!k.down(60));
  // In the other order.
  CHECK(k.press(62, 0, 1.0, true) == Route::Both);
  CHECK(k.press(62, 1, 1.5, true) == Route::Both);
  CHECK(k.release(62, 1) == Route::Voices);
  CHECK(k.release(62, 0) == Route::Both);
  // A release on a channel the key wasn't pressed on: nobody hears it.
  CHECK(k.press(64, 0, 2.0, true) == Route::Both);
  CHECK(k.release(64, 3) == Route::Ignore);
  CHECK(k.release(64, 0) == Route::Both);
}

// The case that would leave a lit key: a doubled press on channel 0, and the
// same note on channel 1. Every voice the kit started still gets its release.
TEST(keys_doubled_and_another_channel) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 0, 0.002, true) == Route::Ignore);  // doubled
  CHECK(k.press(60, 1, 0.1, true) == Route::Both);
  CHECK(k.release(60, 0) == Route::Voices);  // channel 0's voice
  CHECK(k.release(60, 0) == Route::Ignore);  // its doubled release
  CHECK(k.down(60));                         // channel 1 still holds it
  CHECK(k.release(60, 1) == Route::Both);    // channel 1's voice, and the instrument
  CHECK(!k.down(60));
}

// Random presses and releases on three channels (toggling Keyswitches,
// doubling, repeating), checked from both listeners' side: per channel the
// voices get exactly one release for each press they got (the kit pairs them
// by note and channel), never one too many; the instrument hears a key go up
// only once it went down, and only when no press of it is left; at the end
// every key is up for both.
TEST(keys_fuzz_balanced) {
  for (int seed = 0; seed < 300; ++seed) {
    std::mt19937 g(static_cast<unsigned>(seed));
    std::uniform_real_distribution<double> u(0, 1);
    Keys k;
    int voices[128][3]{}, pressed[128][3]{};
    bool instDown[128]{};
    bool ks = true, ok = true;
    double t = 0;
    auto release = [&](int n, int ch) {
      const Route r = k.release(n, ch);
      if (r == Route::Both || r == Route::Voices) --voices[n][ch];
      if (voices[n][ch] < 0) ok = false;  // a release the kit has no voice for
      if (r == Route::Both) {
        if (!instDown[n]) ok = false;  // the instrument never heard it go down
        instDown[n] = false;
        for (int c = 0; c < 3; ++c) ok = ok && voices[n][c] == 0;  // only the last release
      }
    };
    for (int ev = 0; ev < 500; ++ev) {
      t += u(g) < 0.3 ? 0.001 : 0.05 * u(g);
      if (u(g) < 0.05) ks = !ks;
      const int n = 20 + static_cast<int>(u(g) * 30);
      const int ch = u(g) < 0.7 ? 0 : 1 + static_cast<int>(u(g) * 2);
      if (u(g) < 0.55) {
        ++pressed[n][ch];
        const Route r = k.press(n, ch, t, ks);
        if (r == Route::Both) ++voices[n][ch], instDown[n] = true;
        if (r == Route::Voices) ok = false;  // a press never goes to the voices alone
      } else if (pressed[n][ch] > 0) {
        --pressed[n][ch];
        release(n, ch);
      }
    }
    for (int n = 0; n < 128; ++n)
      for (int ch = 0; ch < 3; ++ch)
        while (pressed[n][ch]-- > 0) release(n, ch);
    for (int n = 0; n < 128; ++n) {
      ok = ok && !instDown[n] && !k.down(n);
      for (int ch = 0; ch < 3; ++ch) ok = ok && voices[n][ch] == 0;
    }
    if (!ok) std::printf("  keys fuzz seed %d\n", seed);
    CHECK(ok);
  }
}

// ---------------------------------------------------------------------------
// Performer, through the Instrument.
struct Rig {
  Instrument inst;
  PerformerControls pc;
  InstrumentControls ic;
  explicit Rig(int instrument) { inst.prepare(48000.0, instrumentDef(instrument)); }
  Performer& p() { return inst.performer(); }
  void run(float s) {
    for (int i = 0; i < static_cast<int>(s * 48000.f); ++i) inst.process(pc, ic);
  }
  void on(int n, float v = 0.7f) { p().noteOn(n, v, pc); }
  void off(int n, float rel = 0.5f) { p().noteOff(n, rel, pc); }
  int bowedCount() {
    int b = 0;
    for (int s = 0; s < 4; ++s) b += p().bowedString(s);
    return b;
  }
  // The bow is off every string and no string is pushed (the strings may ring).
  bool quiet() {
    bool q = !p().bowing();
    for (int s = 0; s < 4; ++s) q = q && p().forceOn(s) == 0.f;
    return q;
  }
};

static int lowNote(int instrument) { return instrumentDef(instrument).strings[0].openNote; }

TEST(performer_note_on_off) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int n = lowNote(i) + 14;
    CHECK(r.quiet());
    r.on(n);
    r.run(0.3f);
    CHECK(r.p().bowing());
    CHECK(r.bowedCount() == 1);
    CHECK(r.p().heldCount() == 1);
    r.off(n);
    r.run(0.3f);
    CHECK(r.quiet());
    CHECK(r.p().heldCount() == 0);
  }
}

// Legato: the next key while one is down slurs (no new stroke); letting go of
// the newer key returns to the older one; the last key up ends the note.
TEST(performer_slur_and_return) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int a = lowNote(i) + 14, b = a + 2;
    r.on(a);
    r.run(0.3f);
    const int strokes = r.p().onset().id;
    r.on(b);
    r.run(0.2f);
    CHECK(r.p().onset().id == strokes);  // slurred: the same stroke
    CHECK(r.p().sounding() == b);
    r.off(b);
    r.run(0.2f);
    CHECK(r.p().sounding() == a);  // back to the held key
    CHECK(r.p().bowing());
    r.off(a);
    r.run(0.4f);
    CHECK(r.quiet());
  }
}

TEST(performer_legato_off_is_a_new_stroke) {
  Rig r(0);
  r.pc.legato = false;
  r.on(69);
  r.run(0.3f);
  const int strokes = r.p().onset().id;
  r.on(71);
  r.run(0.1f);
  CHECK(r.p().onset().id == strokes + 1);
  r.off(69);
  r.off(71);
  r.run(0.4f);
  CHECK(r.quiet());
}

// Releasing a key that isn't the sounding one changes nothing.
TEST(performer_release_of_an_older_key) {
  Rig r(0);
  r.on(69);
  r.run(0.2f);
  r.on(72);
  r.run(0.2f);
  r.off(69);
  r.run(0.2f);
  CHECK(r.p().sounding() == 72);
  CHECK(r.p().bowing());
  r.off(72);
  r.run(0.4f);
  CHECK(r.quiet());
}

TEST(performer_unknown_release_is_ignored) {
  Rig r(0);
  r.off(69);  // nothing held
  r.run(0.1f);
  CHECK(r.quiet());
  r.on(69);
  r.run(0.2f);
  r.off(70);  // not held
  r.run(0.2f);
  CHECK(r.p().bowing());
  CHECK(r.p().sounding() == 69);
  r.off(69);
  r.run(0.4f);
  CHECK(r.quiet());
}

// A held key pressed again within kDoubleOn is the same press (doubled input);
// later it restrikes. Either way one key, one note, one release.
TEST(performer_repeated_key) {
  Rig r(0);
  r.on(69);
  r.run(0.01f);
  const int strokes = r.p().onset().id;
  r.on(69);  // 10 ms later: ignored
  CHECK(r.p().onset().id == strokes);
  CHECK(r.p().heldCount() == 1);
  r.run(0.3f);
  r.pc.articulation = kSpiccato;  // a separate stroke shows the restrike
  r.on(69);
  CHECK(r.p().onset().id == strokes + 1);
  CHECK(r.p().heldCount() == 1);
  r.off(69);
  r.run(0.5f);
  CHECK(r.quiet());
}

// The bug: with 16 keys down, a 17th played but wasn't remembered, so letting
// go of it didn't return to the key below. Now the oldest is forgotten.
TEST(performer_more_keys_than_it_remembers) {
  Rig r(0);
  for (int n = 55; n < 55 + 18; ++n) {
    r.on(n);
    r.run(0.05f);
  }
  CHECK(r.p().heldCount() == 16);
  CHECK(r.p().sounding() == 72);
  r.off(72);
  r.run(0.1f);
  CHECK(r.p().sounding() == 71);  // back to the key below, which is remembered
  CHECK(r.p().bowing());
  for (int n = 55; n < 72; ++n) r.off(n);
  r.run(0.5f);
  CHECK(r.quiet());
  CHECK(r.p().heldCount() == 0);
}

TEST(performer_double_stop) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int lo = lowNote(i) + 9, hi = lo + 7;  // a fifth: adjacent strings
    r.on(lo);
    r.on(hi);  // within the chord window
    r.run(0.3f);
    CHECK(r.p().chordSize() == 2);
    CHECK(r.bowedCount() == 2);
    r.off(lo);
    r.run(0.2f);
    CHECK(r.p().chordSize() == 0);
    CHECK(r.bowedCount() == 1);
    CHECK(r.p().sounding() == hi);
    r.off(hi);
    r.run(0.4f);
    CHECK(r.quiet());
  }
}

TEST(performer_rolled_chord) {
  Rig r(0);
  r.on(55);
  r.on(62);
  r.on(69);
  r.on(76);
  r.run(0.4f);
  CHECK(r.p().chordSize() == 4);
  CHECK(r.bowedCount() == 2);  // the bow is on one pair at a time
  for (int n : {55, 62, 69, 76}) r.off(n);
  r.run(0.4f);
  CHECK(r.quiet());
  CHECK(r.p().chordSize() == 0);
}

// Keys far apart in time are not a chord: legato, one string.
TEST(performer_no_chord_outside_the_window) {
  Rig r(0);
  r.on(62);
  r.run(r.pc.chordWindow + 0.02f);
  r.on(69);
  r.run(0.2f);
  CHECK(r.p().chordSize() == 0);
  CHECK(r.bowedCount() == 1);
  r.off(62);
  r.off(69);
  r.run(0.4f);
  CHECK(r.quiet());
}

// A chord let go while an older key is still down returns to it.
TEST(performer_chord_over_a_held_key) {
  Rig r(0);
  r.on(69);
  r.run(0.3f);
  r.on(74);
  r.on(81);
  r.run(0.3f);
  CHECK(r.p().chordSize() == 2);
  r.off(74);
  r.off(81);
  r.run(0.2f);
  CHECK(r.p().sounding() == 69);
  CHECK(r.p().bowing());
  r.off(69);
  r.run(0.4f);
  CHECK(r.quiet());
}

// Every articulation and release mode, quick and gentle key-ups: the bow
// leaves the string within a second (a plucked note never has the bow).
TEST(performer_every_articulation_and_release) {
  for (int i = 0; i < kInstrumentCount; ++i)
    for (int art = 0; art <= kSautille; ++art)
      for (int mode = 0; mode < 4; ++mode)
        for (float rel : {0.3f, 0.95f}) {
          Rig r(i);
          r.pc.articulation = art;
          r.pc.releaseMode = mode;
          const int n = lowNote(i) + 16;
          r.on(n);
          r.run(0.4f);
          if (art == kPizzicato) CHECK(!r.p().bowing());
          else CHECK(r.p().bowing());
          r.off(n, rel);
          r.run(1.f);
          if (!r.quiet()) std::printf("  instrument %d articulation %d release %d key-up %.2f\n", i, art, mode, rel);
          CHECK(r.quiet());
        }
}

// The articulation is taken at the press: changing it while a note sounds
// doesn't change how that note ends.
TEST(performer_articulation_changed_mid_note) {
  for (int art = 0; art <= kSautille; ++art) {
    Rig r(0);
    r.on(69);
    r.run(0.3f);
    r.pc.articulation = art;
    r.run(0.1f);
    r.off(69);
    r.run(0.6f);
    CHECK(r.quiet());
    // ... and a pizzicato over a held arco note takes the bow off.
    Rig q(0);
    q.on(69);
    q.run(0.3f);
    q.pc.articulation = kPizzicato;
    q.on(72);
    q.run(0.1f);
    CHECK(!q.p().bowing());
    q.off(72);
    q.off(69);
    q.run(0.5f);
    CHECK(q.quiet());
  }
}

TEST(performer_auto_bow_change_long_note) {
  Rig r(0);
  r.pc.autoChange = true;
  r.on(69);
  r.run(6.f);  // several bow changes
  CHECK(r.p().bowing());
  r.off(69);
  r.run(0.5f);
  CHECK(r.quiet());
}

TEST(performer_all_notes_off) {
  Rig r(0);
  r.on(62);
  r.on(69);
  r.run(0.3f);
  r.on(76);
  r.run(0.2f);
  r.p().allNotesOff();
  r.run(0.3f);
  CHECK(r.quiet());
  CHECK(r.p().heldCount() == 0);
  CHECK(r.p().chordSize() == 0);
  // The keys' late releases are harmless; a new key plays one note.
  r.off(62);
  r.off(76);
  r.on(64);
  r.run(0.3f);
  CHECK(r.bowedCount() == 1);
  r.off(64);
  r.off(69);
  r.run(0.4f);
  CHECK(r.quiet());
}

TEST(performer_reset) {
  Rig r(0);
  r.on(69);
  r.run(0.3f);
  r.inst.reset();
  CHECK(r.quiet());
  CHECK(r.p().heldCount() == 0);
  r.off(69);  // the key's release after a reset (Stop all sound): ignored
  r.run(0.1f);
  CHECK(r.quiet());
}

int main() {
  RUN(keys_plain_press_and_release);
  RUN(keys_unknown_release_is_ignored);
  RUN(keys_keyswitch_press_and_release);
  RUN(keys_keyswitch_toggled_while_a_key_is_down);
  RUN(keys_doubled_input);
  RUN(keys_repeated_key_while_down);
  RUN(keys_doubled_window_edge);
  RUN(keys_clear_forgets_every_key);
  RUN(keys_two_channels);
  RUN(keys_doubled_and_another_channel);
  RUN(keys_fuzz_balanced);
  RUN(performer_note_on_off);
  RUN(performer_slur_and_return);
  RUN(performer_legato_off_is_a_new_stroke);
  RUN(performer_release_of_an_older_key);
  RUN(performer_unknown_release_is_ignored);
  RUN(performer_repeated_key);
  RUN(performer_more_keys_than_it_remembers);
  RUN(performer_double_stop);
  RUN(performer_rolled_chord);
  RUN(performer_no_chord_outside_the_window);
  RUN(performer_chord_over_a_held_key);
  RUN(performer_every_articulation_and_release);
  RUN(performer_articulation_changed_mid_note);
  RUN(performer_auto_bow_change_long_note);
  RUN(performer_all_notes_off);
  RUN(performer_reset);
  std::printf("%d of %d checks failed\n", failures, checks);
  return failures ? 1 : 0;
}
