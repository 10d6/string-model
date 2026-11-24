// The bridge: one driving-point admittance Y(ω) = v/F shared by every
// string (KB §5.1), as a passive modal filter bank.
//
//   Y(s) = Σ_k (1/m_k) s / (s² + (ω_k/Q_k) s + ω_k²)
//
// Each mode has a positive effective mass, so Y is positive real (passive);
// the bilinear transform (pre-warped at ω_k) keeps it so. Each mode becomes a
// biquad with a direct term b0, which is what lets the strings and bridge be
// solved together without a delay (see Instrument):
//
//   u = Y0·F + h,  Y0 = Σ b0_k,  h = Σ (modes' state)
//
// A string terminated by the moving bridge reflects r = u − a (a: incoming
// velocity wave) and pushes on it with F = Z (a − r) = Z (2a − u). That's
// the reflectance −1 + 2 Z Y of KB §5.1, solved exactly.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace sm {

struct BridgeMode {
  float hz;      // resonance
  float q;       // quality factor
  float peak;    // admittance at resonance (s/kg) = Q / (m ω)
};

class Bridge {
 public:
  // modes: the instrument's signature modes; the dense part above them is
  // generated (random frequencies, deterministic per instrument).
  void prepare(float fs, const BridgeMode* modes, int count, const BridgeMode& hill, uint32_t seed,
               float denseFrom, float denseTo, float spacing) {
    fs_ = fs;
    modes_.clear();
    for (int i = 0; i < count; ++i) add(modes[i]);
    // Dense high band (KB §5.3): random modes with mean spacing `spacing`,
    // Q falling with frequency, levels shaped by the bridge hill (a broad
    // resonance of the bridge rocking on the body).
    uint32_t r = seed;
    auto uni = [&r]() {
      r = r * 1664525u + 1013904223u;
      return static_cast<float>(r >> 8) * (1.f / 16777216.f);
    };
    hill_ = hill;
    for (float f = denseFrom + spacing * uni(); f < denseTo; f += spacing * (0.5f + uni())) {
      const float q = std::max(8.f, 40.f * std::pow(denseFrom / f, 0.5f));
      const float factor = 0.5f + uni();
      const std::size_t before = modes_.size();
      add({f, q, hill.peak * hillShape(f, hill.hz, hill.q) * factor});
      if (modes_.size() > before) modes_.back().dense = factor;
    }
    coupling_ = 1.f;
    hillHz_ = hill.hz;
    hillLevel_ = 1.f;
    updateCoefficients();
    reset();
  }

  void reset() {
    for (auto& m : modes_) m.s1 = m.s2 = 0.f;
    hist_ = 0.f;
  }

  // Move the bridge hill (KB §5.3): the dense band's levels follow the hill
  // shape at its new centre; levelScale scales them (a mute adds mass to the
  // bridge: the hill moves down and gets weaker). Control rate, on change.
  void setHill(float hz, float levelScale) {
    if (hz == hillHz_ && levelScale == hillLevel_) return;
    hillHz_ = hz;
    hillLevel_ = levelScale;
    for (auto& m : modes_)
      if (m.dense > 0.f) m.peak = hill_.peak * levelScale * hillShape(m.hz, hz, hill_.q) * m.dense;
    updateCoefficients();
  }

  // A wolf eliminator on signature mode `index` (KB §5.4): a mass damper
  // tuned near the mode adds damping and loads it a little lower. amount
  // 0..1: Q / (1 + 3 amount), frequency x (1 - 0.03 amount), the mode's mass
  // kept (so its peak admittance falls with its Q) [T]. Control rate, on
  // change.
  void setDamper(int index, float amount) {
    if (index < 0 || index >= static_cast<int>(modes_.size()) || modes_[static_cast<std::size_t>(index)].dense > 0.f) return;
    if (amount == damper_) return;
    damper_ = amount;
    Mode& m = modes_[static_cast<std::size_t>(index)];
    const float mass = m.q0 / (6.2831853f * m.hz0 * m.peak0);
    m.q = m.q0 / (1.f + 3.f * amount);
    m.hz = m.hz0 * (1.f - 0.03f * amount);
    m.peak = m.q / (6.2831853f * m.hz * mass);
    updateCoefficients();
  }

