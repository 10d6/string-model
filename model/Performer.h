// The gesture layer (KB §7.3, §11): turns keys and controllers into bow and
// finger motion for the physics.
//
// - Monophonic, last-note priority. Overlapping keys slur (same bow, finger
//   change); a new key after all keys are up is a new stroke in the other
//   bow direction. With legato off every key is a bow change (détaché).
// - Keys pressed together (within the chord window) become a double stop on
//   two adjacent strings sharing the bow, or a rolled chord for three or four
//   (KB §11.2: the lower pair first, then onto the upper pair).
// - Playability assistant (KB §11.5): a dynamics value picks v_b/beta from the
//   measured level law, splits it into v_b and beta, and sets the force on the
//   players' line through the Schelleng diagram, F ∝ v_b * beta^-1.39 * Z^1.39.
//   Pressure moves along the force axis; contact point moves beta. High
//   positions get more force (the body loads short, high strings: F_min rises).
// - Attacks: the bow lands (force rises over ~8 ms) and accelerates at
//   a = k * F (Guettler plane, KB §7.2); velocity raises k and adds an accent.
// - Bow changes reverse v_b at a limited acceleration with a force dip; the
//   bow position is tracked so long notes change bow at the tip/frog.
// - Vibrato on stopped, bowed strings, delayed and ramped in, tied to dynamics.
// - Fingers leave strings the bow has left (after a moment), so those strings
//   ring open and in sympathy through the bridge.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Bridge.h"
#include "defs/Violin.h"

namespace sm {

struct PerformerControls {
  float dynamics = 0.55f;     // 0..1
  float velSens = 0.7f;       // velocity -> dynamics
  float pressure = 0.f;       // -1..1 (log2 force offset / 1.5)
  float contact = 0.f;        // -1 tasto .. +1 ponticello
  float bite = 0.3f;          // 0..1 attack harshness
  bool legato = true;
  float bowChange = 0.02f;    // s, reversal time (full speed one way to the other)
  float vibDepth = 16.f;      // cents, peak-to-peak at mf
  float vibRate = 5.5f;       // Hz
  float vibDelay = 0.25f;     // s
  float shiftTime = 0.04f;    // s
  float bend = 0.f;           // semitones
  float a4 = 440.f;
  float changeFloor = 0.2f;   // force at zero bow speed during a reversal, x target [T]
  float changeWidth = 1.f;    // force is back to full at this fraction of the target speed [T]
  float sustainSpeed = 0.6f;  // long notes: bow speed after the attack, x attack speed
  bool autoChange = true;     // change bow at the tip/frog; off = an endless bow
  float sustainBetaMin = 0.06f;  // long notes move toward the bridge no closer than this [T]
  float minAccel = 2.5f;      // m/s^2, floor on attack acceleration: below ~2 soft notes take 0.25 s to speak [T]
  float chordWindow = 0.035f; // s: keys pressed within this are one chord
  float rollTime = 0.06f;     // s per string pair when rolling a 3-4 note chord
  float fingerLift = 0.25f;   // s after the bow leaves a string before the finger lifts [T]
  // How a note ends when the last key goes up (KB §11.2 "Note release"):
  // 0 Auto (by key-up speed: quick = stop, gentle = lift), 1 Lift (the bow
  // leaves; the string rings), 2 Stop (the bow stops on the string and damps
  // it), 3 Finger Off (the bow leaves, then the finger: the note falls to the
  // open string).
  int releaseMode = 0;
  int forcedString = -1;      // -1 auto, else 0..3: play on this string when it reaches the note
  float slideNoise = 0.4f;    // 0..1: winding noise under a sliding finger
  // Articulation for new notes (KB §11.2, §11.5): 0 Arco, 1 Martelé,
  // 2 Spiccato, 3 Tremolo, 4 Pizzicato, 5 Harmonics.
  int articulation = 0;
  float tremoloRate = 12.2f;  // bow reversals per second (VSCO solo violin median, KB §11.2)
};

enum Articulation { kArco = 0, kMartele, kSpiccato, kTremolo, kPizzicato, kHarmonics };

struct StringTarget {
  float hz = 440.f;        // sounding pitch incl. vibrato and bend
  float noteHz = 440.f;    // without vibrato (sets bridge-side length)
  bool stopped = false;
  float force = 0.f;       // N
  float damp = 0.f;        // 0..1: the bow resting on the string as a damper (a "stop" release)
  float slide = 0.f;       // m/s: the stopping finger's speed along the string (shifts)
  float touchQ = 0.f;      // harmonics: touch point, fraction of the vibrating length from the finger end (0 = none)
  float touchR = 0.f;      // touch resistance / Z0
};

class Performer {
 public:
  void prepare(float fs, const InstrumentDef& def) {
    fs_ = fs;
    def_ = &def;
    landRate_ = 1.f - std::exp(-1.f / (0.008f * fs_));
    liftRate_ = 1.f - std::exp(-1.f / (0.025f * fs_));
    accentDecay_ = std::exp(-1.f / (0.07f * fs_));
    reset();
  }
  void setInstrument(const InstrumentDef& def) { def_ = &def; }
  // The bridge, for the body's load on each note (see updateForceFactors).
  void setBridge(const Bridge* b) { bridge_ = b; }

