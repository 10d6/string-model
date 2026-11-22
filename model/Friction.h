// Bow–string friction (instrument-research KB §3).
//
// ThermalFriction is Woodhouse's 2003 model (KB §3.2a): friction depends only
// on the contact temperature, mu = mu(T), and T follows the frictional power.
// At any instant mu is fixed, so the junction (string line against a stepped
// friction law) has exactly one solution: no hysteresis rule, no iteration.
// Hysteresis in the force–velocity plane comes from the thermal lag.
//
// Heat flow out of the contact (conduction into string and bow) is a
// half-space disk: its admittance is ~ A*lambda0*sqrt(s) at short times and
// 4*K*a at long times. The sqrt(s) part is approximated by a Foster network of
// RC branches with log-spaced time constants (G_k = A*lambda*ln(r)/(pi*sqrt(tau_k))
// makes the sum ~ A*lambda*sqrt(s) between the smallest and largest tau).
//
// The bow side is different: the hair moves through the contact at v_b, so
// each bit of hair is heated only for 2a/v_b. That is a moving heat source
// (Peclet number v_b a / D ~ 100s), whose conductance is about
// lambda_bow * A * sqrt(v_b / (2 pi a)), and the rosin carried in by the bow
// adds convection 2 a delta rho c v_b. Without these terms fast strokes run
// away thermally (continuous slip).
//
// ClassicFriction is the hyperbolic friction curve with the hysteresis rule,
// solved in closed form (KB §3.4), kept for A/B comparison.
#pragma once

#include <algorithm>
#include <cmath>

namespace sm {

struct JunctionResult {
  float f = 0.f;      // friction force on the string (N), in the bow's direction
  float v = 0.f;      // string velocity at the bow (m/s)
  bool slipping = false;
};

class ThermalFriction {
 public:
  // Physical constants (Woodhouse 2003, Table I) and tuning.
  float lambdaString = 2050.f;  // sqrt(K rho c) of the string  [W s^0.5 / (m^2 K)]
  float lambdaBow = 450.f;      // of the bow hair
  float kString = 1.4f;         // string conductivity (W/m K), long-time 4 K a term
  float kBow = 0.2f;
  float a0 = 250e-6f;         // contact radius at 1 N (m); area ∝ normal force
  float delta = 1e-6f;        // active rosin layer (m)
  float rhoC = 1010.f * 250.f;// rosin volumetric heat capacity (J / m^3 K)
  float heatScale = 3.f;      // calibration of the heat path ([T], see docs/MODEL.md)
  float muStatic = 1.2f, muDynamic = 0.35f, tDrop = 40.f;  // mu(T): 1.2 at ambient, 0.35 at >= tDrop K

  void prepare(float fs) {
    dt_ = 1.f / fs;
    float tau = 2e-5f;
    for (int k = 0; k < kBranches; ++k) {
      tau_[k] = tau;
      relax_[k] = 1.f - std::exp(-dt_ / tau);
      invSqrtTau_[k] = 1.f / std::sqrt(tau);
      tau *= kRatio;
    }
    idleDecay_ = std::exp(-dt_ / 0.05f);  // ~50 ms cooling with the bow off [T]
    reset();
  }
  void reset() {
    temp_ = 0.f;
    for (float& u : u_) u = 0.f;
  }

  float temperature() const { return temp_; }

  // Bow off the string: the contact cools toward ambient. A cheap stand-in for
  // update() with no power (the full network isn't needed until the bow lands).
  void coolIdle() {
    if (temp_ == 0.f) return;
    temp_ *= idleDecay_;
    for (float& u : u_) u *= idleDecay_;
    if (temp_ < 1e-3f) {
      temp_ = 0.f;
      for (float& u : u_) u = 0.f;
    }
  }

  float mu() const {
    // Smooth piecewise-linear: full drop by tDrop, rounded at both ends.
    const float x = temp_ / tDrop;
    const float s = x <= 0.f ? 1.f : (x >= 1.f ? 0.f : 1.f - x * x * (3.f - 2.f * x));
    return muDynamic + (muStatic - muDynamic) * s;
  }

