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
//
// Per-string mode strengths (the cello, cello doc §3.1): a mode may couple to
// each string with its own weight w_s (the string's notch on the bridge sits
// where the mode moves more or less). The mode is driven by Σ w_s F_s and
// moves string s's end by w_s times its velocity, so the strings see a 4x4
// admittance Y_st = Σ_k w_ks w_kt Y_k (still passive), and the solve is
//
//   u = (I + Y0 Z)^-1 (Y0 S + h),  S_s = 2 Z_s a_s,
//
// with the inverse precomputed whenever the coefficients change. With no
// weighted modes (the violin) the scalar path above is used, unchanged.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace sm {

struct BridgeMode {
  float hz;      // resonance
  float q;       // quality factor
  float peak;    // admittance at resonance (s/kg) = Q / (m ω), at a string weighted 1
  float w[4] = {1.f, 1.f, 1.f, 1.f};  // per-string weight (residue ∝ w²); all 1: shared
};

class Bridge {
 public:
  // modes: the instrument's signature modes; the dense part above them is
  // generated (random frequencies, deterministic per instrument).
  void prepare(float fs, const BridgeMode* modes, int count, const BridgeMode& hill, uint32_t seed,
               float denseFrom, float denseTo, float spacing) {
    fs_ = fs;
    modes_.clear();
    weighted_ = false;
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
    for (float& h : histW_) h = 0.f;
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

  // The strings' impedances (for the weighted solve). Before prepare().
  void setStringImpedances(const float z[4]) {
    for (int s = 0; s < 4; ++s) z_[s] = z[s];
  }
  // The impedance standing at each string's notch now (the sum over the
  // strings there: several voices can share a notch, and a notch can be
  // empty). Re-solves the weighted path when it changes; the scalar path
  // takes the total each sample. Real-time safe.
  void setSlotImpedances(const float z[4]) {
    bool same = true;
    for (int s = 0; s < 4; ++s) same = same && z[s] == z_[s];
    if (same) return;
    for (int s = 0; s < 4; ++s) z_[s] = z[s];
    if (weighted_) invertWeighted();
  }
  // Any mode with per-string weights: the strings need solveWeighted().
  bool weighted() const { return weighted_; }
  // Bridge velocity at each string from S_s = 2 Z_s a_s (weighted solve).
  void solveWeighted(const float S[4], float u[4]) const {
    for (int s = 0; s < 4; ++s) {
      float v = 0.f;
      for (int t = 0; t < 4; ++t) v += A_[s][t] * S[t] + M_[s][t] * histW_[t];
      u[s] = v;
    }
  }
  // Advance with each string's force (weighted modes take Σ w_s F_s).
  void advanceWeighted(const float F[4]) {
    const float total = F[0] + F[1] + F[2] + F[3];
    for (float& h : histW_) h = 0.f;
    for (auto& m : modes_) {
      const float f = m.weighted ? m.w[0] * F[0] + m.w[1] * F[1] + m.w[2] * F[2] + m.w[3] * F[3] : total;
      const float y = m.b0 * f + m.s1;
      m.s1 = -m.a1 * y + m.s2;
      m.s2 = m.b2 * f - m.a2 * y;
      if (m.weighted)
        for (int s = 0; s < 4; ++s) histW_[s] += m.w[s] * m.s1;
      else
        for (float& h : histW_) h += m.s1;
    }
  }

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

  // |Y| at a frequency, for tests and documentation; string >= 0: the
  // driving-point admittance at that string (weighted modes x w_s²).
  float magnitude(float hz, int string = -1) const {
    double re = 0, im = 0;
    const double w = 2 * 3.141592653589793 * hz;
    for (const auto& m : modes_) {
      const double wk = 2 * 3.141592653589793 * m.hz, mass = m.q / (wk * m.peak);
      // s/(m (s² + wk/Q s + wk²)), s = jw
      const double dr = wk * wk - w * w, di = w * wk / m.q;
      const double ws = (string >= 0 && m.weighted) ? double(m.w[string]) * m.w[string] : 1.0;
      const double nr = 0, ni = ws * w / mass;
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
    float w[4] = {1.f, 1.f, 1.f, 1.f};
    bool weighted = false;
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
    for (int s = 0; s < 4; ++s) {
      m.w[s] = b.w[s];
      if (b.w[s] != 1.f) m.weighted = true;
    }
    weighted_ = weighted_ || m.weighted;
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
    if (weighted_) updateWeighted();
  }

  // Y0_st = Σ_k w_ks w_kt b0_k; M = (I + Y0 Z)^-1; A = M Y0.
  void updateWeighted() {
    for (auto& row : Y0_)
      for (double& v : row) v = 0.0;
    for (const auto& m : modes_)
      for (int s = 0; s < 4; ++s)
        for (int t = 0; t < 4; ++t) Y0_[s][t] += double(m.w[s]) * m.w[t] * m.b0;
    invertWeighted();
  }
  void invertWeighted() {
    const auto& Y0 = Y0_;
    double a[4][8] = {};
    for (int s = 0; s < 4; ++s) {
      for (int t = 0; t < 4; ++t) a[s][t] = (s == t ? 1.0 : 0.0) + Y0[s][t] * z_[t];
      a[s][4 + s] = 1.0;
    }
    for (int c = 0; c < 4; ++c) {  // Gauss-Jordan with partial pivoting (the matrix is well conditioned)
      int p = c;
      for (int r = c + 1; r < 4; ++r)
        if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
      for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[p][k]);
      const double piv = a[c][c];
      for (int k = 0; k < 8; ++k) a[c][k] /= piv;
      for (int r = 0; r < 4; ++r) {
        if (r == c) continue;
        const double f = a[r][c];
        for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
      }
    }
    for (int s = 0; s < 4; ++s)
      for (int t = 0; t < 4; ++t) {
        M_[s][t] = static_cast<float>(a[s][4 + t]);
        double v = 0;
        for (int k = 0; k < 4; ++k) v += a[s][4 + k] * Y0[k][t];
        A_[s][t] = static_cast<float>(v);
      }
  }

  std::vector<Mode> modes_;
  float damper_ = 0.f;
  bool weighted_ = false;
  float z_[4] = {1.f, 1.f, 1.f, 1.f};
  float M_[4][4] = {}, A_[4][4] = {}, histW_[4] = {};
  double Y0_[4][4] = {};
  float fs_ = 96000.f, y0_ = 0.f, coupling_ = 1.f, hist_ = 0.f, hillHz_ = 0.f, hillLevel_ = 1.f;
  BridgeMode hill_{2600.f, 3.f, 0.15f};
};

}  // namespace sm