  void reset() {
    nHeld_ = 0;
    note_ = -1;
    bowing_ = false;
    vb_ = 0.f;
    dir_ = -1.f;
    bowPos_ = 0.15f;
    for (int s = 0; s < 4; ++s) {
      force_[s] = 0.f;
      bowed_[s] = false;
      fingerFrom_[s] = fingerTo_[s] = static_cast<float>(def_->strings[s].openNote);
      shiftT_[s] = 1.f;
      stopped_[s] = false;
      offTime_[s] = 0.f;
      fingerOffAt_[s] = 0.f;
      fFactor_[s] = 1.f;
      pluckStart_[s] = -1.f;
      pluckNow_[s] = 0.f;
      touchQ_[s] = 0.f;
    }
    art_ = kArco;
    muting_ = false;
    tremPhase_ = 0.f;
    str_ = 2;
    chordN_ = 0;
    stopping_ = false;
    damp_ = 0.f;
    primed_ = false;
    vibPhase_ = 0.f;
    noteAge_ = 0.f;
    now_ = 0.f;
    accent_ = 0.f;
    rng_ = 0x12345678u;
  }

  // Keys ---------------------------------------------------------------
  void noteOn(int note, float vel, const PerformerControls& c) {
    prime(c);
    removeHeld(note);
    if (nHeld_ < kMaxHeld) {
      held_[nHeld_] = note;
      vel_[nHeld_] = vel;
      press_[nHeld_] = now_;
      ++nHeld_;
    }
    // A note after a "stop" release is a new stroke from rest.
    const bool wasStopping = stopping_;
    stopping_ = false;
    damp_ = 0.f;
    for (float& t : fingerOffAt_) t = 0.f;
    muting_ = false;
    // Keys pressed together: a double stop or a rolled chord (a strum, plucked).
    if (nHeld_ >= 2 && tryChord(c)) return;
    chordN_ = 0;
    if (c.articulation == kPizzicato) {
      art_ = kPizzicato;
      pluckNote(note, vel, c, 0.f);
      return;
    }
    // Martelé and spiccato strokes are separate by nature; tremolo and
    // harmonics slur like arco.
    const bool canSlur = c.articulation == kArco || c.articulation == kTremolo || c.articulation == kHarmonics;
    const bool slur = bowing_ && c.legato && note_ >= 0 && !wasStopping && canSlur && art_ == c.articulation;
    if (!slur) art_ = c.articulation;
    play(note, vel, slur, c);
  }

  // relVel: key-up velocity 0..1 (keyboards without it send 64/127).
  void noteOff(int note, float relVel, const PerformerControls& c) {
    const bool wasTop = nHeld_ > 0 && held_[nHeld_ - 1] == note;
    removeHeld(note);
    if (chordN_ > 0) {
      int k = -1;
      for (int i = 0; i < chordN_; ++i)
        if (chordNote_[i] == note) k = i;
      if (k < 0) return;
      // Drop that note from the chord; the bow stays on the rest.
      bowed_[chordStr_[k]] = false;
      for (int i = k; i + 1 < chordN_; ++i) {
        chordNote_[i] = chordNote_[i + 1];
        chordStr_[i] = chordStr_[i + 1];
      }
      --chordN_;
      if (chordN_ >= 2) {
        rollIdx_ = chordN_ - 2;
        applyChordContact();
      } else if (chordN_ == 1) {
        str_ = chordStr_[0];
        note_ = chordNote_[0];
        chordN_ = 0;
        for (int s = 0; s < 4; ++s) bowed_[s] = s == str_;
        updateForceFactors();
      } else {
        chordN_ = 0;
        release(relVel, c);
      }
      return;
    }
    if (!wasTop) return;
    if (nHeld_ > 0) {
      // Back to the previous held key, slurred.
      play(held_[nHeld_ - 1], vel_[nHeld_ - 1], c.legato, c);
    } else {
      release(relVel, c);
    }
  }

  void allNotesOff() {
    nHeld_ = 0;
    chordN_ = 0;
    stopping_ = false;
    damp_ = 0.f;
    lift();
  }

