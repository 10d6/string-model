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
#include "Performer.h"

namespace sm {

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
  FrictionModel friction = FrictionModel::Thermal;
  float noise = 0.12f;       // rosin noise during slip
  float intonation = 0.8f;   // 0..1: how much the player corrects flattening
};

class Instrument {
 public:
  void prepare(double hostRate) {
    host_ = static_cast<float>(hostRate);
    os_ = hostRate < 70000.0 ? 2 : 1;
    fs_ = host_ * static_cast<float>(os_);
    dec_.prepare(os_);
    def_ = &violin();
    for (int s = 0; s < 4; ++s) {
      str_[s].prepare(fs_, 40.f);
      str_[s].setDef(def_->strings[s], def_->loss);
    }
    perf_.prepare(host_, *def_);
    reset();
  }

  void reset() {
    for (auto& s : str_) s.reset();
    perf_.reset();
    dec_.reset();
    for (int s = 0; s < 4; ++s) {
      corr_[s] = 0.f;
      sinceSlip_[s] = 0;
      prevSlip_[s] = false;
    }
    tick_ = 0;
    dcX_ = dcY_ = 0.f;
  }

  Performer& performer() { return perf_; }
  const InstrumentDef& def() const { return *def_; }

  // One host sample of summed bridge force (N).
  float process(const PerformerControls& pc, const InstrumentControls& ic) {
    if (tick_ == 0) controlTick(pc, ic);
    tick_ = (tick_ + 1) % kControl;
    const float vb = perf_.tickSample(pc);
    float x[4];
    for (int k = 0; k < os_; ++k) {
      float sum = 0.f;
      for (int s = 0; s < 4; ++s) {
        BowedString& st = str_[s];
        const float fb = target_[s].force;
        if (st.asleep() && fb <= 0.f) continue;
        rng_ = rng_ * 1664525u + 1013904223u;
        const float noise = static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
        sum += st.process(vb, fb, ic.friction, ic.noise, noise);
        // Intonation: slip onsets mark periods in Helmholtz motion.
        const bool sl = st.slipping();
        ++sinceSlip_[s];
        if (sl && !prevSlip_[s]) {
          onPeriod(s, static_cast<float>(sinceSlip_[s]), ic);
          sinceSlip_[s] = 0;
        }
        prevSlip_[s] = sl;
      }
      x[k] = sum;
    }
    const float y = dec_.process(x);
    // DC blocker (5 Hz).
    const float out = y - dcX_ + dcR_ * dcY_;
    dcX_ = y;
    dcY_ = out;
    return out;
  }

 private:
  static constexpr int kControl = 16;  // host samples per control tick

  void controlTick(const PerformerControls& pc, const InstrumentControls& ic) {
    perf_.controlTick(pc, kControl / host_);
    perf_.strings(target_, pc);
    const float beta = perf_.beta();
    for (int s = 0; s < 4; ++s) {
      if (!target_[s].stopped || ic.intonation <= 0.f) corr_[s] *= 0.995f;
      const float k = std::pow(2.f, corr_[s] / 1200.f);
      wantHz_[s] = target_[s].hz;
      str_[s].setGeometry(target_[s].hz * k, target_[s].noteHz * k, beta, target_[s].stopped);
    }
    dcR_ = std::exp(-2.f * 3.14159265f * 5.f / host_);
  }

  void onPeriod(int s, float period, const InstrumentControls& ic) {
    if (!target_[s].stopped || ic.intonation <= 0.f || target_[s].force <= 0.f) return;
    const float want = fs_ / wantHz_[s];
    const float err = period / want;
    if (err < 0.96f || err > 1.04f) return;  // not steady one-slip-per-period motion
    // Flat (long period) -> raise the finger pitch. ~60 periods to settle.
    corr_[s] += ic.intonation * 0.016f * 1200.f * std::log2(err);
    corr_[s] = std::clamp(corr_[s], -60.f, 60.f);
  }

  const InstrumentDef* def_ = nullptr;
  BowedString str_[4];
  Performer perf_;
  Decimator dec_;
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
