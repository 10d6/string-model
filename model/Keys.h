// Which keys are down, and where each key event goes (no synthkit, so the
// tests can drive it). The synth's processEvents asks this for every note-on
// and note-off before anything else sees it.
//
// Two listeners hear the keys:
// - the instrument (Performer): a monophonic player that merges channels. It
//   must hear a key go up only when the key is really up (no press of it left
//   on any channel), and must never hear a key go up that it didn't hear go
//   down, or the two disagree and a note sticks or stops early.
// - the kit's voices (SilentVoice): silent trackers for the page's lit keys and
//   voice count. The kit pairs each note-off with the oldest held voice of the
//   same note and channel, so per channel they must get one note-off for each
//   note-on they got, and none for a note-on they didn't.
//
// The rules, per key (MIDI note number) and channel (the kit's voices are
// paired per channel, so every count here is too):
// - Keyswitch: a press in the keyswitch range while keyswitches are on. Neither
//   listener hears it, nor its release, even if Keyswitches is turned off
//   while the key is down (it was decided at the press).
// - Doubled: a press of a key already down on the same channel, within
//   kDoubled of that press: the same message arriving twice (a keyboard the
//   browser sees as two MIDI inputs, PR #25). Neither listener hears it; one
//   of the key's later releases on that channel is swallowed to match.
// - Play: anything else. Both hear it. A second press of a key that is down
//   (a sequencer's overlapping notes, the same note on another channel)
//   restrikes the note; the instrument hears the release only when the last
//   press of that key goes up, the voices hear every release.
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
    Ignore,     // nobody hears it (a doubled press or its release; an unknown release)
    Voices,     // the voices only (a release of one press of a key still down)
    Both,       // the instrument and the voices
  };

  static bool inKeyswitchRange(int note) { return note >= kKeyswitchLow && note < kKeyswitchLow + kKeyswitchCount; }

  // A note-on at time t (s, any steady clock) on MIDI channel 0..15.
  Route press(int note, int channel, double t, bool keyswitchesOn) {
    if (!valid(note)) return Route::Ignore;
    Key& k = key_[note];
    Channel& c = k.ch[channel & 15];
    if (keyswitchesOn && inKeyswitchRange(note)) {
      if (c.switches < 255) ++c.switches;
      return Route::Keyswitch;
    }
    if (c.plays > 0 && t - c.lastPress < kDoubled) {
      if (c.doubles < 255) ++c.doubles;
      return Route::Ignore;
    }
    if (c.plays < 255) ++c.plays, ++k.plays;
    c.lastPress = t;
    return Route::Both;
  }

  // A note-off. The press it ends, on its channel, is chosen in this order: a
  // keyswitch, then a played press (so a doubled pair lets go at the first
  // release), then a doubled one.
  Route release(int note, int channel) {
    if (!valid(note)) return Route::Ignore;
    Key& k = key_[note];
    Channel& c = k.ch[channel & 15];
    if (c.switches > 0) {
      --c.switches;
      return Route::Keyswitch;
    }
    if (c.plays > 0) {
      --c.plays;
      --k.plays;
      return k.plays > 0 ? Route::Voices : Route::Both;
    }
    if (c.doubles > 0) --c.doubles;
    return Route::Ignore;  // a doubled press's release, or a key never pressed (or forgotten by all notes off)
  }

  // All notes off / Stop all sound: every key counts as up.
  void clear() {
    for (Key& k : key_) k = Key{};
  }

  // Tests: is this key down (played, on any channel; not a keyswitch)?
  bool down(int note) const { return valid(note) && key_[note].plays > 0; }
  int downCount() const {
    int n = 0;
    for (const Key& k : key_) n += k.plays > 0;
    return n;
  }

 private:
  struct Channel {
    uint8_t plays = 0, doubles = 0, switches = 0;
    double lastPress = -1.0;
  };
  struct Key {
    Channel ch[16];
    int plays = 0;  // over every channel
  };
  static bool valid(int note) { return note >= 0 && note < 128; }
  Key key_[128];
};

}  // namespace sm