  // Per sample: bow kinematics and forces. Returns the bow velocity.
  float tickSample(const PerformerControls& c) {
    const float dt = 1.f / fs_;
    now_ += dt;
    // Bow velocity: approach dir * vbTarget at a limited acceleration. A
    // "stop" release brings the bow to rest on the string in ~40 ms, then
    // lifts it.
    float want = bowing_ ? dir_ * vbT_ : vb_ * 0.999f;
    float accel = bowing_ ? accel_ : 0.5f;
    if (stopping_) {
      want = 0.f;
      accel = std::max(vbT_, 0.05f) / 0.04f;
      // Once the bow has (nearly) stopped, it rests on the string as a damper
      // (over ~15 ms), then lifts after 150 ms.
      if (std::fabs(vb_) < 0.1f * std::max(vbT_, 0.05f)) damp_ = std::min(1.f, damp_ + dt / 0.015f);
      if (std::fabs(vb_) < 1e-3f && now_ - stopStart_ > 0.15f) {
        stopping_ = false;
        lift();
      }
    } else if (muting_) {
      // The hand damps a plucked string, then lets go after 200 ms.
      damp_ = std::min(1.f, damp_ + dt / 0.015f);
      if (now_ - muteStart_ > 0.2f) muting_ = false;
    } else if (damp_ > 0.f) {
      damp_ = std::max(0.f, damp_ - dt / 0.02f);
    }
    // Bow-stroke templates (Demoucron, KB §11.5) set the bow's speed directly.
    templateForce_ = -1.f;
    if (bowing_ && (art_ == kMartele || art_ == kSpiccato || art_ == kTremolo)) strokeTemplate(c, dt, want);
    if (templateForce_ >= 0.f) {
      vb_ = want;  // the template's speed is already smooth
    } else {
      const float dv = want - vb_;
      const float step = accel * dt;
      vb_ += std::clamp(dv, -step, step);
    }
    // Bow position along the hair; change bow at the ends.
    bowPos_ += vb_ * dt / def_->bowLength;
    if (!c.autoChange) bowPos_ = std::clamp(bowPos_, 0.1f, 0.9f);  // never runs out
    if (bowing_ && c.autoChange && art_ == kArco) {
      if (dir_ > 0 && bowPos_ > 0.97f) bowChange(c);
      else if (dir_ < 0 && bowPos_ < 0.03f) bowChange(c);
    }
    bowPos_ = std::clamp(bowPos_, 0.f, 1.f);
    // Rolled chords move onto the next pair of strings.
    if (chordN_ >= 3 && rollIdx_ < chordN_ - 2 && now_ - rollStart_ >= c.rollTime * static_cast<float>(rollIdx_ + 1)) {
      ++rollIdx_;
      applyChordContact();
    }
    // Force per string: landing ramp, change dip, accent; lift ramps to 0.
    const float speedFrac = vbT_ > 1e-4f ? std::min(1.f, std::fabs(vb_) / vbT_) : 1.f;
    // Force dips through a reversal: players lighten the bow, which shortens the
    // gap and cleans the re-attack (KB §11.5: dip 0.6 -> 0.1 N, gap 28 -> 16 ms).
    const float sf = std::min(1.f, speedFrac / c.changeWidth);
    // (A stop holds its force: the stationary bow is what damps the string.)
    const float dip = stopping_ ? 1.f : c.changeFloor + (1.f - c.changeFloor) * sf * sf;
    accent_ *= accentDecay_;
    const float fTarget = fT_ * dip * (1.f + accent_);
    for (int s = 0; s < 4; ++s) {
      const float base = templateForce_ >= 0.f ? templateForce_ : fTarget;
      const float goal = (bowed_[s] && bowing_) ? std::min(base * fFactor_[s], def_->forceMax) : 0.f;
      const float rate = goal > force_[s] ? landRate_ : liftRate_;
      force_[s] += (goal - force_[s]) * rate;
      if (force_[s] < 1e-4f && goal == 0.f) force_[s] = 0.f;
    }
    noteAge_ += dt;
    vibPhase_ += vibRateNow_ * dt;
    if (vibPhase_ >= 1.f) {
      vibPhase_ -= 1.f;
      // Slow random drift of rate and depth per cycle (KB §11.2).
      vibRateJit_ = 1.f + 0.05f * bipolar();
      vibDepthJit_ = 1.f + 0.15f * bipolar();
    }
    for (int s = 0; s < 4; ++s)
      if (shiftT_[s] < 1.f) shiftT_[s] = std::min(1.f, shiftT_[s] + dt / shiftDur_[s]);
    // Plucks: the finger loads the string (force rises over 10-25 ms), then
    // slips off (falls in 1-2.5 ms) (KB §11.2 "Pizzicato").
    for (int s = 0; s < 4; ++s) {
      pluckNow_[s] = 0.f;
      if (pluckStart_[s] < 0.f || now_ < pluckStart_[s]) continue;
      const float t = now_ - pluckStart_[s];
      if (t < pluckRise_[s]) {
        const float x = std::sin(1.5707963f * t / pluckRise_[s]);
        pluckNow_[s] = pluckF_[s] * x * x;
      } else if (t < pluckRise_[s] + pluckRel_[s]) {
        const float x = std::cos(1.5707963f * (t - pluckRise_[s]) / pluckRel_[s]);
        pluckNow_[s] = pluckF_[s] * x * x;
      } else {
        pluckStart_[s] = -1.f;
      }
    }
    return vb_;
  }

  // Per sample: the plucking finger's force on string s (N).
  float pluckForce(int s) const { return pluckNow_[s] * pluckDir_[s]; }

  // At control rate: pitch, vibrato and force targets per string.
  void strings(StringTarget out[4], const PerformerControls& c) const {
    for (int s = 0; s < 4; ++s) {
      const bool lead = bowed_[s] && (bowing_ || s == str_);
      float n = currentFinger(s);
      if (lead && note_ >= 0) n += c.bend;
      const bool stopped = stopped_[s] || (lead && std::fabs(c.bend) > 0.01f);
      // Open strings are tuned a little sharp at rest (InstrumentDef): the bow
      // pulls them down. Plucked, there is no bow, so they sound at pitch.
      const float tune = stopped || art_ == kPizzicato ? 0.f : def_->openTuneCents[s] * 0.01f;
      const float noteHz = mtof(n + tune, c.a4);
      float vib = 0.f;
      if (stopped && (lead || s == str_)) vib = vibCents_ * std::sin(6.2831853f * vibPhase_);
      out[s].noteHz = noteHz;
      out[s].hz = noteHz * std::pow(2.f, vib / 1200.f);
      out[s].stopped = stopped;
      out[s].force = force_[s];
      out[s].damp = (bowed_[s] || s == str_) ? damp_ : 0.f;
      out[s].touchQ = touchQ_[s];
      out[s].touchR = touchQ_[s] > 0.f ? kTouchR : 0.f;
      // Finger speed along the string during a shift: x = L (1 - 2^(-p/12)),
      // p semitones above the open string; dn/dt from the S-curve.
      out[s].slide = 0.f;
      if (shiftT_[s] < 1.f && stopped) {
        const float t = shiftT_[s];
        const float dndt = (fingerTo_[s] - fingerFrom_[s]) * 6.f * t * (1.f - t) / shiftDur_[s];
        const float pos = n - static_cast<float>(def_->strings[s].openNote);
        const float dxdn = def_->scaleLength * 0.0577623f * std::pow(2.f, -pos / 12.f);  // L ln2/12 2^(-p/12)
        out[s].slide = std::fabs(dxdn * dndt);
      }
    }
  }

