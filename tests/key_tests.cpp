// Every way a note starts and stops, checked without synthkit:
// - Keys (model/Keys.h): which key events reach the instrument and the kit's
//   silent voices (keyswitches, doubled input, repeated keys, MIDI channels,
//   all notes off).
// - Performer (model/Performer.h), through the Instrument, on the violin and
//   the cello: polyphonic notes and chords, voices freed and reused, the
//   legato player's slurs, string crossings and return to a held key, every
//   articulation and release mode in both, more keys than it holds, unknown
//   releases, Legato switched mid-phrase, all notes off and reset.
// The synth's own routing (src/Synth.cpp: the queue, the pedals, Stop all
// sound) is covered by the render scenarios tests/keys-*.json.
//
//   ./build/key_tests          (exit 1 on any failure)
//
// Add a case here when a key-handling bug turns up; performer_fuzz covers
// random playing.
#include <cstdio>
#include <algorithm>
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
  // Down as a note, its release lost, Keyswitches turned on: pressing it
  // again is still a re-press of the note (so a stuck note there clears).
  CHECK(k.press(27, 0, 0.0, false) == Route::Both);
  CHECK(k.press(27, 0, 1.0, true) == Route::Repress);
  CHECK(k.release(27, 0) == Route::Both);
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
  CHECK(k.release(30, 0) == Route::Ignore);  // nobody hears it either way
  CHECK(k.represses() == 0);
}

// The owner's stuck C4: a press whose release never arrived, then the key
// pressed again. A key can't go down twice, so the second press ends the
// first (the voices let go of it: one voice per key), the instrument
// restrikes, and the next release ends the note. Before, the second press
// counted as a second, overlapping press and the note needed two releases.
TEST(keys_lost_release_then_pressed_again) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);  // its release is lost
  int replaced = -1;
  CHECK(k.press(60, 0, 2.0, true, &replaced) == Route::Repress);
  CHECK(replaced == 0);
  CHECK(k.represses() == 1);
  CHECK(k.voices() == 1);
  CHECK(k.release(60, 0) == Route::Both);  // ends the note
  CHECK(!k.down(60));
  CHECK(k.voices() == 0);
}

// A sequencer overlapping the same note: the second press restrikes, the
// first release ends the note (as a single key would), the second is ignored.
TEST(keys_overlapping_presses) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 0, 0.5, true) == Route::Repress);
  CHECK(k.release(60, 0) == Route::Both);
  CHECK(k.release(60, 0) == Route::Ignore);
  CHECK(!k.down(60));
}

TEST(keys_doubled_window_edge) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 0, Keys::kDoubled - 1e-4, true) == Route::Ignore);
  CHECK(k.press(60, 0, Keys::kDoubled + 1e-4, true) == Route::Repress);  // a re-press, not a double
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
  CHECK(k.press(60, 0, 1.0, true) == Route::Both);  // a fresh press, not a re-press
}

// Channels: a key is one key. The same note on another channel at the same
// moment (a keyboard layering two channels) is one press; later, it's a
// re-press, which hands the voice to the new channel. Each release is
// matched on its own channel.
TEST(keys_channels) {
  Keys k;
  CHECK(k.press(60, 0, 0.0, true) == Route::Both);
  CHECK(k.press(60, 1, 0.001, true) == Route::Ignore);  // layered: the same press
  CHECK(k.release(60, 0) == Route::Both);
  CHECK(k.release(60, 1) == Route::Ignore);
  CHECK(!k.down(60));
  int replaced = -1;
  CHECK(k.press(62, 0, 1.0, true) == Route::Both);
  CHECK(k.press(62, 1, 1.5, true, &replaced) == Route::Repress);
  CHECK(replaced == 0);
  CHECK(k.release(62, 0) == Route::Ignore);  // the ended press's release
  CHECK(k.down(62));
  CHECK(k.release(62, 1) == Route::Both);
  // A release on a channel the key wasn't pressed on: nobody hears it.
  CHECK(k.press(64, 0, 2.0, true) == Route::Both);
  CHECK(k.release(64, 3) == Route::Ignore);
  CHECK(k.release(64, 0) == Route::Both);
}