  // Solve the bow junction: v = vh + f/(2 Zeff), |f| <= Fb*mu, f opposes slip.
  // noise in [0,1): during slip mu is scaled by (1 - noiseAmt*noise) (KB §6.4).
  JunctionResult solve(float vh, float vb, float fb, float zeff, float noiseAmt, float noise) const {
    JunctionResult r;
    if (fb <= 0.f) {
      r.v = vh;
      return r;
    }
    const float m = mu();
    const float a = vb - vh;
    const float fStick = 2.f * zeff * a;
    if (std::fabs(fStick) <= fb * m) {
      r.f = fStick;
      r.v = vb;
      return r;
    }
    const float ms = m * (1.f - noiseAmt * noise);
    r.f = (a > 0.f ? 1.f : -1.f) * fb * ms;
    r.v = vh + r.f / (2.f * zeff);
    r.slipping = true;
    return r;
  }

  // Advance the contact temperature by one sample, given this sample's
  // junction result (power f*(vb - v) heats the contact).
  void update(const JunctionResult& r, float vb, float fb) {
    const float slip = std::fabs(vb - r.v);
    const float power = std::fabs(r.f) * slip;
    const float force = std::max(fb, 0.02f);     // contact never vanishes in the thermal path
    const float area = 3.14159265f * a0 * a0 * force;
    const float radius = a0 * std::sqrt(force);
    const float c0 = rhoC * area * delta;
    const float vbow = std::fabs(vb);
    const float gInf = 4.f * (kString + kBow) * radius;
    const float gMove = lambdaBow * area * std::sqrt(vbow / (2.f * 3.14159265f * radius));
    const float gConv = 2.f * radius * delta * rhoC * (vbow + slip);
    const float gUnit = heatScale * lambdaString * area * kLnRatio / 3.14159265f;
    float gSum = 0.f, gu = 0.f;
    for (int k = 0; k < kBranches; ++k) {
      const float g = gUnit * invSqrtTau_[k];
      gSum += g;
      gu += g * u_[k];
    }
    // Implicit (backward Euler) contact temperature: stable for any c0.
    const float cdt = c0 / dt_;
    temp_ = (cdt * temp_ + power + gu) / (cdt + gSum + heatScale * (gInf + gMove) + gConv);
    if (!(temp_ >= 0.f)) temp_ = 0.f;
    for (int k = 0; k < kBranches; ++k) u_[k] += (temp_ - u_[k]) * relax_[k];
  }

 private:
  static constexpr int kBranches = 7;
  static constexpr float kRatio = 5.f;
  static constexpr float kLnRatio = 1.6094379f;  // ln 5
  float tau_[kBranches]{}, relax_[kBranches]{}, invSqrtTau_[kBranches]{}, u_[kBranches]{};
  float temp_ = 0.f, dt_ = 1.f / 96000.f, idleDecay_ = 0.9998f;
};

// Hyperbolic friction curve mu = muD + (muS - muD) v0 / (v0 + |u|) with the
// hysteresis rule (KB §3.4): stick until |a| > k muS, then slip while a real
// root exists.
class ClassicFriction {
 public:
  float muStatic = 0.8f, muDynamic = 0.3f, v0 = 0.15f;

  void reset() { slipping_ = false; }

  JunctionResult solve(float vh, float vb, float fb, float zeff, float noiseAmt, float noise) {
    JunctionResult r;
    if (fb <= 0.f) {
      slipping_ = false;
      r.v = vh;
      return r;
    }
    const float a = vb - vh;
    const float k = fb / (2.f * zeff);
    const float aa = std::fabs(a);
    const float dmu = muStatic - muDynamic;
    if (!slipping_ && aa <= k * muStatic) {
      r.f = 2.f * zeff * a;
      r.v = vb;
      return r;
    }
    // Slip: |u| from u^2 + (v0 - b) u - v0 (b - k dmu) = 0, b = |a| - k muD.
    const float b = aa - k * muDynamic;
    const float disc = (v0 - b) * (v0 - b) + 4.f * v0 * (b - k * dmu);
    float u = disc >= 0.f ? 0.5f * ((b - v0) + std::sqrt(disc)) : -1.f;
    if (u <= 0.f) {  // no slipping solution: re-stick
      slipping_ = false;
      if (aa <= k * muStatic) {
        r.f = 2.f * zeff * a;
        r.v = vb;
        return r;
      }
      u = std::max(0.f, b);  // shouldn't happen; slide at the dynamic level
    }
    slipping_ = true;
    const float s = a > 0.f ? 1.f : -1.f;
    float m = muDynamic + dmu * v0 / (v0 + u);
    m *= 1.f - noiseAmt * noise;
    r.f = s * fb * m;
    r.v = vh + r.f / (2.f * zeff);
    r.slipping = true;
    return r;
  }

 private:
  bool slipping_ = false;
};

}  // namespace sm