  float beta() const { return betaT_; }
  float forceOn(int s) const { return force_[s]; }
  float bowVelocity() const { return vb_; }
  int currentString() const { return str_; }
  int chordSize() const { return chordN_; }
  bool bowing() const { return bowing_; }
  bool bowedString(int s) const { return bowed_[s]; }
  float bowPosition() const { return bowPos_; }

  // At control rate: smoothing, targets, vibrato envelope, finger lifts.
  void controlTick(const PerformerControls& c, float dtControl) {
    // Smooth the continuous controls (~15 ms) on this fixed clock.
    prime(c);
    const float k = 1.f - std::exp(-dtControl / 0.015f);
    dynIn_ += (c.dynamics - dynIn_) * k;
    pressureIn_ += (c.pressure - pressureIn_) * k;
    contactIn_ += (c.contact - contactIn_) * k;
    computeTargets(c);
    const float d = dyn_;
    const float age = noteAge_ - c.vibDelay;
    const float ramp = age <= 0.f ? 0.f : std::min(1.f, age / 0.35f);
    // Peak-to-peak depth given; amplitude is half. Louder = wider, faster.
    vibCents_ = 0.5f * c.vibDepth * (0.6f + 0.8f * d) * ramp * vibDepthJit_;
    vibRateNow_ = c.vibRate * (0.9f + 0.2f * d) * vibRateJit_;
    // Fingers leave strings the bow has left, so they ring open (a "finger
    // off" release lifts the playing finger too, soon after the bow).
    for (int s = 0; s < 4; ++s) {
      const bool due = fingerOffAt_[s] > 0.f && now_ >= fingerOffAt_[s] && !(bowed_[s] && bowing_);
      if (due) fingerOffAt_[s] = 0.f;
      if (!stopped_[s] || (!due && (bowed_[s] || s == str_))) continue;
      if (due || now_ - offTime_[s] >= c.fingerLift) {
        stopped_[s] = false;
        fingerFrom_[s] = fingerTo_[s] = static_cast<float>(def_->strings[s].openNote);
        shiftT_[s] = 1.f;
      }
    }
  }

  static float mtof(float n, float a4) { return a4 * std::pow(2.f, (n - 69.f) / 12.f); }

 private:
  static constexpr int kMaxHeld = 16;

  // The smoothed controls start at their first values (no glide from defaults).
  void prime(const PerformerControls& c) {
    if (primed_) return;
    dynIn_ = c.dynamics;
    pressureIn_ = c.pressure;
    contactIn_ = c.contact;
    primed_ = true;
  }

  void removeHeld(int note) {
    int j = 0;
    for (int i = 0; i < nHeld_; ++i)
      if (held_[i] != note) {
        held_[j] = held_[i];
        vel_[j] = vel_[i];
        press_[j] = press_[i];
        ++j;
      }
    nHeld_ = j;
  }

  // The last key went up: end the note the chosen way.
  void release(float relVel, const PerformerControls& c) {
    int mode = c.releaseMode;
    if (mode == 0) mode = relVel > 0.7f ? 2 : 1;  // a quick key-up stops the bow on the string
    if (art_ == kPizzicato) {
      // A plucked note rings on; "stop" damps it with the hand.
      if (mode == 2) {
        muting_ = true;
        muteStart_ = now_;
      }
      if (mode == 3)
        for (int s = 0; s < 4; ++s)
          if (stopped_[s]) fingerOffAt_[s] = now_ + 0.04f;
      return;
    }
    if (mode == 2 && bowing_) {
      stopping_ = true;
      stopStart_ = now_;
      return;
    }
    lift();
    if (mode == 3)
      for (int s = 0; s < 4; ++s)
        if (stopped_[s]) fingerOffAt_[s] = now_ + 0.04f;  // the finger leaves just after the bow
  }

  void lift() {
    bowing_ = false;  // the bow leaves the string(s); they ring on
    for (int s = 0; s < 4; ++s)
      if (bowed_[s]) {
        bowed_[s] = false;
        offTime_[s] = now_;
      }
  }

  void setBowed(int s, bool on) {
    if (bowed_[s] && !on) offTime_[s] = now_;
    bowed_[s] = on;
  }