// Random presses and releases on three channels (toggling Keyswitches,
// doubling, pressing keys again while down, losing releases), checked from
// both listeners' side, as the kit pairs voices (by note and channel):
// - every release the voices get matches a voice they hold;
// - a key never has more than one voice (no doubled notes on the page);
// - the instrument hears a key go up only once it went down;
// - every key the instrument holds is down, and at the end, once every key
//   is let go (lost releases included: the key is pressed again and let go,
//   as the owner did), every key is up for both.
TEST(keys_fuzz_balanced) {
  for (int seed = 0; seed < 300; ++seed) {
    std::mt19937 g(static_cast<unsigned>(seed));
    std::uniform_real_distribution<double> u(0, 1);
    Keys k;
    int voices[128][16]{};
    bool instDown[128]{};
    std::vector<std::pair<int, int>> pressed;  // (note, channel) presses whose release will come
    bool ks = true, ok = true;
    double t = 0;
    auto voicesOf = [&](int n) {
      int v = 0;
      for (int c = 0; c < 16; ++c) v += voices[n][c];
      return v;
    };
    auto press = [&](int n, int ch) {
      int replaced = -1;
      const Route r = k.press(n, ch, t, ks, &replaced);
      if (r == Route::Repress) {
        if (replaced < 0 || voices[n][replaced] <= 0) ok = false;
        --voices[n][replaced];
      }
      if (r == Route::Both || r == Route::Repress) ++voices[n][ch], instDown[n] = true;
      if (voicesOf(n) > 1) ok = false;  // one voice per key
    };
    auto release = [&](int n, int ch) {
      const Route r = k.release(n, ch);
      if (r == Route::Both) {
        if (--voices[n][ch] < 0) ok = false;  // a release the kit has no voice for
        if (!instDown[n]) ok = false;         // the instrument never heard it go down
        instDown[n] = false;
      }
    };
    for (int ev = 0; ev < 500; ++ev) {
      t += u(g) < 0.3 ? 0.001 : 0.05 * u(g);
      if (u(g) < 0.05) ks = !ks;
      const int n = 20 + static_cast<int>(u(g) * 30);
      const int ch = u(g) < 0.7 ? 0 : 1 + static_cast<int>(u(g) * 2);
      if (u(g) < 0.55) {
        press(n, ch);
        if (u(g) < 0.95) pressed.push_back({n, ch});  // else its release is lost
      } else if (!pressed.empty()) {
        const int i = static_cast<int>(u(g) * static_cast<double>(pressed.size()));
        release(pressed[i].first, pressed[i].second);
        pressed.erase(pressed.begin() + i);
      }
      for (int m = 0; m < 128; ++m) ok = ok && (!instDown[m] || k.down(m) || Keys::inKeyswitchRange(m));
    }
    for (auto& p : pressed) release(p.first, p.second);
    // Keys whose release was lost are still down: the player presses each again and lets go.
    for (int n = 0; n < 128; ++n)
      if (k.down(n)) {
        t += 1.0;
        int ch = 0;
        for (int c = 0; c < 16; ++c)
          if (voices[n][c] > 0) ch = c;
        press(n, ch);
        release(n, ch);
      }
    for (int n = 0; n < 128; ++n) ok = ok && !instDown[n] && !k.down(n) && voicesOf(n) == 0;
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
  explicit Rig(int instrument, bool legato = false) {
    inst.prepare(48000.0, instrumentDef(instrument));
    pc.legato = legato;
  }
  Performer& p() { return inst.performer(); }
  // Runs the instrument, checking the performer's rules (Performer::audit)
  // every millisecond: a broken rule fails the test wherever it happens.
  void run(float s) {
    for (int i = 0; i < static_cast<int>(s * 48000.f); ++i) {
      inst.process(pc, ic);
      if (i % 48 == 0) audit();
    }
  }
  void audit() {
    int note;
    const int bad = p().audit(note, pc.legato);
    ++checks;
    if (bad && !reported) {
      ++failures;
      reported = true;  // once per rig
      std::printf("FAIL %s: Performer::audit 0x%x (note %d)\n", current, bad, note);
    }
  }
  bool reported = false;
  void on(int n, float v = 0.7f) { p().noteOn(n, v, pc); }
  void off(int n, float rel = 0.5f) { p().noteOff(n, rel, pc); }
  int bowedCount() { return p().bowingCount(); }
  // The stroke count of the voice playing `note` (-1: none).
  int strokesOf(int note) {
    const int v = p().voiceFor(note);
    return v < 0 ? -1 : p().voice(v).onset().id;
  }
  // Every bow is off its string and no string is pushed (the strings may ring).
  bool quiet() {
    bool q = p().bowingCount() == 0;
    for (int s = 0; s < 4; ++s) q = q && p().forceOn(s) == 0.f;
    return q;
  }
};

static int lowNote(int instrument) { return instrumentDef(instrument).strings[0].openNote; }

// --- Polyphonic (the default) ----------------------------------------------

TEST(poly_note_on_off) {
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

// Keys pressed together are a chord: one voice, one bow each. Letting go of
// one leaves the others playing.
TEST(poly_chord) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int lo = lowNote(i) + 12;
    const int notes[] = {lo, lo + 4, lo + 7, lo + 12};
    for (int n : notes) r.on(n);
    r.run(0.3f);
    CHECK(r.bowedCount() == 4);
    for (int n : notes) CHECK(r.p().voiceFor(n) >= 0);
    r.off(lo + 4);
    r.run(0.3f);
    CHECK(r.bowedCount() == 3);
    CHECK(r.p().voiceFor(lo + 4) < 0);
    for (int n : notes) r.off(n);
    r.run(0.4f);
    CHECK(r.quiet());
  }
}

