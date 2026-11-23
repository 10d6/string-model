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
#include <array>
#include <cmath>

#include "Delay.h"
#include "Friction.h"

namespace sm {

struct StringDef {
  const char* name;
  int openNote;   // MIDI note of the open string
  float z0;       // transverse wave impedance (kg/s)
  float zr;       // torsional impedance seen at the surface (kg/s)
  // Scales the treble damping down (rates / hf): thicker, wound strings lose
  // their high partials faster (internal viscous loss grows with frequency
  // and diameter, KB §2.3) [T]. 1 = the plain E.
  float hf = 1.f;
};

// A string family's damping as decay rates (1/s, amplitude) that depend on a
// partial's frequency, not on the note (Valette-Cuesta, KB §2.3):
//   sigma(f) = low + (high - low) f^2 / (f^2 + fT^2)
// for the string itself, plus the stopping finger's on stopped notes. Fitted
// to the Philharmonia pizzicato's per-partial decay (tools/partial_q.py),
// the bridge and body's own losses included by the model [T].
struct LossDef {
  float lowRate;      // 1/s, the string's own, low partials
  float highRate;     // 1/s, the string's own, high partials (/ hf per string)
  float fT;           // Hz, where the rise is half-way
  float fingerLow;    // 1/s, added by a stopping finger, low partials
  float fingerHigh;   // 1/s, added by a stopping finger, high partials
  float bridgeShare;  // of the string's own loss, at the bridge end (the bow-bridge segment is short)
  float topHz;        // one-pole low-pass at the bridge (x hf): viscous loss keeps rising above the
                      // measured range (> 8 kHz), and it rounds the Helmholtz corner [T]
};

enum class FrictionModel { Thermal, Classic, Enhanced };

class BowedString {
 public:
  void prepare(float fs, float lowestHz) {
    fs_ = fs;
    const int maxLen = static_cast<int>(fs / lowestHz) + 16;
    for (Delay* d : lines()) d->prepare(maxLen);
    thermal.prepare(fs);
    thermal2.prepare(fs);
    smooth_ = 1.f - std::exp(-1.f / (0.001f * fs));  // 1 ms for delay-length steps
    touchGlide_ = 1.f - std::exp(-1.f / (0.01f * fs));  // ~10 ms touch on/off (KB: 5-20 ms)
    reset();
  }

  void setDef(const StringDef& s, const LossDef& l) {
    def_ = s;
    loss_ = l;
  }
  const StringDef& def() const { return def_; }

  void reset() {
    for (Delay* d : lines()) d->clear();
    nutLoss_.clear();
    bridgeLoss_.clear();
    topLP_.y = 0.f;
    torX_ = torY_ = torX2_ = torY2_ = 0.f;
    touchR_ = 0.f;
    thermal.reset();
    thermal2.reset();
    classic.reset();
    classic2.reset();
    level_ = 0.f;
    quiet_ = 0;
    asleep_ = true;
    first_ = true;
  }

  // Called at control rate. hz: sounding pitch (with vibrato). noteHz: the
  // pitch without vibrato, which fixes the bridge-side length (vibrato moves
  // only the finger, KB §11.2). beta: relative bow–bridge distance.
  // slide: finger speed (m/s) during a shift; a sliding finger presses
  // lighter, so the stop is lossier [T].
  void setGeometry(float hz, float noteHz, float beta, bool stopped, float slide = 0.f) {
    // Losses per round trip (1/hz s): each end's shelf takes its share of the
    // rates. A sliding finger presses lighter, so the stop is lossier [T].
    const float hi = loss_.highRate / std::max(def_.hf, 0.1f);
    const float sb = loss_.bridgeShare;
    const float fk = stopped ? 1.f + 1.5f * slide : 0.f;
    const float nLow = (1.f - sb) * loss_.lowRate + fk * loss_.fingerLow;
    const float nHigh = (1.f - sb) * hi + fk * loss_.fingerHigh;
    nutLoss_.set(std::exp(-nLow / hz), std::exp(-nHigh / hz), loss_.fT, fs_);
    bridgeLoss_.set(std::exp(-sb * loss_.lowRate / hz), std::exp(-sb * hi / hz), loss_.fT, fs_);
    const float period = fs_ / hz;
    topLP_.setCutoff(std::min(loss_.topHz * def_.hf, 0.45f * fs_), fs_);
    const float filt = nutLoss_.delayAt(hz, fs_) + bridgeLoss_.delayAt(hz, fs_) + topLP_.delayAt(hz, fs_);
    const float net = std::max(period - filt, 8.f);
    const float netNote = std::max(fs_ / noteHz - filt, 8.f);
    float dB = 0.5f * beta * netNote;
    dB = std::clamp(dB, kMinD, 0.5f * net - kMinD);
    const float dN = 0.5f * net - dB;
    tdN_ = dN;
    tdB_ = dB;
    // The touch point (harmonics) splits the finger side at q of the
    // vibrating length from the finger/nut end: the round trip finger ->
    // touch -> finger, including the nut filter's delay, is q of the period,
    // so the touched partial is in tune. Off, the split just sits mid-way.
    const float tauN = nutLoss_.delayAt(hz, fs_);
    tdT_ = std::clamp(touchQ_ > 0.f ? 0.5f * (noteHz > 0.f ? fs_ / noteHz : period) * touchQ_ - 0.5f * tauN : 0.5f * dN, kMinD,
                      dN - kMinD);
    if (first_) {
      dN_ = tdN_;
      dB_ = tdB_;
      dT_ = tdT_;
      first_ = false;
    }
    betaActual_ = dB / (dN + dB);
    // Torsion high-pass y = a (y1 + x - x1), corner at 2 x the pitch [T].
    torA_ = std::exp(-2.f * 3.14159265f * 2.f * hz / fs_);
    // The shelves' gain falls with frequency, so the loop's gain at DC never
    // exceeds the fundamental's: a DC wave (a uniform slope a fixed string
    // can't have, that the bow's one-way drag would feed) decays at the low
    // rate.
  }