  // Scale every mode's admittance (0 = a rigid bridge).
  void setCoupling(float c) {
    if (c == coupling_) return;
    coupling_ = c;
    updateCoefficients();
  }
  float coupling() const { return coupling_; }

  // Direct term and history of the bridge velocity for this sample.
  float y0() const { return y0_; }
  float history() const { return hist_; }
  // Advance every mode with this sample's total force from the strings
  // (and sum the next sample's history in the same pass).
  void advance(float f) {
    float h = 0.f;
    for (auto& m : modes_) {
      const float y = m.b0 * f + m.s1;
      m.s1 = -m.a1 * y + m.s2;           // b1 = 0
      m.s2 = m.b2 * f - m.a2 * y;
      h += m.s1;
    }
    hist_ = h;
  }
  // Rough energy measure for sleeping.
  float activity() const {
    float a = 0.f;
    for (const auto& m : modes_) a += std::fabs(m.s1) + std::fabs(m.s2);
    return a;
  }
  int modeCount() const { return static_cast<int>(modes_.size()); }

  // |Y| at a frequency, for tests and documentation.
  float magnitude(float hz) const {
    double re = 0, im = 0;
    const double w = 2 * 3.141592653589793 * hz;
    for (const auto& m : modes_) {
      const double wk = 2 * 3.141592653589793 * m.hz, mass = m.q / (wk * m.peak);
      // s/(m (s² + wk/Q s + wk²)), s = jw
      const double dr = wk * wk - w * w, di = w * wk / m.q;
      const double nr = 0, ni = w / mass;
      const double d2 = dr * dr + di * di;
      re += (nr * dr + ni * di) / d2;
      im += (ni * dr - nr * di) / d2;
    }
    return static_cast<float>(std::sqrt(re * re + im * im)) * coupling_;
  }

 private:
  struct Mode {
    float hz, q, peak;
    float hz0 = 0, q0 = 0, peak0 = 0;  // as defined (a wolf eliminator moves hz, q, peak)
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    float dense = 0;  // dense-band mode: its random level factor (0 = signature mode)
  };

  // |hill(f)|: 2nd-order resonance magnitude, normalised to 1 at the centre.
  static float hillShape(float f, float centre, float q) {
    const float x = f / centre;
    const float den = std::sqrt((1.f - x * x) * (1.f - x * x) + (x / q) * (x / q));
    return (x / q) / den;
  }

  void add(const BridgeMode& b) {
    if (b.hz <= 0.f || b.hz >= 0.45f * fs_ || b.peak <= 0.f) return;
    Mode m;
    m.hz = b.hz;
    m.q = b.q;
    m.peak = b.peak;
    m.hz0 = b.hz;
    m.q0 = b.q;
    m.peak0 = b.peak;
    modes_.push_back(m);
  }

  void updateCoefficients() {
    y0_ = 0.f;
    for (auto& m : modes_) {
      const double w = 2 * 3.141592653589793 * m.hz;
      const double mass = m.q / (w * m.peak);
      const double K = w / std::tan(w / (2.0 * fs_));  // pre-warp at the mode
      const double a = w / m.q, b = w * w;
      const double a0 = K * K + a * K + b;
      const double g = coupling_ / mass;
      m.b0 = static_cast<float>(g * K / a0);
      m.b2 = -m.b0;
      m.a1 = static_cast<float>((2 * b - 2 * K * K) / a0);
      m.a2 = static_cast<float>((K * K - a * K + b) / a0);
      y0_ += m.b0;
    }
  }

  std::vector<Mode> modes_;
  float damper_ = 0.f;
  float fs_ = 96000.f, y0_ = 0.f, coupling_ = 1.f, hist_ = 0.f, hillHz_ = 0.f, hillLevel_ = 1.f;
  BridgeMode hill_{2600.f, 3.f, 0.15f};
};

}  // namespace sm
