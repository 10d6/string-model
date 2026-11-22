// The gesture layer (KB §7.3, §11): turns keys and controllers into bow and
// finger motion for the physics.
//
// - Monophonic, last-note priority. Overlapping keys slur (same bow, finger
//   change); a new key after all keys are up is a new stroke in the other
//   bow direction. With legato off every key is a bow change (détaché).
// - Playability assistant (KB §11.5): a dynamics value picks v_b/beta from the
//   measured level law, splits it into v_b and beta, and sets the force on the
//   players' line through the Schelleng diagram, F ∝ v_b * beta^-1.39 * Z^1.39.
//   Pressure moves along the force axis; contact point moves beta.
// - Attacks: the bow lands (force rises over ~8 ms) and accelerates at
//   a = k * F (Guettler plane, KB §7.2); velocity raises k and adds an accent.
// - Bow changes reverse v_b at a limited acceleration with a force dip; the
//   bow position is tracked so long notes change bow at the tip/frog.
// - Vibrato on stopped notes only, delayed and ramped in, tied to dynamics.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "defs/Violin.h"

namespace sm {

struct PerformerControls {
  float dynamics = 0.55f;     // 0..1
  float velSens = 0.7f;       // velocity -> dynamics
  float pressure = 0.f;       // -1..1 (log2 force offset / 1.5)
  float contact = 0.f;        // -1 tasto .. +1 ponticello
  float bite = 0.3f;          // 0..1 attack harshness
  bool legato = true;
  float bowChange = 0.06f;    // s, nominal reversal time at mf
  float vibDepth = 16.f;      // cents, peak-to-peak at mf
  float vibRate = 5.5f;       // Hz
  float vibDelay = 0.25f;     // s
  float shiftTime = 0.04f;    // s
  float bend = 0.f;           // semitones
  float a4 = 440.f;
};

struct StringTarget {
  float hz = 440.f;        // sounding pitch incl. vibrato and bend
  float noteHz = 440.f;    // without vibrato (sets bridge-side length)
  bool stopped = false;
  float force = 0.f;       // N
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

  void reset() {
    nHeld_ = 0;
    note_ = -1;
    bowing_ = false;
    vb_ = 0.f;
    dir_ = -1.f;
    bowPos_ = 0.15f;
    for (int s = 0; s < 4; ++s) {
      force_[s] = 0.f;
      lift_[s] = true;
      fingerNote_[s] = static_cast<float>(def_->strings[s].openNote);
      fingerFrom_[s] = fingerTo_[s] = fingerNote_[s];
      shiftT_[s] = 1.f;
      stopped_[s] = false;
    }
    str_ = 2;
    vibPhase_ = 0.f;
    noteAge_ = 0.f;
    accent_ = 0.f;
    rng_ = 0x12345678u;
  }

  // Keys ---------------------------------------------------------------
  void noteOn(int note, float vel, const PerformerControls& c) {
    removeHeld(note);
    if (nHeld_ < kMaxHeld) {
      held_[nHeld_] = note;
      vel_[nHeld_] = vel;
      ++nHeld_;
    }
    const bool slur = bowing_ && c.legato && note_ >= 0;
    play(note, vel, slur, c);
  }

  void noteOff(int note, const PerformerControls& c) {
    const bool wasTop = nHeld_ > 0 && held_[nHeld_ - 1] == note;
    removeHeld(note);
    if (!wasTop) return;
    if (nHeld_ > 0) {
      // Back to the previous held key, slurred.
      play(held_[nHeld_ - 1], vel_[nHeld_ - 1], c.legato, c, true);
    } else {
      bowing_ = false;  // lift the bow; the string rings on
      lift_[str_] = true;
    }
  }

  void allNotesOff() {
    nHeld_ = 0;
    bowing_ = false;
    for (bool& l : lift_) l = true;
  }

