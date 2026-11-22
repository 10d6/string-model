// One bowed string as a digital waveguide split at the bow (KB §6.1, §10).
//
//   nut/finger  <-- dN -->  [bow junction]  <-- dB -->  bridge
//
// Four one-way fractional delay lines carry velocity waves. Losses are lumped
// into the two terminations: -g * one-pole low-pass at the nut (or finger),
// and g * one-pole low-pass on the wave arriving at the bridge, which then
// meets the moving bridge (Bridge.h): reflected r = u - a.
// Torsion is folded into an effective impedance at the bow
// (1/Zeff = 1/Z0 + 1/ZR, KB §3.6). The torsional share of the bow's push is a
// high-passed resistance: torsion has fixed ends too, so it can't absorb a
// steady push (a pure resistance would let the bow spin the string forever).
// The high-pass sits near the torsional fundamental's range [T]; only
// f/(2 Z0) enters the transverse waves.
#pragma once

#include <algorithm>
#include <cmath>

#include "Delay.h"
#include "Friction.h"

namespace sm {

struct StringDef {
  const char* name;
  int openNote;   // MIDI note of the open string
  float z0;       // transverse wave impedance (kg/s)
  float zr;       // torsional impedance seen at the surface (kg/s)
};

// Lumped losses for a string family [T]: decay of the low partials, and the
// low-passes that make high partials decay faster (and round the corner).
struct LossDef {
  float openT60;     // s, low partials of an open string on the instrument
  float stoppedT60;  // s, finger-stopped (finger is far lossier than the nut)
  float bridgeHz;    // bridge-end low-pass
  float nutHz;       // open-string nut low-pass
  float fingerHz;    // stopping-finger low-pass
};

enum class FrictionModel { Thermal, Classic };

class BowedString {
 public:
  void prepare(float fs, float lowestHz) {
    fs_ = fs;
    const int maxLen = static_cast<int>(fs / lowestHz) + 16;
    for (Delay* d : {&toNut_, &fromNut_, &toBridge_, &fromBridge_}) d->prepare(maxLen);
    thermal.prepare(fs);
    smooth_ = 1.f - std::exp(-1.f / (0.001f * fs));  // 1 ms for delay-length steps
    reset();
  }

  void setDef(const StringDef& s, const LossDef& l) {
    def_ = s;
    loss_ = l;
  }
  const StringDef& def() const { return def_; }

  void reset() {
    for (Delay* d : {&toNut_, &fromNut_, &toBridge_, &fromBridge_}) d->clear();
    nutLP_.y = bridgeLP_.y = 0.f;
    torX_ = torY_ = 0.f;
    thermal.reset();
    classic.reset();
    level_ = 0.f;
    quiet_ = 0;
    asleep_ = true;
    first_ = true;
  }

  // Called at control rate. hz: sounding pitch (with vibrato). noteHz: the
  // pitch without vibrato, which fixes the bridge-side length (vibrato moves
  // only the finger, KB §11.2). beta: relative bow–bridge distance.
  void setGeometry(float hz, float noteHz, float beta, bool stopped) {
    const float t60 = stopped ? loss_.stoppedT60 : loss_.openT60;
    nutLP_.setCutoff(stopped ? loss_.fingerHz : loss_.nutHz, fs_);
    bridgeLP_.setCutoff(loss_.bridgeHz, fs_);
    const float period = fs_ / hz;
    const float filt = nutLP_.delayAt(hz, fs_) + bridgeLP_.delayAt(hz, fs_);
    const float net = std::max(period - filt, 8.f);
    const float netNote = std::max(fs_ / noteHz - filt, 8.f);
    float dB = 0.5f * beta * netNote;
    dB = std::clamp(dB, kMinD, 0.5f * net - kMinD);
    const float dN = 0.5f * net - dB;
    tdN_ = dN;
    tdB_ = dB;
    if (first_) {
      dN_ = tdN_;
      dB_ = tdB_;
      first_ = false;
    }
    betaActual_ = dB / (dN + dB);
    // Torsion high-pass y = a (y1 + x - x1), corner at 2 x the pitch [T].
    torA_ = std::exp(-2.f * 3.14159265f * 2.f * hz / fs_);
    // Loop gain for the wanted T60 of the low partials. The low-passes take a
    // little more from higher fundamentals, so high notes decay faster (as
    // with a constant Q). The gain is NOT raised to make up for that: the
    // loop's gain at DC (g) must not exceed the fundamental's, or the
    // waveguide holds a DC wave (a uniform slope a fixed string can't have)
    // that the bow's one-way drag feeds and that barely decays.
    const float gLoop = std::pow(10.f, -3.f / (t60 * hz));
    gNut_ = gBridge_ = std::sqrt(gLoop);
  }