  // A light finger touch (harmonics, KB §11.2): at q of the vibrating length
  // from the finger end (1/2, 1/3, 1/4...), with resistance rOverZ * Z0 (a
  // light touch is about 1-10 Z, KB [T]); 0 = no touch. The resistance glides
  // in over ~10 ms. Takes effect with the next setGeometry().
  void setTouch(float q, float rOverZ) {
    touchQ_ = q;
    touchTarget_ = rOverZ;
  }

  // Finite bow width (KB §3.5): the hair ribbon touches the string over a few
  // mm, so the bow is two contact points `samples` apart (each with its own
  // friction and contact temperature, half the force each). They can slip at
  // different moments (differential slipping), which adds the ribbon's noise
  // and roughness. 0 = one point.
  void setBowWidth(int samples, float share = 0.8f) {
    width_ = samples > 0 ? std::max(samples, 2) : 0;
    share_ = share;
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
    step(vb, fb, model, noiseAmt, noise, 0.f);
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
  // damp (0..1): the bow resting on the string as a lossy contact (hair
  // compliance and rosin) instead of a friction junction: a resistance
  // R = damp * 2 Z0 at the bow point (2 Z0 absorbs best). A stopped bow damps
  // the string (KB §11.2); a rigid stick would only pin it.
  // pluck (N): an external force at the junction point with the bow off (a
  // plucking finger, KB §11.2 "Pizzicato").
  float step(float vb, float fb, FrictionModel model, float noiseAmt, float noise, float damp = 0.f, float pluck = 0.f) {
    dN_ += (tdN_ - dN_) * smooth_;
    dB_ += (tdB_ - dB_) * smooth_;
    dT_ += (tdT_ - dT_) * smooth_;
    dT_ = std::clamp(dT_, kMinD, dN_ - kMinD);
    const float dBT = dN_ - dT_;
    touchR_ += (touchTarget_ - touchR_) * touchGlide_;

    const int w = width_ > 0 && dB_ > static_cast<float>(width_) + kMinD ? width_ : 0;
    const float dBB = dB_ - static_cast<float>(w);  // bridge side of the second point
    const float vinN = fromTouch_.read(dBT);       // from the touch point toward the bow
    const float atTouchB = toTouch_.read(dBT);     // from the bow, arriving at the touch point
    const float atNut = touchToNut_.read(dT_);
    const float atTouchN = nutToTouch_.read(dT_);  // from the nut/finger, arriving at the touch point
    const float vinB = w ? fromB2_.tap(w) : fromBridge_.read(dB_);  // toward the (first) bow point
    const float vin2N = w ? toB2_.tap(w) : 0.f;    // second point: from the first
    const float vin2B = w ? fromBridge_.read(dBB) : 0.f;
    const float atBridge = toBridge_.read(dBB);

    // Contact velocity = vh + f/(2 Z0) + HP(f)/(2 ZR); the HP's instantaneous
    // part folds into Zeff, its memory into the history term.
    const float zeff = 1.f / (1.f / def_.z0 + torA_ / def_.zr);
    const float vh = vinN + vinB + torA_ * (torY_ - torX_) / (2.f * def_.zr);
    const float f1 = w ? fb * share_ : fb;
    JunctionResult r;
    if (damp > 0.f) {
      // Point resistor: f = -R v, v = vh + f/(2 Zeff)  =>  v = vh / (1 + R/(2 Zeff)).
      const float R = damp * 2.f * def_.z0;
      r.v = vh / (1.f + R / (2.f * zeff));
      r.f = -R * r.v;
      thermal.coolIdle();
    } else if (fb <= 0.f) {
      // Bow off: the junction is transparent (or a plucking finger pushes);
      // the contact just cools.
      r.f = pluck;
      r.v = vh + pluck / (2.f * zeff);
      thermal.coolIdle();
    } else {
      r = solve(thermal, classic, model, vh, vb, f1, zeff, noiseAmt, noise);
    }
    slipping_ = r.slipping;
    torY_ = torA_ * (torY_ + r.f - torX_);
    torX_ = r.f;
    const float inc = r.f / (2.f * def_.z0);
    if (w) {
      // The second point: the rest of the force, its own noise sample.
      const float vh2 = vin2N + vin2B + torA_ * (torY2_ - torX2_) / (2.f * def_.zr);
      JunctionResult r2;
      if (fb > 0.f && damp <= 0.f) {
        const float n2 = noise * 7.31f + 0.37f;
        r2 = solve(thermal2, classic2, model, vh2, vb, fb - f1, zeff, noiseAmt, n2 - std::floor(n2));
      } else {
        r2.v = vh2;
        thermal2.coolIdle();
      }
      // (Slip is reported for the first point: the labs count slips per period.)
      torY2_ = torA_ * (torY2_ + r2.f - torX2_);
      torX2_ = r2.f;
      const float inc2 = r2.f / (2.f * def_.z0);
      toB2_.write(vinN + inc);
      toTouch_.write(vinB + inc);
      toBridge_.write(vin2N + inc2);
      fromB2_.write(vin2B + inc2);
    } else {
      toBridge_.write(vinN + inc);
      toTouch_.write(vinB + inc);
      toB2_.write(0.f);
      fromB2_.write(0.f);
    }

    const float reflNut = -nutLoss_.process(atNut) + fingerNoise_;
    // Touch junction: a resistance R between the two sides,
    // v = 2Z (a1 + a2) / (2Z + R); each side gets v - its own incoming wave
    // (R = 0: transparent).
    const float R = touchR_ * def_.z0;
    const float vt = 2.f * def_.z0 * (atTouchB + atTouchN) / (2.f * def_.z0 + R);
    touchToNut_.write(vt - atTouchN);
    nutToTouch_.write(reflNut);
    fromTouch_.write(vt - atTouchB);

    arrive_ = topLP_.process(bridgeLoss_.process(atBridge));
    lv_ = std::fabs(atBridge) + std::fabs(atNut);
    fb_ = fb;
    pluck_ = pluck;
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
    if (fb_ <= 0.f && pluck_ == 0.f && level_ < 1e-7f) {
      if (++quiet_ > 2048) {
        for (Delay* d : lines()) d->clear();
        nutLoss_.clear();
        bridgeLoss_.clear();
        topLP_.y = 0.f;
        torX_ = torY_ = torX2_ = torY2_ = 0.f;
        asleep_ = true;
      }
    } else {
      quiet_ = 0;
    }
    return force;
  }

  float z0() const { return def_.z0; }
  // A sliding finger rubbing the winding: velocity (m/s) added at the finger
  // end this sample.
  void setFingerNoise(float v) { fingerNoise_ = v; }
  // Recent peak wave amplitude at the ends (m/s), 0 when asleep.
  float level() const { return asleep_ ? 0.f : level_; }

  ThermalFriction thermal, thermal2;  // the two contact points (thermal2: bow width)
  ClassicFriction classic, classic2;

 private:
  static JunctionResult solve(ThermalFriction& th, ClassicFriction& cl, FrictionModel model, float vh, float vb, float fb, float zeff,
                              float noiseAmt, float noise) {
    JunctionResult r;
    if (model == FrictionModel::Thermal) {
      r = th.solve(vh, vb, fb, zeff, noiseAmt, noise);
    } else if (model == FrictionModel::Enhanced) {
      r = th.solveEnhanced(vh, vb, fb, zeff, noiseAmt, noise);
    } else {
      r = cl.solve(vh, vb, fb, zeff, noiseAmt, noise);  // the temperature still follows, for switching
    }
    th.update(r, vb, fb);
    return r;
  }

  static constexpr float kMinD = 2.f;
  // The finger side is split at the touch point: bow -> touch -> nut and back.
  // With bow width, the bridge side is split at the second contact point.
  Delay toTouch_, touchToNut_, nutToTouch_, fromTouch_, toBridge_, fromBridge_, toB2_, fromB2_;
  std::array<Delay*, 8> lines() {
    return {&toTouch_, &touchToNut_, &nutToTouch_, &fromTouch_, &toBridge_, &fromBridge_, &toB2_, &fromB2_};
  }
  int width_ = 0;
  float share_ = 0.8f, torX2_ = 0.f, torY2_ = 0.f;
  LossShelf nutLoss_, bridgeLoss_;
  OnePoleLP topLP_;
  StringDef def_{"", 69, 0.2f, 0.7f};
  LossDef loss_{2.f, 25.f, 2000.f, 3.f, 5.f, 0.1f, 20000.f};
  float fs_ = 96000.f, smooth_ = 0.01f, torA_ = 0.99f, torX_ = 0.f, torY_ = 0.f;
  float dN_ = 100.f, dB_ = 10.f, tdN_ = 100.f, tdB_ = 10.f, betaActual_ = 0.1f;
  float dT_ = 50.f, tdT_ = 50.f, touchQ_ = 0.f, touchTarget_ = 0.f, touchR_ = 0.f, pluck_ = 0.f;
  float touchGlide_ = 0.001f;
  float level_ = 0.f, arrive_ = 0.f, lv_ = 0.f, fb_ = 0.f, fingerNoise_ = 0.f;
  int quiet_ = 0;
  bool asleep_ = true, first_ = true, slipping_ = false;
};

}  // namespace sm
