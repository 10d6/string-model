// The held tone's bow noise (docs/MODEL.md "The cello": the held tone's
// noise), added to the bridge force with the recorded onsets.
//
// Measured against the Iowa arco and the bigcat cello (tools/bow_noise.py),
// the synth's held tone was 10-25 dB too clean between 125 Hz and 2 kHz: the
// friction's own noise (Rosin Noise, KB §7) comes out high, at 4-8 kHz, where
// it already matches. This adds the missing part: white noise in seven octave
// bands, each at a level per string and dynamic (data/CelloBowNoise.h) re the
// note's own bridge force, and only while the bow slips, as real bow noise
// comes in a burst at each slip (KB §7, Chafe; Demoucron, ref 93).
#pragma once
#include <cmath>
#include <cstdint>

namespace sm {

struct BowNoiseTable {
  const float* hz = nullptr;   // band centres
  const float* vel = nullptr;  // the three dynamics' velocities (0-127)
  const float* db = nullptr;   // [string][dynamic][band], dB re the bridge force's RMS
  int bands = 0;
};

class BowNoise {
 public:
  static constexpr int kMaxBands = 8;

  void prepare(float fs, const BowNoiseTable& t) {
    fs_ = fs;
    t_ = t;
    n_ = t.bands < kMaxBands ? t.bands : kMaxBands;
    for (int k = 0; k < n_; ++k) {
      // RBJ band-pass, 0 dB peak, an octave wide (Q = sqrt 2).
      const float f = std::fmin(t.hz[k], 0.45f * fs);
      const float w0 = 6.2831853f * f / fs, q = 1.41421356f;
      const float al = std::sin(w0) / (2.f * q), a0 = 1.f + al;
      bp_[k].b0 = al / a0;
      bp_[k].b2 = -al / a0;
      bp_[k].a1 = -2.f * std::cos(w0) / a0;
      bp_[k].a2 = (1.f - al) / a0;
      // Unit-variance noise into it gives sigma^2 * NBW / (fs / 2), NBW its
      // noise bandwidth (pi/2) f/Q: this turns a band's power ratio into an
      // amplitude.
      const float nbw = 1.5707963f * f / q;
      norm_[k] = std::sqrt(0.5f * fs / nbw);
    }
    gateA_ = 1.f - std::exp(-1.f / (0.0002f * fs));  // 0.2 ms: a slip's burst, not a click
    reset();
  }
  void reset() {
    for (auto& b : bp_) b.s1 = b.s2 = 0.f;
    gate_ = 0.f;
  }

  // Per control tick: the lead string (0-3) and its stroke's velocity (0-1);
  // `amount` (Bow Noise) and the instrument's trim (dB).
  void setTarget(int string, float vel, float amount, float trimDb) {
    if (!t_.db) return;
    const float v = vel * 127.f;
    int lo = 0;
    float u = 0.f;
    if (v <= t_.vel[0]) {
      lo = 0;
      u = 0.f;
    } else if (v >= t_.vel[2]) {
      lo = 1;
      u = 1.f;
    } else {
      lo = v < t_.vel[1] ? 0 : 1;
      u = (v - t_.vel[lo]) / (t_.vel[lo + 1] - t_.vel[lo]);
    }
    const float* a = t_.db + (string * 3 + lo) * t_.bands;
    const float* b = a + t_.bands;
    for (int k = 0; k < n_; ++k) {
      const float d = a[k] + (b[k] - a[k]) * u + trimDb;
      gain_[k] = amount * std::pow(10.f, d / 20.f) * norm_[k];
    }
  }

  // One host sample: `level` the note's bridge force (N RMS, before this),
  // `slip` the share of this sample's internal steps a bowed string slipped
  // (0..1), `white` uniform in [-1, 1).
  float process(float level, float slip, float white) {
    gate_ += (slip - gate_) * gateA_;
    if (level <= 0.f || gate_ < 1e-4f) {
      for (int k = 0; k < n_; ++k) bp_[k].process(0.f);
      return 0.f;
    }
    const float x = white * 1.7320508f;  // unit variance
    float y = 0.f;
    for (int k = 0; k < n_; ++k) y += gain_[k] * bp_[k].process(x);
    return y * level * gate_;
  }

 private:
  struct BP {
    float b0 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, s1 = 0.f, s2 = 0.f;
    float process(float x) {
      const float y = b0 * x + s1;
      s1 = -a1 * y + s2;
      s2 = b2 * x - a2 * y;
      return y;
    }
  };
  BP bp_[kMaxBands];
  float norm_[kMaxBands]{}, gain_[kMaxBands]{};
  BowNoiseTable t_{};
  int n_ = 0;
  float fs_ = 48000.f, gate_ = 0.f, gateA_ = 0.f;
};

}  // namespace sm