  float betaActual() const { return betaActual_; }
  bool asleep() const { return asleep_; }
  float temperature() const { return thermal.temperature(); }
  bool slipping() const { return slipping_; }

  // One sample on a rigid bridge (the labs): step() then finish(0).
  float process(float vb, float fb, FrictionModel model, float noiseAmt, float noise) {
    if (asleep_) {
      if (fb <= 0.f) return 0.f;
      wake();
    }
    step(vb, fb, model, noiseAmt, noise);
    return finish(0.f);
  }

  void wake() {
    if (!asleep_) return;
    asleep_ = false;
    quiet_ = 0;
  }

  // First half of a sample: the bow junction and the nut end. Returns the
  // velocity wave arriving at the bridge (after the string's own losses);
  // the caller solves the bridge and calls finish() with its velocity.
  // vb: bow velocity (m/s), fb: bow force (N, 0 = bow off).
  float step(float vb, float fb, FrictionModel model, float noiseAmt, float noise) {
    dN_ += (tdN_ - dN_) * smooth_;
    dB_ += (tdB_ - dB_) * smooth_;

    const float vinN = fromNut_.read(dN_);
    const float vinB = fromBridge_.read(dB_);
    const float atNut = toNut_.read(dN_);
    const float atBridge = toBridge_.read(dB_);

    // Contact velocity = vh + f/(2 Z0) + HP(f)/(2 ZR); the HP's instantaneous
    // part folds into Zeff, its memory into the history term.
    const float vh = vinN + vinB + torA_ * (torY_ - torX_) / (2.f * def_.zr);
    const float zeff = 1.f / (1.f / def_.z0 + torA_ / def_.zr);
    JunctionResult r;
    if (fb <= 0.f) {
      // Bow off: the junction is transparent; the contact just cools.
      r.v = vh;
      thermal.coolIdle();
    } else if (model == FrictionModel::Thermal) {
      r = thermal.solve(vh, vb, fb, zeff, noiseAmt, noise);
      thermal.update(r, vb, fb);
    } else {
      r = classic.solve(vh, vb, fb, zeff, noiseAmt, noise);
      thermal.update(r, vb, fb);  // keep the temperature meaningful for switching
    }
    slipping_ = r.slipping;
    torY_ = torA_ * (torY_ + r.f - torX_);
    torX_ = r.f;
    const float inc = r.f / (2.f * def_.z0);

    const float reflNut = -gNut_ * nutLP_.process(atNut);
    toBridge_.write(vinN + inc);
    toNut_.write(vinB + inc);
    fromNut_.write(reflNut);

    arrive_ = gBridge_ * bridgeLP_.process(atBridge);
    lv_ = std::fabs(atBridge) + std::fabs(atNut);
    fb_ = fb;
    return arrive_;
  }

  // Second half: the bridge moves at u (m/s). Reflects r = u - a and returns
  // the force on the bridge, Z (a - r) = Z (2a - u).
  float finish(float u) {
    const float refl = u - arrive_;
    fromBridge_.write(refl);
    const float force = def_.z0 * (arrive_ - refl);
    // Sleep when the bow is off and the string has died away.
    level_ = std::max(lv_, level_ * 0.9995f);
    if (fb_ <= 0.f && level_ < 1e-7f) {
      if (++quiet_ > 2048) {
        for (Delay* d : {&toNut_, &fromNut_, &toBridge_, &fromBridge_}) d->clear();
        nutLP_.y = bridgeLP_.y = 0.f;
        torX_ = torY_ = 0.f;
        asleep_ = true;
      }
    } else {
      quiet_ = 0;
    }
    return force;
  }

  float z0() const { return def_.z0; }
  // Recent peak wave amplitude at the ends (m/s), 0 when asleep.
  float level() const { return asleep_ ? 0.f : level_; }

  ThermalFriction thermal;
  ClassicFriction classic;

 private:
  static constexpr float kMinD = 2.f;
  Delay toNut_, fromNut_, toBridge_, fromBridge_;
  OnePoleLP nutLP_, bridgeLP_;
  StringDef def_{"", 69, 0.2f, 0.7f};
  LossDef loss_{2.f, 1.f, 7000.f, 9000.f, 4000.f};
  float fs_ = 96000.f, smooth_ = 0.01f, torA_ = 0.99f, torX_ = 0.f, torY_ = 0.f;
  float dN_ = 100.f, dB_ = 10.f, tdN_ = 100.f, tdB_ = 10.f, betaActual_ = 0.1f;
  float gNut_ = 0.99f, gBridge_ = 0.99f, level_ = 0.f, arrive_ = 0.f, lv_ = 0.f, fb_ = 0.f;
  int quiet_ = 0;
  bool asleep_ = true, first_ = true, slipping_ = false;
};

}  // namespace sm