// Overlapping keys are two notes (no slur): the second is a stroke of its
// own, and the first plays on until its key goes up.
TEST(poly_overlapping_keys_are_separate_notes) {
  Rig r(0);
  r.on(69);
  r.run(0.3f);
  r.on(71);
  r.run(0.1f);
  CHECK(r.bowedCount() == 2);
  CHECK(r.p().voiceFor(69) != r.p().voiceFor(71));
  r.off(69);
  r.run(0.3f);
  CHECK(r.bowedCount() == 1);
  CHECK(r.p().voiceFor(71) >= 0);
  r.off(71);
  r.run(0.4f);
  CHECK(r.quiet());
}

// A held key pressed again within kDoubleOn is the same press (doubled input);
// later it restrikes its own voice. Either way one key, one note, one release.
TEST(poly_repeated_key) {
  for (bool legato : {false, true}) {
    Rig r(0, legato);
    r.on(71);
    r.run(0.01f);
    const int v = r.p().voiceFor(71);
    const int strokes = r.strokesOf(71);
    r.on(71);  // 10 ms later: ignored
    CHECK(r.strokesOf(71) == strokes);
    CHECK(r.p().heldCount() == 1);
    r.run(0.3f);
    r.pc.articulation = kSpiccato;  // a separate stroke shows the restrike
    r.on(71);
    CHECK(r.p().voiceFor(71) == v);
    CHECK(r.strokesOf(71) == strokes + 1);
    CHECK(r.p().heldCount() == 1);
    CHECK(r.bowedCount() == 1);
    r.off(71);
    r.run(0.5f);
    CHECK(r.quiet());
  }
}

