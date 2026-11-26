// Which keys are down, and where each key event goes (no synthkit, so the
// tests can drive it). The synth's processEvents asks this for every note-on
// and note-off before anything else sees it.
//
// Two listeners hear the keys:
// - the instrument (Performer): a monophonic player. It must hear a key go
//   up when the key is up, and never hear a key go up that it didn't hear go
//   down, or the two disagree and a note sticks or stops early.
// - the kit's voices (SilentVoice): silent trackers for the page's lit keys and
//   voice count. The kit pairs each note-off with the oldest held voice of the
//   same note and channel, so they must get exactly one note-off on a press's
//   channel for each note-on they got.
//
// A key (MIDI note number, any channel) is down or up: a real key can't go
// down twice without coming up. The rules:
// - Keyswitch: a press in the keyswitch range while keyswitches are on (of a
//   key that isn't down as a note). Neither listener hears it, nor its
//   release, even if Keyswitches is turned off while the key is down (it was
//   decided at the press, per channel).
// - Doubled: a press of a key that is down, within kDoubled of its press, on
//   any channel: the same press arriving twice (a keyboard the browser sees
//   as two MIDI inputs, PR #25; a keyboard layering two channels). Neither
//   listener hears it; its release, on its channel, is swallowed to match.
// - Repress: a press of a key that is down, later than that: its release
//   never arrived (lost on the way, the page's lit key says so too), or two
//   sources overlap the same note (a sequencer). The earlier press ends: the
//   voices let go of it (on its channel) and take the new one, the instrument
//   restrikes; a release then ends the note, and the earlier press's own
//   release, if it ever comes, is swallowed. The synth reports it (the key
//   check): pressing a stuck key again clears it.
// - Play: anything else. Both hear it.
// - All notes off (CC123) and Stop all sound (CC120) forget every key: the kit
//   lets its voices go too, and the keys' later releases go nowhere.
#pragma once

#include <cstdint>

namespace sm {

class Keys {
 public:
  static constexpr int kKeyswitchLow = 24;  // C1 .. B1
  static constexpr int kKeyswitchCount = 12;
  static constexpr double kDoubled = 0.03;  // s

  enum class Route : uint8_t {
    Keyswitch,  // a keyswitch press (apply it) or release: nobody hears it
    Ignore,     // nobody hears it (a doubled press or its release; a stale or unknown release)
    Both,       // the instrument and the voices
    Repress,    // a press of a key still down: the voices let go of the earlier press first (replacedChannel)
  };

  static bool inKeyswitchRange(int note) { return note >= kKeyswitchLow && note < kKeyswitchLow + kKeyswitchCount; }

  // A note-on at time t (s, any steady clock) on MIDI channel 0..15. For
  // Repress, *replacedChannel is the channel of the press it ends.
  Route press(int note, int channel, double t, bool keyswitchesOn, int* replacedChannel = nullptr) {
    if (!valid(note)) return Route::Ignore;
    Key& k = key_[note];
    const int ch = channel & 15;
    // A key down as a note stays a note: pressing it again is a re-press
    // (which clears it if it's stuck), even if Keyswitches came on meanwhile.
    if (!k.down && keyswitchesOn && inKeyswitchRange(note)) {
      k.switches[ch] = true;  // a doubled keyswitch's second release is ignored anyway
      return Route::Keyswitch;
    }
    if (k.down && t - k.lastPress < kDoubled) {
      if (k.doubles[ch] < 255) ++k.doubles[ch];
      return Route::Ignore;
    }
    const Route r = k.down ? Route::Repress : Route::Both;
    if (r == Route::Repress) {
      if (replacedChannel) *replacedChannel = k.channel;
      ++represses_;
    }
    k.down = true;
    k.channel = static_cast<uint8_t>(ch);
    k.lastPress = t;
    k.switches[ch] = false;  // one key can't be a keyswitch and a note: that release was lost
    return r;
  }

  // A note-off. On its channel, it ends (in this order) a keyswitch, the
  // press that holds the key down, or a doubled press; anything else (the
  // release of a press a Repress ended, a key never pressed) is ignored.
  Route release(int note, int channel) {
    if (!valid(note)) return Route::Ignore;
    Key& k = key_[note];
    const int ch = channel & 15;
    if (k.switches[ch]) {
      k.switches[ch] = false;
      return Route::Keyswitch;
    }
    if (k.down && k.channel == ch) {
      k.down = false;
      return Route::Both;
    }
    if (k.doubles[ch] > 0) --k.doubles[ch];
    return Route::Ignore;
  }

  // All notes off / Stop all sound: every key counts as up.
  void clear() {
    for (Key& k : key_) k = Key{};
  }

  bool down(int note) const { return valid(note) && key_[note].down; }
  int downCount() const {
    int n = 0;
    for (const Key& k : key_) n += k.down;
    return n;
  }
  // Presses the voices are holding (one per key down): what the page's voice
  // count should show, up to the kit's voice limit.
  int voices() const { return downCount(); }
  // Represses so far (a release that never arrived, or overlapping sources).
  int represses() const { return represses_; }

  // The listeners agree: every key the instrument holds is down here.
  // Returns the first that isn't (a note that would stick), or -1. P is the
  // Performer (heldCount, heldNote); a template keeps this header free of it.
  template <class P>
  int strayHeld(const P& performer) const {
    for (int i = 0; i < performer.heldCount(); ++i)
      if (!down(performer.heldNote(i))) return performer.heldNote(i);
    return -1;
  }

 private:
  struct Key {
    bool down = false;
    uint8_t channel = 0;  // the channel of the press holding it down
    double lastPress = -1.0;
    uint8_t doubles[16]{};
    bool switches[16]{};  // held as a keyswitch, per channel
  };
  static bool valid(int note) { return note >= 0 && note < 128; }
  Key key_[128];
  int represses_ = 0;
};

}  // namespace sm