  // Keys pressed within the chord window of the newest key, lowest first.
  // Assign them to adjacent strings (lowest positions); false if they can't
  // be played as one chord.
  bool tryChord(const PerformerControls& c) {
    int notes[4], n = 0;
    float vel = 0.f;
    for (int i = nHeld_ - 1; i >= 0 && n < 4; --i)
      if (now_ - press_[i] <= c.chordWindow) {
        notes[n++] = held_[i];
        vel = std::max(vel, vel_[i]);
      }
    if (n < 2) return false;
    std::sort(notes, notes + n);
    for (int i = 1; i < n; ++i)
      if (notes[i] == notes[i - 1]) return false;
    // The whole chord if it fits on adjacent strings, else its top notes.
    int best = -1;
    while (n >= 2) {
      int bestCost = 1 << 20;
      for (int s0 = 0; s0 + n <= 4; ++s0) {
        int lo = 99, hi = -99;
        bool ok = true;
        for (int j = 0; j < n && ok; ++j) {
          const int pos = notes[j] - def_->strings[s0 + j].openNote;
          if (pos < 0 || pos > 14) ok = false;
          lo = std::min(lo, pos);
          hi = std::max(hi, pos);
        }
        if (!ok || hi - lo > 7) continue;  // the hand spans about a fifth
        const int cost = hi;               // lowest positions win
        if (cost < bestCost) bestCost = cost, best = s0;
      }
      if (best >= 0) break;
      for (int j = 0; j + 1 < n; ++j) notes[j] = notes[j + 1];  // drop the lowest
      --n;
    }
    if (best < 0) return false;
    const bool wasChord = chordN_ > 0;
    // A chord started from silence (the first of its keys began this stroke)
    // keeps that stroke; otherwise it's a new one, like any key.
    const bool fresh = !bowing_ || noteAge_ <= c.chordWindow + 0.005f;
    for (int s = 0; s < 4; ++s) setBowed(s, false);
    chordN_ = n;
    for (int j = 0; j < n; ++j) {
      const int s = best + j;
      chordStr_[j] = s;
      chordNote_[j] = notes[j];
      fingerFrom_[s] = fingerTo_[s] = static_cast<float>(notes[j]);
      shiftT_[s] = 1.f;
      stopped_[s] = notes[j] != def_->strings[s].openNote;
    }
    note_ = notes[n - 1];
    str_ = chordStr_[n - 1];
    vel_now_ = vel;
    for (float& q : touchQ_) q = 0.f;
    if (c.articulation == kPizzicato) {
      // A plucked chord is strummed, low to high, ~12 ms apart.
      art_ = kPizzicato;
      for (int j = 0; j < n; ++j) startPluck(chordStr_[j], vel, 0.012f * static_cast<float>(j));
      chordN_ = 0;
      lift();
      return true;
    }
    art_ = c.articulation == kHarmonics ? kArco : c.articulation;  // chords: no harmonics
    rollIdx_ = 0;
    rollStart_ = now_;
    applyChordContact();
    if (!fresh && !wasChord) startStroke(vel, c, true);
    else if (!bowing_) startStroke(vel, c, false);
    else computeTargets(c);
    return true;
  }

  // Which strings the bow touches: both of a double stop; for 3-4 notes, the
  // pair at rollIdx_ (lower pair first, then onto the upper pair).
  void applyChordContact() {
    for (int s = 0; s < 4; ++s) setBowed(s, false);
    if (chordN_ == 2) {
      setBowed(chordStr_[0], true);
      setBowed(chordStr_[1], true);
    } else if (chordN_ >= 3) {
      setBowed(chordStr_[rollIdx_], true);
      setBowed(chordStr_[rollIdx_ + 1], true);
    }
    updateForceFactors();
  }

  int chooseString(int note, bool slur, int forced) const {
    // A chosen string (keyswitch / String parameter) plays every note it
    // reaches: from its open note up two octaves.
    if (forced >= 0 && forced < 4) {
      const int open = def_->strings[forced].openNote;
      if (note >= open && note <= open + 24) return forced;
    }
    // Stay on the current string through a slur if it reaches within 7 semitones.
    if (slur) {
      const int open = def_->strings[str_].openNote;
      if (note >= open && note <= open + 7) return str_;
    }
    int s = 0;
    for (int i = 0; i < 4; ++i)
      if (def_->strings[i].openNote <= note) s = i;
    return s;
  }

  void play(int note, float vel, bool slur, const PerformerControls& c) {
    int s = chooseString(note, slur, c.forcedString);
    const float prevNote = note_ >= 0 ? static_cast<float>(note_) : static_cast<float>(note);
    vel_now_ = vel;
    note_ = note;
    float n = static_cast<float>(std::max(note, def_->strings[0].openNote - 7));
    // Harmonics: play the note as a natural or artificial harmonic; the
    // finger stops the fundamental and a light touch picks the partial.
    for (float& q : touchQ_) q = 0.f;
    if (art_ == kHarmonics) {
      int hs, stop;
      float q;
      if (findHarmonic(note, c.forcedString, hs, stop, q)) {
        s = hs;
        n = static_cast<float>(stop);
        touchQ_[s] = q;
      }
    }
    const bool open = static_cast<int>(n) == def_->strings[s].openNote;
    // Finger: shift on the same string while sounding; jump otherwise.
    const bool sounding = force_[s] > 0.f || (s == str_ && bowing_);
    if (sounding && slur) {
      fingerFrom_[s] = currentFinger(s);
      fingerTo_[s] = n;
      const float interval = std::fabs(n - prevNote);
      // A gentle key slides slowly and expressively, a hard one shifts quickly
      // (x1.4 at velocity 0.2, x1 at 0.5, x0.4 at 1).
      const float speed = std::clamp(1.6f - 1.2f * vel, 0.4f, 1.4f);
      shiftDur_[s] = 0.008f + c.shiftTime * std::min(1.f, interval / 5.f) * speed;
      shiftT_[s] = 0.f;
    } else {
      fingerFrom_[s] = fingerTo_[s] = n;
      shiftT_[s] = 1.f;
    }
    stopped_[s] = !open;
    str_ = s;
    // The bow is on this string only (a crossing leaves the others ringing).
    for (int k = 0; k < 4; ++k) setBowed(k, k == s);
    updateForceFactors();
    if (!slur) startStroke(vel, c, bowing_);
    else computeTargets(c);
  }