// More keys than it holds: the oldest is let go (its note ends), so the
// newest always plays and every release is heard.
TEST(poly_more_keys_than_it_holds) {
  Rig r(0);
  for (int n = 55; n < 55 + 20; ++n) {
    r.on(n);
    r.run(0.05f);
  }
  CHECK(r.p().heldCount() == Performer::kMaxHeld);
  CHECK(r.bowedCount() == Performer::kMaxHeld);
  CHECK(r.p().voiceFor(55) < 0);  // the oldest, let go
  CHECK(r.p().voiceFor(74) >= 0);
  for (int n = 55; n < 55 + 20; ++n) r.off(n);
  r.run(0.5f);
  CHECK(r.quiet());
  CHECK(r.p().heldCount() == 0);
}

// Released notes ring on, then their voices are freed; a fast passage of
// short notes never runs out of voices (the quietest ringing one makes room).
TEST(poly_voices_are_freed_and_reused) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int lo = lowNote(i) + 13;
    int most = 0;
    for (int k = 0; k < 60; ++k) {
      const int n = lo + (k * 5) % 24;
      r.on(n, 0.8f);
      r.run(0.03f);
      r.off(n, 0.3f);  // lift: the string rings on
      r.run(0.03f);
      most = std::max(most, r.p().loadedCount());
      CHECK(r.p().voiceFor(n) < 0);
    }
    CHECK(most <= Performer::kVoices);
    r.run(8.f);
    CHECK(r.quiet());
    if (r.p().loadedCount() != 0) std::printf("  instrument %d: %d voices still on after 8 s\n", i, r.p().loadedCount());
    CHECK(r.p().loadedCount() == 0);
    // ... and a chord after it all plays in full.
    for (int n : {lo, lo + 4, lo + 7}) r.on(n);
    r.run(0.2f);
    CHECK(r.bowedCount() == 3);
    for (int n : {lo, lo + 4, lo + 7}) r.off(n);
    r.run(0.4f);
    CHECK(r.quiet());
  }
}

// Every articulation and release mode, quick and gentle key-ups, both modes:
// the bow leaves the string within a second (a plucked note never has the bow).
TEST(performer_every_articulation_and_release) {
  for (bool legato : {false, true})
    for (int i = 0; i < kInstrumentCount; ++i)
      for (int art = 0; art <= kSautille; ++art)
        for (int mode = 0; mode < 4; ++mode)
          for (float rel : {0.3f, 0.95f}) {
            Rig r(i, legato);
            r.pc.articulation = art;
            r.pc.releaseMode = mode;
            const int n = lowNote(i) + 16;
            r.on(n);
            r.run(0.4f);
            if (art == kPizzicato) CHECK(!r.p().bowing());
            else CHECK(r.p().bowing());
            r.off(n, rel);
            r.run(1.f);
            if (!r.quiet()) std::printf("  legato %d instrument %d articulation %d release %d key-up %.2f\n", legato, i, art, mode, rel);
            CHECK(r.quiet());
          }
}

// The articulation is taken at the press: changing it while a note sounds
// doesn't change how that note ends.
TEST(performer_articulation_changed_mid_note) {
  for (bool legato : {false, true})
    for (int art = 0; art <= kSautille; ++art) {
      Rig r(0, legato);
      r.on(69);
      r.run(0.3f);
      r.pc.articulation = art;
      r.run(0.1f);
      r.off(69);
      r.run(0.6f);
      CHECK(r.quiet());
      // ... and a pizzicato over a held arco note: in legato it takes the bow
      // off; polyphonic, the arco note plays on.
      Rig q(0, legato);
      q.on(69);
      q.run(0.3f);
      q.pc.articulation = kPizzicato;
      q.on(72);
      q.run(0.1f);
      CHECK(q.bowedCount() == (legato ? 0 : 1));
      q.off(72);
      q.off(69);
      q.run(0.5f);
      CHECK(q.quiet());
    }
}

