// The whole instrument: four strings driven by the performer, run at an
// oversampled internal rate and decimated to the host rate. Output is the
// summed transverse bridge force (N) at the host rate; the body/output stage
// lives in the synth.
//
// Also the player's ear: on stopped notes a slow loop compares the measured
// slip-to-slip period with the wanted period and moves the finger, as a
// player corrects intonation by ear (bowed strings go flat under force,
// KB §1.2). Open strings can't be corrected.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "BowedString.h"
#include "Bridge.h"
#include "Performer.h"
#include "defs/Instruments.h"

namespace sm {

// Band-pass biquad (RBJ, constant 0 dB peak) for the winding noise.
struct BandPass {
  float b0 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
  void setBandPass(float hz, float q, float fs) {
    const float w0 = 6.2831853f * hz / fs, al = std::sin(w0) / (2.f * q), a0 = 1.f + al;
    b0 = al / a0;
    b2 = -b0;
    a1 = -2.f * std::cos(w0) / a0;
    a2 = (1.f - al) / a0;
  }
  float process(float x) {
    const float y = b0 * x + s1;
    s1 = -a1 * y + s2;
    s2 = b2 * x - a2 * y;
    return y;
  }
};

// Linear-phase low-pass FIR decimator by an integer factor (1, 2 or 4).
class Decimator {
 public:
  void prepare(int factor) {
    factor_ = factor;
    if (factor_ <= 1) return;
    const int taps = 24 * factor_ + 1;
    h_.assign(static_cast<std::size_t>(taps), 0.f);
    const double fc = 0.45 / factor_;  // of the internal rate: just under the host Nyquist
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
      const double m = i - (taps - 1) / 2.0;
      const double sinc = m == 0 ? 2 * fc : std::sin(2 * 3.141592653589793 * fc * m) / (3.141592653589793 * m);
      // Blackman-Harris window
      const double w = 0.35875 - 0.48829 * std::cos(2 * 3.141592653589793 * i / (taps - 1)) +
                       0.14128 * std::cos(4 * 3.141592653589793 * i / (taps - 1)) -
                       0.01168 * std::cos(6 * 3.141592653589793 * i / (taps - 1));
      h_[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
      sum += sinc * w;
    }
    for (float& v : h_) v = static_cast<float>(v / sum);
    buf_.assign(static_cast<std::size_t>(2 * taps), 0.f);
    pos_ = 0;
  }
  int factor() const { return factor_; }
  // Push `factor` internal samples, get one host sample.
  float process(const float* x) {
    if (factor_ <= 1) return x[0];
    const int taps = static_cast<int>(h_.size());
    for (int k = 0; k < factor_; ++k) {
      buf_[static_cast<std::size_t>(pos_)] = buf_[static_cast<std::size_t>(pos_ + taps)] = x[k];
      pos_ = (pos_ + 1) % taps;
    }
    // buf_[pos_ .. pos_+taps) is oldest..newest
    float acc = 0.f;
    const float* b = &buf_[static_cast<std::size_t>(pos_)];
    for (int i = 0; i < taps; ++i) acc += h_[static_cast<std::size_t>(taps - 1 - i)] * b[i];
    return acc;
  }
  void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); }

 private:
  int factor_ = 1, pos_ = 0;
  std::vector<float> h_, buf_;
};

struct InstrumentControls {
  FrictionModel friction = FrictionModel::Enhanced;
  float noise = 0.12f;       // rosin noise during slip
  float intonation = 0.8f;   // 0..1: how much the player corrects flattening
  float coupling = 1.f;      // bridge admittance scale (0 = rigid bridge, no sympathy)
  float hillHz = 2600.f;     // bridge hill centre (admittance; the radiation follows in the synth)
  float mute = 0.f;          // 0..1: the mute's mass on the bridge
  float slideNoise = 0.4f;   // 0..1: winding noise under a sliding finger
  float bowWidth = 0.006f;   // m: hair in contact (two contact points this far apart; 0 = one point) [T]
};