  // A new stroke: a bow change if the bow is moving, else from the air.
  void startStroke(float vel, const PerformerControls& c, bool change) {
    noteAge_ = 0.f;
    // Guettler plane: acceleration proportional to force, steeper with velocity.
    attackK_ = (6.f + 8.f * vel) * (1.f + 1.5f * c.bite);
    accent_ = std::max(0.f, vel - 0.55f) * (0.6f + 1.2f * c.bite);
    if (change && bowing_) {
      bowChange(c);
    } else {
      // From the air: start the stroke in the direction with more bow left.
      dir_ = bowPos_ > 0.6f ? -1.f : (bowPos_ < 0.4f ? 1.f : -dir_);
      vb_ = 0.f;
      bowing_ = true;
    }
    computeTargets(c);
  }

  // Force scale per bowed string, relative to the assistant's base force:
  // string impedance (F ∝ Z^1.39 on the players' line), more force above a
  // fifth up the string (short, high strings: the body loads them, F_min
  // rises) [T], and more on each string when two share the bow: two strings
  // driving one bridge load each other (e.g. A4's 3rd harmonic is E5's 2nd),
  // which raises F_min (measured with instrument_lab chord) [T].
  // Notes whose fundamental sits on a body resonance need more force too
  // (the body absorbs, F_min rises, KB §5.4): log2 F += 1.5 * 25 Z0 |Y(f0)|,
  // fitted with tools/calibrate_force.py (C#5 on the 550 Hz mode) [T].
  void updateForceFactors() {
    int count = 0;
    for (int s = 0; s < 4; ++s) count += bowed_[s];
    for (int s = 0; s < 4; ++s) {
      const float z = def_->strings[s].z0;
      const float ratio = std::pow(2.f, (fingerTo_[s] - static_cast<float>(def_->strings[s].openNote)) / 12.f);
      const float pos = std::pow(std::max(1.f, ratio / 1.5f), 0.7f);
      float body = 1.f;
      if (bridge_) {
        const float hz = mtof(fingerTo_[s], 440.f);
        const float load = std::min(1.f, 25.f * z * bridge_->magnitude(hz));
        body = std::pow(2.f, 1.5f * load);
      }
      fFactor_[s] = std::pow(z / def_->zRef, 1.39f) * pos * body * def_->forceTrim[s] * (count >= 2 ? 1.0f : 1.f);
      // Harmonics: a much lighter bow; quarter touches lighter still. Through
      // the synth: octaves within ~5 cents, thirds/quarters within ~15 [T].
      if (touchQ_[s] > 0.f) fFactor_[s] *= touchQ_[s] < 0.3f ? 0.15f : 0.3f;
    }
  }

  float currentFinger(int s) const {
    const float t = shiftT_[s];
    const float sc = t * t * (3.f - 2.f * t);
    return fingerFrom_[s] + (fingerTo_[s] - fingerFrom_[s]) * sc;
  }

  // Bow-stroke templates, from Demoucron's measured strokes (KB §11.5).
  // Sets `want` (bow velocity) and templateForce_ (base force before the
  // per-string factors); t is the time since the stroke began.
  void strokeTemplate(const PerformerControls& c, float dt, float& want) {
    const float t = noteAge_, d = dyn_;
    auto sin2 = [](float x) { const float v = std::sin(1.5707963f * x); return v * v; };
    if (art_ == kMartele) {
      // Pinch with the bow still, then a fast stroke while the force drops
      // over 0.2 s, with the bow's ~13 Hz ripple; the bow stays on the string.
      const float fp = 0.8f + 1.9f * d, tp = 0.06f * d, vm = 0.5f + 0.7f * d, T = 0.25f + 0.08f * d, A = 0.42f;
      const float fmin = 0.25f + 0.2f * d;
      if (t < tp) {
        want = 0.f;
        templateForce_ = fp;
        return;
      }
      const float u = t - tp;
      float v = 0.f;
      if (u < A * T) v = vm * sin2(u / (A * T));
      else if (u < T) v = vm * (1.f - sin2((u - A * T) / ((1.f - A) * T)));
      else damp_ = std::min(1.f, damp_ + 2.f * dt / 0.015f);  // at rest on the string: it damps
      want = dir_ * v;
      const float drop = u < 0.2f ? 0.5f * (fp - fmin) * (1.f + std::cos(3.14159265f * u / 0.2f)) : 0.f;
      const float ripple = 0.15f * fp * std::exp(-u / 0.08f) * std::cos(6.2831853f * 13.f * u);
      templateForce_ = std::max(0.f, fmin + drop + ripple);
    } else if (art_ == kSpiccato) {
      // Thrown: a ~75 ms force bell (rise a little faster than the fall) with a
      // half-sine stroke, then in the air. Demoucron measured 0.5 N (p) to 3 N
      // (f) peaks; x0.4 maps them onto this string's force scale (at x1 the
      // notes went 20-40 cents flat, a sign of crushing) [T].
      const float tc = 0.075f, A = 0.45f, fm = 0.4f * (0.5f + 2.5f * std::pow(d, 1.5f));
      const float vm = 0.08f + 0.7f * std::pow(d, 1.2f), tb = 0.14f;
      float f = 0.f;
      if (t < A * tc) f = fm * sin2(t / (A * tc));
      else if (t < tc) f = fm * (1.f - sin2((t - A * tc) / ((1.f - A) * tc)));
      want = t < tb ? dir_ * vm * std::sin(3.14159265f * t / tb) : 0.f;
      templateForce_ = f;
    } else {
      // Tremolo: ~12 reversals/s, near-sinusoidal speed; down-bows lighter
      // than up-bows (0.4 vs 0.6 N measured); timing and force jitter per stroke.
      const float v0 = 0.15f + 0.35f * d;
      const float before = tremPhase_;
      tremPhase_ += 0.5f * c.tremoloRate * tremRateJit_ * dt;
      if ((before < 0.5f && tremPhase_ >= 0.5f) || tremPhase_ >= 1.f) {
        if (tremPhase_ >= 1.f) tremPhase_ -= 1.f;
        tremRateJit_ = 1.f + 0.12f * bipolar();
        tremForceJit_ = 1.f + 0.15f * bipolar();
      }
      const float v = v0 * std::sin(6.2831853f * tremPhase_);
      want = dir_ * v;
      templateForce_ = fT_ * (v * dir_ > 0.f ? 0.8f : 1.2f) * tremForceJit_;
    }
  }