TEST(performer_all_notes_off) {
  for (bool legato : {false, true}) {
    Rig r(0, legato);
    r.on(62);
    r.on(69);
    r.run(0.3f);
    r.on(76);
    r.run(0.2f);
    r.p().allNotesOff();
    r.run(0.3f);
    CHECK(r.quiet());
    CHECK(r.p().heldCount() == 0);
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
}

TEST(performer_reset) {
  Rig r(0);
  r.on(69);
  r.on(76);
  r.run(0.3f);
  r.inst.reset();
  CHECK(r.quiet());
  CHECK(r.p().heldCount() == 0);
  CHECK(r.p().loadedCount() == 0);
  r.off(69);  // the key's release after a reset (Stop all sound): ignored
  r.run(0.1f);
  CHECK(r.quiet());
}

TEST(performer_unknown_release_is_ignored) {
  for (bool legato : {false, true}) {
    Rig r(0, legato);
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
}

// --- Legato (the mono player) -----------------------------------------------

// The next key while one is down slurs (no new stroke); letting go of the
// newer key returns to the older one; the last key up ends the note.
TEST(legato_slur_and_return) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i, true);
    const int a = lowNote(i) + 16, b = a + 2;  // on one string (not an open string: Avoid Open Strings would move it)
    r.on(a);
    r.run(0.3f);
    const int v = r.p().leadVoice();
    const int strokes = r.p().lead().onset().id;
    r.on(b);
    r.run(0.2f);
    CHECK(r.p().leadVoice() == v);  // the same string
    CHECK(r.p().lead().onset().id == strokes);  // slurred: the same stroke
    CHECK(r.p().sounding() == b);
    CHECK(r.bowedCount() == 1);
    r.off(b);
    r.run(0.2f);
    CHECK(r.p().sounding() == a);  // back to the held key
    CHECK(r.p().bowing());
    r.off(a);
    r.run(0.4f);
    CHECK(r.quiet());
  }
}

// A slur onto another string carries the bow over (no new stroke); the old
// string rings on without the bow. Going back to it plays it again (one voice
// per string, as on the instrument).
TEST(legato_string_crossing) {
  Rig r(0, true);
  r.on(71);  // B4, on the A string
  r.run(0.3f);
  const int a = r.p().leadVoice();
  const int strokes = r.p().lead().onset().id;
  r.on(77);  // F5: the E string
  r.run(0.15f);
  const int e = r.p().leadVoice();
  CHECK(e != a);
  CHECK(r.p().lead().onset().id == strokes);  // the bow went on
  CHECK(r.bowedCount() == 1);
  CHECK(!r.p().voice(a).bowing());
  CHECK(r.p().loaded(a));  // ringing
  r.on(72);  // C5: back on the A string
  r.run(0.15f);
  CHECK(r.p().leadVoice() == a);
  CHECK(r.bowedCount() == 1);
  for (int n : {71, 77, 72}) r.off(n);
  r.run(0.4f);
  CHECK(r.quiet());
}

// Wider leaps than the Slur Range take a new bow.
TEST(legato_wide_leap_is_a_new_bow) {
  Rig r(0, true);
  r.pc.slurRange = 5;
  r.on(71);
  r.run(0.3f);
  const int strokes = r.p().lead().onset().id;
  r.on(73);  // a tone: slurred
  r.run(0.1f);
  CHECK(r.p().lead().onset().id == strokes);
  r.on(83);  // a seventh on: a new bow (on the E string's voice)
  r.run(0.1f);
  CHECK(r.p().sounding() == 83);
  CHECK(r.p().lead().onset().id == strokes + 1);
  CHECK(r.bowedCount() == 1);
  for (int n : {71, 73, 83}) r.off(n);
  r.run(0.4f);
  CHECK(r.quiet());
}