class Instrument {
 public:
  // def: which instrument (model/defs/; it must outlive this object).
  void prepare(double hostRate, const InstrumentDef& def = violin()) {
    host_ = static_cast<float>(hostRate);
    harmGlide_ = 1.f - std::exp(-1.f / (0.02f * host_));  // ~20 ms in/out
    os_ = hostRate < 70000.0 ? 2 : 1;
    fs_ = host_ * static_cast<float>(os_);
    dec_.prepare(os_);
    def_ = &def;
    for (int s = 0; s < 4; ++s) {
      str_[s].prepare(fs_, 40.f);
      str_[s].setDef(def_->strings[s], def_->loss);
      LossDef lv = def_->loss;
      for (float* r : {&lv.lowRate, &lv.highRate, &lv.fingerLow, &lv.fingerHigh}) *r /= def_->vertT60;
      vert_[s].prepare(fs_, 40.f);
      vert_[s].setDef(def_->strings[s], lv);
    }
    perf_.prepare(host_, *def_);
    bridge_.prepare(fs_, def_->modes, def_->modeCount, def_->hill, def_->bodySeed, def_->denseFrom, def_->denseTo,
                    def_->denseSpacing);
    perf_.setBridge(&bridge_);
    reset();
  }

  void reset() {
    for (auto& s : str_) s.reset();
    for (auto& s : vert_) s.reset();
    for (float& p : pluckV_) p = 0.f;
    perf_.reset();
    dec_.reset();
    for (int s = 0; s < 4; ++s) {
      corr_[s] = 0.f;
      sinceSlip_[s] = 0;
      prevSlip_[s] = false;
    }
    tick_ = 0;
    dcX_ = dcY_ = 0.f;
    for (auto& t : tilt_) t.lp = 0.f;
    bridge_.reset();
    uPrev_ = 0.f;
  }

  Performer& performer() { return perf_; }
  const Bridge& bridge() const { return bridge_; }
  BowedString& string(int s) { return str_[s]; }
  const InstrumentDef& def() const { return *def_; }

  // One host sample of summed bridge force (N).
  float process(const PerformerControls& pc, const InstrumentControls& ic) {
    if (tick_ == 0) controlTick(pc, ic);
    tick_ = (tick_ + 1) % kControl;
    const float vb = perf_.tickSample(pc);
    for (int s = 0; s < 4; ++s) {
      pluck_[s] = perf_.pluckForce(s);
      tap_[s] = perf_.fingerTap(s);
      pluckV_[s] = perf_.pluckForceVertical(s);
    }
    float x[4];
    for (int k = 0; k < os_; ++k) x[k] = internalSample(vb, ic);
    float y = tilt_[1].process(tilt_[0].process(dec_.process(x)));
    harmMix_ += (harmMixTarget_ - harmMix_) * harmGlide_;
    const float yh = harmLP_.process(y);
    y += harmMix_ * (yh - y);
    // DC blocker (5 Hz).
    const float out = y - dcX_ + dcR_ * dcY_;
    dcX_ = y;
    dcY_ = out;
    return out;
  }

 private:
  static constexpr int kControl = 16;  // host samples per control tick
  static constexpr float kWindDirect = 1.f;  // winding noise straight to the bridge [T]
  static constexpr float kTiltDb = 5.f;
  static constexpr float kHarmLP = 3.f;  // harmonics' low-pass, x the sounding pitch [T]      // dB (at the top) per octave of bow force re mf [T]
  static constexpr float kTiltMax = 9.f;     // dB
  // First-order high shelf: x + (g - 1) x highpass(x) at fc.
  struct Shelf {
    float a = 0.f, g = 1.f, lp = 0.f;
    void set(float fc, float gain, float fs) {
      a = 1.f - std::exp(-2.f * 3.14159265f * fc / fs);
      g = gain;
    }
    float process(float x) {
      lp += a * (x - lp);
      return x + (g - 1.f) * (x - lp);
    }
  };
  Shelf tilt_[2];
  // Harmonics' purity (see controlTick): 2-pole low-pass, crossfaded in.
  struct LowPass2 {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    void set(float fc, float fs) {  // RBJ, Q 0.707
      const float w = 2.f * 3.14159265f * std::min(fc, 0.45f * fs) / fs, al = std::sin(w) / (2.f * 0.70710678f), c = std::cos(w);
      const float n = 1.f / (1.f + al);
      b0 = b2 = 0.5f * (1.f - c) * n;
      b1 = (1.f - c) * n;
      a1 = -2.f * c * n;
      a2 = (1.f - al) * n;
    }
    float process(float x) {  // transposed direct form II
      const float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };
  LowPass2 harmLP_;
  float harmMix_ = 0.f, harmMixTarget_ = 0.f, harmGlide_ = 0.f;