  // Pluck note on its string (pizzicato): the finger stops it, the bow is away.
  void pluckNote(int note, float vel, const PerformerControls& c, float delay) {
    const int s = chooseString(note, false, c.forcedString);
    vel_now_ = vel;
    note_ = note;
    const float n = static_cast<float>(std::max(note, def_->strings[0].openNote - 7));
    fingerFrom_[s] = fingerTo_[s] = n;
    shiftT_[s] = 1.f;
    stopped_[s] = note != def_->strings[s].openNote;
    for (float& q : touchQ_) q = 0.f;
    str_ = s;
    noteAge_ = 0.f;
    lift();
    startPluck(s, vel, delay);
  }

  // Each pluck varies a little (KB §11.2: the same pluck every time sounds
  // mechanical): force, rise, release and the pluck point [T].
  void startPluck(int s, float vel, float delay) {
    pluckStart_[s] = now_ + delay;
    pluckF_[s] = (0.6f + 3.0f * std::pow(vel, 1.3f)) * (1.f + 0.1f * bipolar());
    pluckRise_[s] = 0.012f + 0.006f * (1.f + bipolar());
    // The release is a ramp: one as long as the period would cancel the
    // fundamental (a sinc null), which made open E plucks weak and uneven. Keep
    // it under ~0.35 period: 1-2.5 ms on the G string, ~0.5 ms on the E [T].
    const float period = 1.f / mtof(fingerTo_[s], 440.f);
    pluckRel_[s] = std::min(0.001f + 0.00075f * (1.f + bipolar()), (0.25f + 0.05f * (1.f + bipolar())) * period);
    pluckDir_[s] = 1.f;
    pluckBeta_ = 0.2f + 0.05f * bipolar();
  }

  // A harmonic for `note`: natural (an open string's 2nd, 3rd or 4th
  // partial, touched at 1/2, 1/3, 1/4) if there is one, else artificial
  // (a stopped note two octaves, a twelfth or an octave below, touched at
  // 1/4, 1/3 or 1/2 of the stopped length), lowest position first.
  bool findHarmonic(int note, int forced, int& s, int& stop, float& q) const {
    static constexpr int kInterval[3] = {12, 19, 24};
    static constexpr float kQ[3] = {0.5f, 1.f / 3.f, 0.25f};
    int bestCost = 1 << 20;
    for (int str = 0; str < 4; ++str) {
      if (forced >= 0 && str != forced) continue;
      const int open = def_->strings[str].openNote;
      for (int k = 0; k < 3; ++k) {
        const int st = note - kInterval[k];
        const int pos = st - open;
        if (pos < 0 || pos > 12) continue;
        // Natural first (cost by partial), then artificial, two-octave first.
        const int cost = pos == 0 ? k : 100 + pos * 4 + (2 - k);
        if (cost < bestCost) bestCost = cost, s = str, stop = st, q = kQ[k];
      }
    }
    return bestCost < (1 << 20);
  }

  void bowChange(const PerformerControls& c) {
    dir_ = -dir_;
    // Reversal time ~ bowChange; acceleration so that 2*vb/a matches.
    const float a = 2.f * std::max(vbT_, 0.05f) / std::max(c.bowChange, 0.005f);
    accelChange_ = a;
    changing_ = true;
  }

  void computeDyn(const PerformerControls& c) {
    dyn_ = std::clamp(dynIn_ + c.velSens * (vel_now_ - 0.6f), 0.f, 1.f);
  }