  // Per sample: bow kinematics and forces. Returns the bow velocity.
  float tickSample(const PerformerControls& c) {
    (void)c;
    const float dt = 1.f / fs_;
    // Bow velocity: approach dir * vbTarget at a limited acceleration.
    const float want = bowing_ ? dir_ * vbT_ : vb_ * 0.999f;
    const float accel = bowing_ ? accel_ : 0.5f;
    const float dv = want - vb_;
    const float step = accel * dt;
    vb_ += std::clamp(dv, -step, step);
    // Bow position along the hair; change bow at the ends.
    bowPos_ += vb_ * dt / def_->bowLength;
    if (bowing_) {
      if (dir_ > 0 && bowPos_ > 0.97f) bowChange(c);
      else if (dir_ < 0 && bowPos_ < 0.03f) bowChange(c);
    }
    bowPos_ = std::clamp(bowPos_, 0.f, 1.f);
    // Force per string: landing ramp, change dip, accent; lift ramps to 0.
    const float speedFrac = vbT_ > 1e-4f ? std::min(1.f, std::fabs(vb_) / vbT_) : 1.f;
    // Force dips through a reversal: players lighten the bow, which shortens the
    // gap and cleans the re-attack (KB §11.5: dip 0.6 -> 0.1 N, gap 28 -> 16 ms).
    const float dip = 0.2f + 0.8f * speedFrac * speedFrac;
    accent_ *= accentDecay_;
    const float fTarget = fT_ * dip * (1.f + accent_);
    for (int s = 0; s < 4; ++s) {
      const float goal = (!lift_[s] && bowing_ && s == str_) ? fTarget : 0.f;
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
    return vb_;
  }

  // At control rate: pitch, vibrato and force targets per string.
  void strings(StringTarget out[4], const PerformerControls& c) const {
    for (int s = 0; s < 4; ++s) {
      const float t = shiftT_[s];
      const float sc = t * t * (3.f - 2.f * t);  // S-curve shift
      float n = fingerFrom_[s] + (fingerTo_[s] - fingerFrom_[s]) * sc;
      if (s == str_ && note_ >= 0) n += c.bend;
      const bool stopped = stopped_[s] || (s == str_ && std::fabs(c.bend) > 0.01f);
      const float noteHz = mtof(n, c.a4);
      float vib = 0.f;
      if (stopped && s == str_) vib = vibCents_ * std::sin(6.2831853f * vibPhase_);
      out[s].noteHz = noteHz;
      out[s].hz = noteHz * std::pow(2.f, vib / 1200.f);
      out[s].stopped = stopped;
      out[s].force = force_[s];
    }
  }

  float beta() const { return betaT_; }
  float bowVelocity() const { return vb_; }
  int currentString() const { return str_; }
  bool bowing() const { return bowing_; }
  float bowPosition() const { return bowPos_; }

  // At control rate: vibrato depth envelope.
  void controlTick(const PerformerControls& c, float dtControl) {
    (void)dtControl;
    computeTargets(c);
    const float d = dyn_;
    const float age = noteAge_ - c.vibDelay;
    const float ramp = age <= 0.f ? 0.f : std::min(1.f, age / 0.35f);
    // Peak-to-peak depth given; amplitude is half. Louder = wider, faster.
    vibCents_ = 0.5f * c.vibDepth * (0.6f + 0.8f * d) * ramp * vibDepthJit_;
    vibRateNow_ = c.vibRate * (0.9f + 0.2f * d) * vibRateJit_;
  }

  static float mtof(float n, float a4) { return a4 * std::pow(2.f, (n - 69.f) / 12.f); }

 private:
  static constexpr int kMaxHeld = 16;

  void removeHeld(int note) {
    int j = 0;
    for (int i = 0; i < nHeld_; ++i)
      if (held_[i] != note) {
        held_[j] = held_[i];
        vel_[j] = vel_[i];
        ++j;
      }
    nHeld_ = j;
  }

  int chooseString(int note, bool slur) const {
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

  void play(int note, float vel, bool slur, const PerformerControls& c, bool returning = false) {
    const int s = chooseString(note, slur);
    const float prevNote = note_ >= 0 ? static_cast<float>(note_) : static_cast<float>(note);
    vel_now_ = vel;
    note_ = note;
    const float n = static_cast<float>(std::max(note, def_->strings[0].openNote - 7));
    const bool open = note == def_->strings[s].openNote;
    // Finger: shift on the same string while sounding; jump otherwise.
    const bool sounding = force_[s] > 0.f || (s == str_ && bowing_);
    if (sounding && slur) {
      fingerFrom_[s] = currentFinger(s);
      fingerTo_[s] = n;
      const float interval = std::fabs(n - prevNote);
      shiftDur_[s] = 0.008f + c.shiftTime * std::min(1.f, interval / 5.f);
      shiftT_[s] = 0.f;
    } else {
      fingerFrom_[s] = fingerTo_[s] = n;
      shiftT_[s] = 1.f;
    }
    stopped_[s] = !open;
    if (s != str_) {
      lift_[str_] = true;  // string crossing: leave the old string ringing
      str_ = s;
    }
    lift_[s] = false;
    computeTargets(c);
    if (!slur) {
      noteAge_ = 0.f;
      // Guettler plane: acceleration proportional to force, steeper with velocity.
      attackK_ = (6.f + 8.f * vel) * (1.f + 1.5f * c.bite);
      accent_ = std::max(0.f, vel - 0.55f) * (0.6f + 1.2f * c.bite);
      if (bowing_) {
        bowChange(c);
      } else {
        // From the air: start the stroke in the direction with more bow left.
        dir_ = bowPos_ > 0.6f ? -1.f : (bowPos_ < 0.4f ? 1.f : -dir_);
        vb_ = 0.f;
        bowing_ = true;
      }
    } else if (!returning && std::fabs(vb_) < 1e-3f) {
      bowing_ = true;
    }
    (void)returning;
  }

  float currentFinger(int s) const {
    const float t = shiftT_[s];
    const float sc = t * t * (3.f - 2.f * t);
    return fingerFrom_[s] + (fingerTo_[s] - fingerFrom_[s]) * sc;
  }

  void bowChange(const PerformerControls& c) {
    dir_ = -dir_;
    // Reversal time ~ bowChange at mf; acceleration so that 2*vb/a matches.
    const float a = 2.f * std::max(vbT_, 0.05f) / std::max(c.bowChange, 0.01f);
    accelChange_ = a;
    changing_ = true;
  }

  void computeDyn(const PerformerControls& c) {
    dyn_ = std::clamp(c.dynamics + c.velSens * (vel_now_ - 0.6f), 0.f, 1.f);
  }

  void computeTargets(const PerformerControls& c) {
    computeDyn(c);
    const float d = dyn_;
    // Level law: L ≈ 7.7 ln(vb/beta) dB; pp..ff spans ~23 dB here.
    const float ratio = 0.6f * std::exp(3.0f * d);
    const float vb = 0.10f * std::exp(1.4f * d);
    float beta = vb / ratio;
    beta *= std::pow(2.f, -1.5f * c.contact);
    beta = std::clamp(beta, 0.015f, 0.3f);
    const float z = def_->strings[str_].z0;
    float f = def_->forceRef * (vb / def_->vbRef) * std::pow(beta / def_->betaRef, -1.39f) *
              std::pow(z / def_->zRef, 1.39f);
    f *= std::pow(2.f, 1.5f * c.pressure);
    f = std::min(f, def_->forceMax);
    vbT_ = vb;
    betaT_ = beta;
    fT_ = f;
    // Acceleration: attack (k * F) until near target speed, then gentle;
    // reversals use the bow-change acceleration.
    const float attackA = std::clamp(attackK_ * f, 1.f, 30.f);
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
  float fs_ = 48000.f;
  int held_[kMaxHeld]{};
  float vel_[kMaxHeld]{};
  int nHeld_ = 0, note_ = -1, str_ = 2;
  float vel_now_ = 0.7f;
  bool bowing_ = false, changing_ = false;
  float vb_ = 0.f, dir_ = -1.f, bowPos_ = 0.15f;
  float vbT_ = 0.2f, betaT_ = 0.08f, fT_ = 0.4f, dyn_ = 0.5f;
  float accel_ = 5.f, accelChange_ = 5.f, attackK_ = 10.f, accent_ = 0.f;
  float landRate_ = 0.01f, liftRate_ = 0.01f, accentDecay_ = 0.999f;
  float force_[4]{};
  bool lift_[4]{true, true, true, true};
  bool stopped_[4]{};
  float fingerNote_[4]{}, fingerFrom_[4]{}, fingerTo_[4]{}, shiftT_[4]{1, 1, 1, 1}, shiftDur_[4]{0.03f, 0.03f, 0.03f, 0.03f};
  float noteAge_ = 0.f, vibPhase_ = 0.f, vibCents_ = 0.f, vibRateNow_ = 5.5f, vibRateJit_ = 1.f, vibDepthJit_ = 1.f;
  uint32_t rng_ = 1;
};

}  // namespace sm