// Releasing a key that isn't the sounding one changes nothing.
TEST(legato_release_of_an_older_key) {
  Rig r(0, true);
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

// The bug: with 16 keys down, a 17th played but wasn't remembered, so letting
// go of it didn't return to the key below. Now the oldest is forgotten.
TEST(legato_more_keys_than_it_remembers) {
  Rig r(0, true);
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

// Letting go of a key that isn't sounding leaves the note alone, even when
// it's the newest key down (stress seed 46 found the chord version of this,
// tests before chords were removed).
TEST(legato_release_of_a_key_not_sounding) {
  Rig r(0, true);
  r.on(55);
  r.run(0.1f);
  r.on(88);  // too wide: a new bow, 88 plays
  r.run(0.1f);
  r.on(62);  // too wide again: 62 plays, the newest
  r.run(0.2f);
  CHECK(r.p().sounding() == 62);
  r.off(88);  // not sounding: nothing changes
  r.run(0.05f);
  CHECK(r.p().sounding() == 62);
  r.off(62);  // sounding: back to the newest key down, 55
  r.run(0.05f);
  CHECK(r.p().sounding() == 55);
  r.off(55);
  r.run(0.4f);
  CHECK(r.quiet());
}

// Legato switched on or off while keys are down: the notes held keep their
// voices until their keys go up; nothing sticks, and the key check holds.
TEST(legato_switched_mid_phrase) {
  for (int i = 0; i < kInstrumentCount; ++i) {
    Rig r(i);
    const int lo = lowNote(i) + 12;
    r.on(lo), r.on(lo + 4), r.on(lo + 7);
    r.run(0.2f);
    r.pc.legato = true;
    r.on(lo + 9);
    r.run(0.2f);
    r.off(lo + 9);  // back to a key a polyphonic voice holds: the player takes it
    r.run(0.2f);
    CHECK(r.p().sounding() == lo + 7);
    r.off(lo + 4);
    r.run(0.2f);
    r.pc.legato = false;
    r.on(lo + 2);
    r.run(0.2f);
    for (int n : {lo, lo + 2, lo + 4, lo + 7, lo + 9}) r.off(n);
    r.run(0.5f);
    CHECK(r.quiet());
    CHECK(r.p().heldCount() == 0);
  }
}

// ---------------------------------------------------------------------------
// Random MIDI through the synth's own path (processEvents: Keys, then the
// performer): channels, doubled messages, keyswitches toggled mid-phrase,
// repeated keys, lost releases (then the key pressed again to clear it), all
// notes off, the articulation, release and Legato changing. Every
// millisecond both listeners' rules are checked: Performer::audit, and every
// key the instrument holds is really down (Keys::strayHeld). At the end every
// key is up: the bows lift and the voices are all let go.
TEST(router_fuzz) {
  for (int i = 0; i < kInstrumentCount; ++i)
    for (int seed = 0; seed < 12; ++seed) {
      std::mt19937 g(static_cast<unsigned>(seed * 3 + i));
      std::uniform_real_distribution<float> u(0.f, 1.f);
      Rig r(i, seed % 2 == 1);
      Keys keys;
      bool ks = true;
      double t = 0;
      int strays = 0;
      std::vector<std::pair<int, int>> down;  // (note, channel) presses not yet released
      auto send = [&](bool on, int n, int ch, float v) {
        const Route route = on ? keys.press(n, ch, t, ks) : keys.release(n, ch);
        if (route == Route::Both || route == Route::Repress) {
          if (on) r.p().noteOn(n, v, r.pc);
          else r.p().noteOff(n, v, r.pc);
        }
      };
      auto step = [&](float sec) {
        const int n = static_cast<int>(sec * 48000.f);
        for (int k = 0; k < n; ++k) {
          r.inst.process(r.pc, r.ic);
          if (k % 48 == 0) {
            r.audit();
            if (keys.strayHeld(r.p()) >= 0) ++strays;
          }
        }
        t += n / 48000.0;
      };
      const int lo = instrumentDef(i).strings[0].openNote;
      for (int ev = 0; ev < 250; ++ev) {
        const float x = u(g);
        if (x < 0.03f) ks = !ks;
        else if (x < 0.05f) r.pc.articulation = static_cast<int>(u(g) * 7.f);
        else if (x < 0.07f) r.pc.releaseMode = static_cast<int>(u(g) * 4.f);
        else if (x < 0.075f) r.pc.legato = !r.pc.legato;
        else if (x < 0.08f) {  // all notes off
          keys.clear();
          r.p().allNotesOff();
          // The keys are still physically down; their releases come later.
        }
        if (u(g) < 0.55f || down.empty()) {
          const int n = u(g) < 0.1f ? 24 + static_cast<int>(u(g) * 12.f) : lo + static_cast<int>(u(g) * 36.f);
          const int ch = u(g) < 0.8f ? 0 : 1 + static_cast<int>(u(g) * 2.f);
          bool isDown = false;
          for (auto& d : down) isDown |= d.first == n && d.second == ch;
          if (!isDown || u(g) < 0.1f) {  // sometimes a key pressed again while down (a sequencer)
            const float v = 0.2f + 0.7f * u(g);
            send(true, n, ch, v);
            if (u(g) < 0.95f) down.push_back({n, ch});  // else its release is lost
            if (u(g) < 0.15f) send(true, n, ch, v), down.push_back({n, ch});  // doubled input
          }
        } else {
          const int k = static_cast<int>(u(g) * static_cast<float>(down.size()));
          send(false, down[k].first, down[k].second, u(g));
          down.erase(down.begin() + k);
        }
        if (u(g) < 0.75f) step(0.005f + 0.1f * u(g));  // else at the same moment
      }
      for (auto& d : down) send(false, d.first, d.second, 0.5f);
      // Keys whose release was lost are still down (and may sound): the
      // player presses each again and lets go, as the owner did.
      for (int n = 0; n < 128; ++n)
        if (keys.down(n)) {
          step(0.1f);
          send(true, n, 0, 0.6f);
          step(0.05f);
          send(false, n, 0, 0.5f);
        }
      step(1.5f);
      CHECK(strays == 0);
      CHECK(r.quiet());
      CHECK(keys.voices() == 0);
      CHECK(keys.downCount() == 0);
      CHECK(r.p().heldCount() == 0);
      if (strays || !r.quiet()) std::printf("  router fuzz instrument %d seed %d: strays %d\n", i, seed, strays);
    }
}

int main() {
  RUN(keys_plain_press_and_release);
  RUN(keys_unknown_release_is_ignored);
  RUN(keys_keyswitch_press_and_release);
  RUN(keys_keyswitch_toggled_while_a_key_is_down);
  RUN(keys_doubled_input);
  RUN(keys_lost_release_then_pressed_again);
  RUN(keys_overlapping_presses);
  RUN(keys_doubled_window_edge);
  RUN(keys_clear_forgets_every_key);
  RUN(keys_channels);
  RUN(keys_fuzz_balanced);
  RUN(poly_note_on_off);
  RUN(poly_chord);
  RUN(poly_overlapping_keys_are_separate_notes);
  RUN(poly_repeated_key);
  RUN(poly_more_keys_than_it_holds);
  RUN(poly_voices_are_freed_and_reused);
  RUN(performer_every_articulation_and_release);
  RUN(performer_articulation_changed_mid_note);
  RUN(performer_all_notes_off);
  RUN(performer_reset);
  RUN(performer_unknown_release_is_ignored);
  RUN(legato_slur_and_return);
  RUN(legato_string_crossing);
  RUN(legato_wide_leap_is_a_new_bow);
  RUN(legato_release_of_an_older_key);
  RUN(legato_more_keys_than_it_remembers);
  RUN(legato_release_of_a_key_not_sounding);
  RUN(legato_switched_mid_phrase);
  RUN(router_fuzz);
  std::printf("%d of %d checks failed\n", failures, checks);
  return failures ? 1 : 0;
}