  // One internal-rate sample: every string's bow junction, then the bridge
  // they share, solved together without a delay (Bridge.h):
  //   F = Σ Z_i (2 a_i - u) = S - Zs u,   u = Y0 F + h
  //   =>  u = (Y0 S + h) / (1 + Y0 Zs)
  // A sleeping string still loads the bridge (its Z is in Zs); it wakes when
  // the bridge moves, which is how open strings ring in sympathy.
  float internalSample(float vb, const InstrumentControls& ic) {
    float S = 0.f, zs = 0.f, direct = 0.f;
    bool any = false;
    const bool coupled = bridge_.coupling() > 0.f;
    const bool bridgeMoving = coupled && std::fabs(uPrev_) > 1e-7f;
    for (int s = 0; s < 4; ++s) {
      BowedString& st = str_[s];
      const float fb = target_[s].force;
      const float damp = target_[s].damp;
      zs += st.z0();
      active_[s] = !st.asleep() || fb > 0.f || bridgeMoving || damp > 0.f || windAmp_[s] > 0.f || pluck_[s] != 0.f || tap_[s] != 0.f;
      if (!active_[s]) continue;
      any = true;
      st.wake();
      rng_ = rng_ * 1664525u + 1013904223u;
      const float noise = static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
      // Winding noise: injected at the finger (it travels the string), and,
      // because a sticking bow reflects it back toward the finger, also
      // straight into the bridge force (in reality it also reaches the body
      // through the neck and the string's other wave types) [T].
      float wn = 0.f;
      if (windAmp_[s] > 0.f) wn = windAmp_[s] * windBp_[s].process(2.f * noise - 1.f);
      // A finger landing or lifting (slurs) taps the string at the finger.
      st.setFingerNoise(wn + tap_[s]);
      direct += st.z0() * wn;
      S += 2.f * st.z0() * st.step(vb, fb, ic.friction, ic.noise, noise, damp, pluck_[s]);
      // Intonation: slip onsets mark periods in Helmholtz motion.
      const bool sl = st.slipping();
      ++sinceSlip_[s];
      if (sl && !prevSlip_[s]) {
        onPeriod(s, static_cast<float>(sinceSlip_[s]), ic);
        sinceSlip_[s] = 0;
      }
      prevSlip_[s] = sl;
    }
    if (!any && (!coupled || bridge_.activity() < 1e-12f) && vertAsleep()) {
      uPrev_ = 0.f;
      return 0.f;
    }
    float u = 0.f;
    if (coupled) {
      const float y0 = bridge_.y0();
      u = (y0 * S + bridge_.history()) / (1.f + y0 * zs);
    }
    for (int s = 0; s < 4; ++s)
      if (active_[s]) str_[s].finish(u);
    const float force = S - zs * u;
    if (coupled) bridge_.advance(force);
    uPrev_ = u;
    // Vertical polarisations (plucks only): on a rigid bridge, into the output.
    float vert = 0.f;
    for (int s = 0; s < 4; ++s) {
      BowedString& v = vert_[s];
      if (v.asleep() && pluckV_[s] == 0.f) continue;
      v.wake();
      // A damping hand or resting bow stops both polarisations.
      v.step(0.f, 0.f, ic.friction, 0.f, 0.f, target_[s].damp, pluckV_[s]);
      vert += v.finish(0.f);
    }
    return force + kWindDirect * direct + def_->vertGain * vert;
  }