  void computeTargets(const PerformerControls& c) {
    computeDyn(c);
    const float d = dyn_;
    // Level law: L ≈ 7.7 ln(vb/beta) dB; pp..ff spans ~23 dB here.
    const float ratio = 0.6f * std::exp(3.0f * d);
    // Long notes save bow (KB §7.3 step 2; Demoucron: 12 cm/s on whole notes,
    // 54 on quarters): after the attack the bow slows to sustainSpeed and
    // moves toward the bridge, keeping v_b/beta (the loudness) the same.
    const float age = std::clamp((noteAge_ - 0.25f) / 0.6f, 0.f, 1.f);
    const float ease = age * age * (3.f - 2.f * age);
    float vb = 0.10f * std::exp(1.4f * d) * (1.f + (c.sustainSpeed - 1.f) * ease);
    vb = std::max(vb, ratio * 0.02f);  // beta can't go below 0.02: keep the loudness
    float beta = vb / ratio;
    // ...but a long note stays at a playable distance (Demoucron: 25-40 mm on
    // long strokes; re-attacks at bow changes get very hard near beta 1/25,
    // KB §7.2): below sustainBetaMin it gets a little softer instead.
    if (ease > 0.f) {
      const float attackBeta = 0.10f * std::exp(1.4f * d) / ratio;
      const float floorBeta = std::min(attackBeta, c.sustainBetaMin);
      beta = std::max(beta, floorBeta);
    }
    if (art_ == kSpiccato) beta = 0.18f - 0.1f * d;   // Demoucron: 58 / 40 / 29 mm at p / mf / f
    if (art_ == kPizzicato) beta = pluckBeta_;        // the pluck point
    // A touched string vibrates in k segments, like a string 1/k as long:
    // the bow goes k times closer to the bridge (relative), with less force
    // (updateForceFactors). Measured: 1.99-4.00 x f0, in tune.
    if (art_ == kHarmonics && touchQ_[str_] > 0.f) beta /= std::round(1.f / touchQ_[str_]);
    beta *= std::pow(2.f, -1.5f * contactIn_);
    beta = std::clamp(beta, 0.015f, 0.3f);
    // Base force on the players' line at the reference impedance; per-string
    // factors (impedance, position, double stops) are in fFactor_.
    float f = def_->forceRef * (vb / def_->vbRef) * std::pow(beta / def_->betaRef, -1.39f);
    // Soft playing sits near the bottom of the Helmholtz window in this model
    // (calibrate_force.py): lift it, most at pp [T].
    const float margin = 0.1f + 0.8f * (1.f - d) * (1.f - d);
    f *= std::pow(2.f, 1.5f * (pressureIn_ + margin));
    vbT_ = vb;
    betaT_ = beta;
    fT_ = f;
    // Acceleration: attack (k * F) until near target speed, then gentle;
    // reversals use the bow-change acceleration.
    const float fLead = std::min(f * fFactor_[str_], def_->forceMax);
    const float attackA = std::clamp(attackK_ * fLead, c.minAccel, 30.f);
    if (changing_) {
      accel_ = accelChange_;
      if (vb_ * dir_ > 0.9f * vbT_) changing_ = false;
    } else {
      accel_ = std::fabs(vb_) < 0.9f * vbT_ ? attackA : 2.f;
    }
  }

  float bipolar() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) * (2.f / 16777216.f) - 1.f;
  }

  const InstrumentDef* def_ = nullptr;
  const Bridge* bridge_ = nullptr;
  float fs_ = 48000.f;
  int held_[kMaxHeld]{};
  float vel_[kMaxHeld]{}, press_[kMaxHeld]{};
  int nHeld_ = 0, note_ = -1, str_ = 2;
  int chordN_ = 0, chordStr_[4]{}, chordNote_[4]{}, rollIdx_ = 0;
  float rollStart_ = 0.f;
  float vel_now_ = 0.7f;
  float dynIn_ = 0.55f, pressureIn_ = 0.f, contactIn_ = 0.f;  // smoothed controls
  bool primed_ = false;
  bool bowing_ = false, changing_ = false;
  float vb_ = 0.f, dir_ = -1.f, bowPos_ = 0.15f;
  float vbT_ = 0.2f, betaT_ = 0.08f, fT_ = 0.4f, dyn_ = 0.5f;
  float accel_ = 5.f, accelChange_ = 5.f, attackK_ = 10.f, accent_ = 0.f;
  float landRate_ = 0.01f, liftRate_ = 0.01f, accentDecay_ = 0.999f;
  float force_[4]{}, fFactor_[4]{1, 1, 1, 1}, offTime_[4]{}, fingerOffAt_[4]{};
  bool stopping_ = false, muting_ = false;
  float stopStart_ = 0.f, damp_ = 0.f, muteStart_ = 0.f;
  int art_ = kArco;
  float templateForce_ = -1.f, tremPhase_ = 0.f, tremRateJit_ = 1.f, tremForceJit_ = 1.f;
  float pluckStart_[4]{-1.f, -1.f, -1.f, -1.f}, pluckF_[4]{}, pluckRise_[4]{}, pluckRel_[4]{}, pluckDir_[4]{1, 1, 1, 1};
  float pluckNow_[4]{}, pluckBeta_ = 0.2f;
  float touchQ_[4]{};
  static constexpr float kTouchR = 4.f;  // a light touch: R = 4 Z (KB: 1-10 Z) [T]
  bool bowed_[4]{};
  bool stopped_[4]{};
  float fingerFrom_[4]{}, fingerTo_[4]{}, shiftT_[4]{1, 1, 1, 1}, shiftDur_[4]{0.03f, 0.03f, 0.03f, 0.03f};
  float noteAge_ = 0.f, now_ = 0.f, vibPhase_ = 0.f, vibCents_ = 0.f, vibRateNow_ = 5.5f, vibRateJit_ = 1.f,
        vibDepthJit_ = 1.f;
  uint32_t rng_ = 1;
};

}  // namespace sm