  void controlTick(const PerformerControls& pc, const InstrumentControls& ic) {
    bridge_.setCoupling(ic.coupling);
    // A mute moves the hill to ~65% and weakens it (KB §11.2) [T level].
    bridge_.setHill(ic.hillHz * (1.f - 0.35f * ic.mute), 1.f - 0.4f * ic.mute);
    perf_.controlTick(pc, kControl / host_);
    perf_.strings(target_, pc);
    // Force-dependent brightness (KB §2 "corner rounding", §11.5: log centroid
    // rises linearly with log bow force). With the loss shelves the string
    // gives about half of the recordings' p -> f brightening by itself (VSCO
    // G3-E5 median, 2-4/4-8/8-16 kHz: +1.3/+2.9/+3.4 dB of +2.5/+6.4/+8.0); the
    // output is tilted for the rest, outside the loop: kTiltDb per octave of
    // force re mf, half on a shelf at 2.5 kHz, half at 7 kHz [T, fitted to
    // VSCO p/f: +3.0/+6.1/+8.2 dB].
    const float tiltDb = std::clamp(kTiltDb * perf_.brightness(), -kTiltMax, kTiltMax);
    const float g = std::pow(10.f, 0.5f * tiltDb / 20.f);
    tilt_[0].set(2500.f, g, host_);
    tilt_[1].set(7000.f, g, host_);
    // Harmonics sound purer than a stopped note at the same pitch: partials
    // 3-6 are 5-12 dB weaker in the Philharmonia recordings (harmonic vs
    // arco-normal, same pitch: mics and body cancel), while the touched
    // waveguide gives the stopped note's spectrum. The finger pad (~1 cm)
    // damps the higher partials, but any loss in the loop stops the light bow
    // a harmonic needs, so the output is low-passed instead, outside the loop,
    // at kHarmLP x the sounding pitch while a bowed string is touched [T].
    {
      harmMixTarget_ = 0.f;
      for (int s = 0; s < 4; ++s)
        if (target_[s].touchQ > 0.f && target_[s].force > 0.f) {
          harmLP_.set(kHarmLP * target_[s].hz / target_[s].touchQ, host_);
          harmMixTarget_ = 1.f;
        }
    }
    const float beta = perf_.beta();
    for (int s = 0; s < 4; ++s) {
      if (!target_[s].stopped || ic.intonation <= 0.f) corr_[s] *= 0.995f;
      const float k = std::pow(2.f, corr_[s] / 1200.f);
      wantHz_[s] = target_[s].hz;
      str_[s].setTouch(target_[s].touchQ, target_[s].touchR);
      // Width in samples: the wave speed is 2 x scale x open pitch on any stop.
      const float openHz = 440.f * std::pow(2.f, (static_cast<float>(def_->strings[s].openNote) - 69.f) / 12.f);
      str_[s].setBowWidth(ic.bowWidth > 0.f ? static_cast<int>(std::lround(ic.bowWidth * fs_ / (2.f * def_->scaleLength * openHz))) : 0);
      str_[s].setGeometry(target_[s].hz * k, target_[s].noteHz * k, beta, target_[s].stopped, target_[s].slide);
      const float kv = k * std::pow(2.f, def_->vertDetuneCents / 1200.f);
      vert_[s].setGeometry(target_[s].hz * kv, target_[s].noteHz * kv, beta, target_[s].stopped, target_[s].slide);
      // Winding noise: the finger rubbing turns of winding at speed v gives a
      // band of noise around v / pitch (KB §11.2 "Shifts") [T level].
      const float v = target_[s].slide * def_->winding[s];
      windAmp_[s] = ic.slideNoise * 0.3f * std::min(v, 2.f);  // ~-18 dB re the note at 1, ~-26 dB at 0.4 [T]
      if (windAmp_[s] > 0.f) {
        const float fc = std::clamp(v / def_->windingPitch, 300.f, 0.4f * fs_);
        windBp_[s].setBandPass(fc, 2.f, fs_);
      }
    }
    dcR_ = std::exp(-2.f * 3.14159265f * 5.f / host_);
  }

  void onPeriod(int s, float period, const InstrumentControls& ic) {
    if (!target_[s].stopped || ic.intonation <= 0.f || target_[s].force <= 0.f) return;
    // A touched (harmonic) string slips k times per fundamental period: the
    // player tunes an artificial harmonic by ear the same way.
    const float k = target_[s].touchQ > 0.f ? std::round(1.f / target_[s].touchQ) : 1.f;
    const float want = fs_ / (wantHz_[s] * k);
    const float err = period / want;
    if (err < 0.96f || err > 1.04f) return;  // not steady one-slip-per-period motion
    // Flat (long period) -> raise the finger pitch. ~60 periods to settle.
    corr_[s] += ic.intonation * 0.016f * 1200.f * std::log2(err);
    corr_[s] = std::clamp(corr_[s], -60.f, 60.f);
  }

  const InstrumentDef* def_ = nullptr;
  BowedString str_[4], vert_[4];  // vert_: the vertical polarisation (pizzicato)
  float pluckV_[4]{};
  bool vertAsleep() const {
    for (int s = 0; s < 4; ++s)
      if (!vert_[s].asleep() || pluckV_[s] != 0.f) return false;
    return true;
  }
  Performer perf_;
  Decimator dec_;
  Bridge bridge_;
  bool active_[4]{};
  float uPrev_ = 0.f;
  BandPass windBp_[4];
  float pluck_[4]{};
  float tap_[4]{};  // finger taps (m/s at the finger), per host sample
  float windAmp_[4]{};
  StringTarget target_[4];
  float host_ = 48000.f, fs_ = 96000.f;
  int os_ = 2, tick_ = 0;
  float corr_[4]{}, wantHz_[4]{440, 440, 440, 440};
  int sinceSlip_[4]{};
  bool prevSlip_[4]{};
  float dcX_ = 0.f, dcY_ = 0.f, dcR_ = 0.999f;
  uint32_t rng_ = 0x9e3779b9u;
};

}  // namespace sm
