// Synthetic radiation: bridge force -> sound at a microphone, in stereo
// (KB §5.3, §5.5, §12.3a).
//
// A bank of body modes, log-spaced from 180 Hz to 10 kHz (16 per octave to
// 1 kHz at Q 22, 24 per octave above at Q ~40 falling slowly; a little random
// jitter), each a peak-normalised band-pass driven by the total bridge force.
// Microphones share the modes and mix them with their own residues (KB
// §12.3a: shared poles, direction-specific residues):
//
//   - below ~600 Hz the body radiates as a monopole: the same residue in
//     every direction (KB §5.5, §12.1, §12.3a low band);
//   - above, each mode has a random radiation pattern on the sphere: real
//     spherical harmonics up to order ceil(k a) + 1 (a = 0.2 m), random
//     Gaussian coefficients, so it is smooth at 1 kHz and changes every few
//     degrees at 8 kHz; times the measured overall shape (the treble beams
//     front-up, weaker toward the floor; KB §12.1) [T]. Blended over
//     600-1200 Hz. Each pattern has unit mean power over the sphere.
//
// A spaced pair of omnis (1.5 m from the violin, `width` x kSpacing apart)
// hears the modes from slightly different directions, so neighbouring modes
// interfere differently in each ear: the response changes from semitone to
// semitone and differently left and right ("directional tone colour",
// Weinreich; KB §5.5). Vibrato then sweeps each partial through different
// peaks in each ear, the main "life" of string tone (KB §8). The mic's place
// (micAz, micEl) changes the colour the way placement does.
//
// The residues are calibrated in prepare(), at the default mic place, so the
// smoothed power response there matches a target curve (for the violin, the CC0 library-mic response with
// the synth's own bridge-force spectrum taken out: data/violin/,
// tools/combine_target.py). Controls (bridge hill, mute, body resonance,
// width) rescale the residues; changes glide over a few ms.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace sm {

struct RadiationSettings {
  float hillHz = 2600.f;   // bridge hill centre; the target was measured at baseHillHz
  float mute = 0.f;        // 0..1: a practice/orchestral mute (hill down, highs cut)
  float resonance = 1.f;   // 0..1: low body peaks and dips (1 = as measured, 0 = smoothed)
  float width = 1.f;       // 0..1: stereo width of the high band
  // Body character (KB §5.8): offsets from the measured response, in dB.
  float a0 = 0.f;          // A0 air resonance (~280 Hz): the one measured quality correlate
  float nasal = 0.f;       // 0.8-1.5 kHz: the "nasal" region (factory violins: strong)
  float brilliance = 0.f;  // the bridge hill's level (2-3 kHz)
  float softness = 0.f;    // cut above ~4 kHz (negative: more air)
  float voice = 0.f;       // F1-F3 shift, fraction: +0.06 ~ Tai's Stradivari vs other old Italians
  // The mic pair's place, in the player's frame (degrees): azimuth 0 = in
  // front of the player, +90 = the player's left (where the scroll points),
  // 180 = behind; elevation 0 = level with the violin, 90 = straight above.
  float micAz = 0.f;
  float micEl = 30.f;
  bool operator==(const RadiationSettings& o) const {
    return hillHz == o.hillHz && mute == o.mute && resonance == o.resonance && width == o.width && a0 == o.a0 &&
           nasal == o.nasal && brilliance == o.brilliance && softness == o.softness && voice == o.voice &&
           micAz == o.micAz && micEl == o.micEl;
  }
  bool sameMic(const RadiationSettings& o) const { return width == o.width && micAz == o.micAz && micEl == o.micEl; }
};

class Radiation {
 public:
  // lowCutHz: below the instrument's lowest air mode it radiates very little
  // (violin: weak at the open G, KB §12.7): below the air mode the f-holes'
  // air and the plates move in antiphase and cancel, like a vented speaker
  // box. A sharp-cornered Chebyshev high-pass there (kLowCutOrder, 0.5 dB
  // ripple). The modes' band-pass skirts fall only 6 dB/oct below them, so
  // with a gentle cut the modes just above A0 leaked into the open-G region:
  // the G3 fundamental came out ~17 dB stronger than in the recordings.
  void prepare(float fs, const float* targetHz, const float* targetDb, int targetCount, uint32_t seed, float baseHillHz,
               float lowCutHz) {
    fs_ = fs;
    lowCut_ = lowCutHz;
    setupLowCut();
    baseHill_ = baseHillHz;
    tHz_.assign(targetHz, targetHz + targetCount);
    tDb_.assign(targetDb, targetDb + targetCount);
    buildModes(seed);
    settings_ = RadiationSettings{};
    settings_.hillHz = baseHillHz;
    micGeometry(settings_);  // the calibration is at the default place
    calibrate();
    applySettings(settings_, true);
    // The audio loop reads plain arrays (structure of arrays, padded to 4
    // with silent modes) so it vectorises.
    voiceNow_ = 0.f;
    const std::size_t n = (modes_.size() + 3) & ~std::size_t(3);
    for (auto* v : {&b0_, &a1_, &a2_, &s1_, &s2_, &rl_, &rr_}) v->assign(n, 0.f);
    for (std::size_t k = 0; k < modes_.size(); ++k) {
      b0_[k] = modes_[k].b0;
      a1_[k] = modes_[k].a1;
      a2_[k] = modes_[k].a2;
    }
    syncResidues();
    reset();
  }

  void reset() {
    for (auto& m : modes_) m.s1 = m.s2 = 0.f;
    std::fill(s1_.begin(), s1_.end(), 0.f);
    std::fill(s2_.begin(), s2_.end(), 0.f);
    for (auto& h : hp_) h.s1 = h.s2 = 0.f;
  }

  // Control rate: new settings (cheap; recomputes target residues on change)
  // and the residues' glide toward them.
  void control(const RadiationSettings& s) {
    if (!(s == settings_)) {
      if (!s.sameMic(settings_)) micPending_ = true;
      settings_ = s;
      applySettings(s, false);
    }
    // Moving the mic re-evaluates every mode's pattern (~80k multiply-adds):
    // at most every kMicEvery control ticks; the residues glide meanwhile.
    if (micPending_ && ++micTick_ >= kMicEvery) {
      micTick_ = 0;
      micPending_ = false;
      micGeometry(settings_);
      applySettings(settings_, false);
    }
    for (auto& m : modes_) {
      m.rl += (m.tl - m.rl) * kGlide;
      m.rr += (m.tr - m.rr) * kGlide;
    }
    syncResidues();
    // Voice moves the modes themselves: glide, recomputing their filters.
    if (voiceNow_ != settings_.voice) {
      voiceNow_ += (settings_.voice - voiceNow_) * kGlide;
      if (std::fabs(settings_.voice - voiceNow_) < 1e-4f) voiceNow_ = settings_.voice;
      for (std::size_t k = 0; k < modes_.size(); ++k) {
        float b0, a1, a2;
        coefficients(modes_[k].hz * voiceScale(modes_[k].hz, voiceNow_), modes_[k].q, b0, a1, a2);
        b0_[k] = b0;
        a1_[k] = a1;
        a2_[k] = a2;
      }
    }
  }

  void process(float f, float& l, float& r) {
    for (auto& h : hp_) f = h.process(f);
    float L[4] = {0.f, 0.f, 0.f, 0.f}, R[4] = {0.f, 0.f, 0.f, 0.f};
    const std::size_t n = b0_.size();
    float* __restrict s1 = s1_.data();
    float* __restrict s2 = s2_.data();
    const float* __restrict b0 = b0_.data();
    const float* __restrict a1 = a1_.data();
    const float* __restrict a2 = a2_.data();
    const float* __restrict rl = rl_.data();
    const float* __restrict rr = rr_.data();
    for (std::size_t k = 0; k < n; k += 4) {
      for (std::size_t j = 0; j < 4; ++j) {
        const std::size_t i = k + j;
        const float y = b0[i] * f + s1[i];
        s1[i] = -a1[i] * y + s2[i];
        s2[i] = -b0[i] * f - a2[i] * y;
        L[j] += rl[i] * y;
        R[j] += rr[i] * y;
      }
    }
    l = ((L[0] + L[1]) + (L[2] + L[3])) * gain_;
    r = ((R[0] + R[1]) + (R[2] + R[3])) * gain_;
  }

  int modeCount() const { return static_cast<int>(modes_.size()); }

  // Labs: move the mic (and settle every glide) at once.
  void setNow(const RadiationSettings& s) {
    settings_ = s;
    micGeometry(s);
    applySettings(s, true);
    micPending_ = false;
    syncResidues();
  }
  // Labs: the residue of mode k toward a unit direction (pattern only, no level).
  float patternAt(int k, const float d[3]) const {
    float y[kMaxOrder1 * kMaxOrder1];
    realSH(d, modes_[k].order, y);
    return pattern(modes_[k], d, y);
  }
  float modeHz(int k) const { return modes_[k].hz; }

  // Magnitude (dB) of one channel's current response at hz (0 left, 1 right).
  float responseDb(float hz, int ch) const {
    std::complex<double> sum = 0;
    for (std::size_t k = 0; k < modes_.size(); ++k) {
      Mode m = modes_[k];  // with the current (voice-shifted) filter
      m.b0 = b0_[k];
      m.a1 = a1_[k];
      m.a2 = a2_[k];
      sum += static_cast<double>(ch == 0 ? m.rl : m.rr) * modeResponse(m, hz);
    }
    for (const auto& h : hp_) sum *= h.response(hz, fs_);
    return static_cast<float>(20.0 * std::log10(std::abs(sum) * gain_ + 1e-12));
  }
  // The calibration target (dB, peak 0) at hz.
  float targetDb(float hz) const { return target(hz); }

 private:
  // High-pass biquad sections.
  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    float process(float x) {
      const float y = b0 * x + s1;
      s1 = b1 * x - a1 * y + s2;
      s2 = b2 * x - a2 * y;
      return y;
    }
    std::complex<double> response(float hz, float fs) const {
      const double w = 2 * 3.141592653589793 * hz / fs;
      const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
      return (static_cast<double>(b0) + static_cast<double>(b1) * z1 + static_cast<double>(b2) * z2) /
             (1.0 + static_cast<double>(a1) * z1 + static_cast<double>(a2) * z2);
    }
  };
  // Chebyshev type I high-pass: the low-pass prototype's pole pairs, each
  // mapped to a high-pass section (natural frequency fc/|p|, Q |p|/-2Re p),
  // then the RBJ bilinear high-pass. Passband ripple peaks at +kLowCutRipple.
  void setupLowCut() {
    const double pi = 3.141592653589793, n = kLowCutSections * 2;
    const double eps = std::sqrt(std::pow(10.0, kLowCutRipple / 10.0) - 1.0);
    const double mu = std::asinh(1.0 / eps) / n;
    for (int i = 0; i < kLowCutSections; ++i) {
      const double th = pi * (2 * i + 1) / (2 * n);
      const double re = -std::sinh(mu) * std::sin(th), im = std::cosh(mu) * std::cos(th);
      const double mag = std::sqrt(re * re + im * im);
      const double f0 = std::min(lowCut_ / mag, 0.45 * fs_), q = mag / (-2 * re);
      const double w0 = 2 * pi * f0 / fs_, cw = std::cos(w0), al = std::sin(w0) / (2 * q);
      const double a0 = 1 + al;
      hp_[i].b0 = static_cast<float>((1 + cw) / 2 / a0);
      hp_[i].b1 = static_cast<float>(-(1 + cw) / a0);
      hp_[i].b2 = hp_[i].b0;
      hp_[i].a1 = static_cast<float>(-2 * cw / a0);
      hp_[i].a2 = static_cast<float>((1 - al) / a0);
    }
  }

  struct Mode {
    float hz, q;
    float b0 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    float amp = 1, ampSmooth = 1;      // calibrated level (as measured / smoothed low band)
    float gl = 1, gr = 1;              // per-channel residues toward the left / right mic
    int order = 0;                     // spherical-harmonic order of its pattern (0: monopole)
    std::size_t coef = 0;              // its coefficients in coef_
    float blend = 0.f;                 // 0 below 600 Hz (monopole) .. 1 above 1.2 kHz (pattern)
    float envNorm = 1.f;               // 1 / rms of the overall shape over the sphere
    float tl = 0, tr = 0, rl = 0, rr = 0;
  };
  static constexpr float kGlide = 0.06f;
  static constexpr float kHighDensity = 24.f;
  static constexpr float kLowQ = 22.f, kHighQ = 40.f;  // per control tick (~5 ms at 48 kHz, 16-sample ticks)

  float target(float hz) const {
    const float lx = std::log(hz);
    if (hz <= tHz_.front()) return tDb_.front();
    if (hz >= tHz_.back()) return tDb_.back();
    const auto it = std::upper_bound(tHz_.begin(), tHz_.end(), hz);
    const std::size_t i = static_cast<std::size_t>(it - tHz_.begin());
    const float x0 = std::log(tHz_[i - 1]), x1 = std::log(tHz_[i]);
    const float t = (lx - x0) / (x1 - x0);
    return tDb_[i - 1] + t * (tDb_[i] - tDb_[i - 1]);
  }

  // Smoothed target (log-frequency Gaussian, sigma in octaves), in dB.
  float smoothTarget(float hz, float sigmaOct) const {
    double num = 0, den = 0;
    for (int j = -24; j <= 24; ++j) {
      const double o = j * sigmaOct / 8.0;
      const double w = std::exp(-0.5 * (o / sigmaOct) * (o / sigmaOct));
      num += w * std::pow(10.0, target(static_cast<float>(hz * std::pow(2.0, o))) / 10.0);
      den += w;
    }
    return static_cast<float>(10.0 * std::log10(num / den));
  }

  static std::complex<double> modeResponse(const Mode& m, float hz, double fs) {
    const double w = 2 * 3.141592653589793 * hz / fs;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    return (static_cast<double>(m.b0) * (1.0 - z2)) / (1.0 + static_cast<double>(m.a1) * z1 + static_cast<double>(m.a2) * z2);
  }
  std::complex<double> modeResponse(const Mode& m, float hz) const { return modeResponse(m, hz, fs_); }

  // Peak-normalised band-pass (bilinear, prewarped).
  void coefficients(float hz, float q, float& b0, float& a1, float& a2) const {
    const double w = 2 * 3.141592653589793 * hz;
    const double K = w / std::tan(w / (2.0 * fs_));
    const double a = w / q, b = w * w;
    const double a0 = K * K + a * K + b;
    b0 = static_cast<float>(a * K / a0);
    a1 = static_cast<float>((2 * b - 2 * K * K) / a0);
    a2 = static_cast<float>((K * K - a * K + b) / a0);
  }

  // Voice: scale the formant region (F1-F3, below ~3 kHz) by 1 + v, fading to
  // no shift by 4.5 kHz so F4 and the top stay put (Tai et al.: F4 ~1%).
  static float voiceScale(float hz, float v) {
    const float t = hz <= 3000.f ? 1.f : (hz >= 4500.f ? 0.f : 0.5f * (1.f + std::cos(3.14159265f * (hz - 3000.f) / 1500.f)));
    return 1.f + v * t;
  }

  // A smooth bump in log frequency (1 at the centre).
  static float bump(float hz, float centre, float sigmaOct) {
    const float o = std::log2(hz / centre) / sigmaOct;
    return std::exp(-0.5f * o * o);
  }

  void buildModes(uint32_t seed) {
    modes_.clear();
    uint32_t r = seed;
    auto uni = [&r]() {
      r = r * 1664525u + 1013904223u;
      return static_cast<float>(r >> 8) * (1.f / 16777216.f);
    };
    auto gauss = [&]() {
      const float a = std::max(uni(), 1e-7f), b = uni();
      return std::sqrt(-2.f * std::log(a)) * std::cos(6.2831853f * b);
    };
    // 16 modes per octave up to 1 kHz, kHighDensity above; Q kLowQ below
    // 1 kHz, kHighQ above (falling slowly with frequency).
    float lhz = std::log2(180.f);
    for (;;) {
      const float base = std::pow(2.f, lhz);
      if (base > 10000.f || base > 0.42f * fs_) break;
      const float per = base < 1000.f ? 16.f : kHighDensity;
      lhz += 1.f / per;
      float hz = base;
      if (hz > 600.f) hz *= std::pow(2.f, (uni() - 0.5f) * 0.6f / per);  // jitter ±0.3 spacing
      Mode m;
      m.hz = hz;
      m.q = hz < 1000.f ? kLowQ : kHighQ * std::pow(1000.f / hz, 0.15f);
      // (Four draws kept from the earlier per-channel residues, so the modes'
      // frequencies, which use the same sequence, stay where they were.)
      uni();
      uni();
      gauss();
      gauss();
      // Radiation pattern: a monopole below 600 Hz, random spherical
      // harmonics above, blended over an octave (KB §12.3a).
      m.blend = std::clamp((std::log2(hz) - std::log2(600.f)) / 1.f, 0.f, 1.f);
      if (m.blend > 0.f) {
        const float ka = 2.f * 3.14159265f * hz * kRadius / kSound;
        m.order = std::min(static_cast<int>(std::ceil(ka)) + 1, kMaxOrder1 - 1);
      }
      coefficients(hz, m.q, m.b0, m.a1, m.a2);
      modes_.push_back(m);
    }
    // Coefficients (own random stream), scaled to unit mean power over the
    // sphere: sum c^2 = 4 pi for orthonormal harmonics.
    coef_.clear();
    uint32_t r2 = seed ^ 0x9e3779b9u;
    auto uni2 = [&r2]() {
      r2 = r2 * 1664525u + 1013904223u;
      return static_cast<float>(r2 >> 8) * (1.f / 16777216.f);
    };
    for (auto& m : modes_) {
      m.coef = coef_.size();
      if (m.blend <= 0.f) continue;
      const int n = (m.order + 1) * (m.order + 1);
      double ss = 0;
      for (int i = 0; i < n; ++i) {
        const float a = std::max(uni2(), 1e-7f), b = uni2();
        const float g = std::sqrt(-2.f * std::log(a)) * std::cos(6.2831853f * b);
        coef_.push_back(g);
        ss += static_cast<double>(g) * g;
      }
      const float k = static_cast<float>(std::sqrt(4.0 * 3.141592653589793 / ss));
      for (int i = 0; i < n; ++i) coef_[m.coef + i] *= k;
    }
    // The overall shape's rms over the sphere, per mode (a 256-point
    // Fibonacci sphere), so each pattern keeps unit mean power.
    for (auto& m : modes_) {
      double p = 0;
      for (int i = 0; i < 256; ++i) {
        float d[3];
        fibonacci(i, 256, d);
        const float g = envelope(m.hz, d);
        p += static_cast<double>(g) * g;
      }
      m.envNorm = static_cast<float>(1.0 / std::sqrt(p / 256.0));
    }
  }

  // Point i of n on a Fibonacci sphere (unit vector).
  static void fibonacci(int i, int n, float d[3]) {
    const float z = 1.f - (2.f * i + 1.f) / n;
    const float rho = std::sqrt(std::max(0.f, 1.f - z * z));
    const float phi = 2.39996323f * i;  // the golden angle
    d[0] = rho * std::cos(phi);
    d[1] = rho * std::sin(phi);
    d[2] = z;
  }

  // The measured overall shape (KB §12.1, Pätynen & Lokki): above ~1 kHz the
  // treble beams toward front-up (at 4-8 kHz only the front is within -3 dB),
  // and at 2-6.3 kHz the bottom elevations are considerably weaker. Gain
  // (linear) toward unit direction d in the player's frame (x front, y the
  // player's left, z up) [T: kBeamDb, kFloorDb, the axis].
  static float envelope(float hz, const float d[3]) {
    const float w = std::clamp(std::log2(hz / 1000.f) / 2.f, 0.f, 1.f);  // 0 at 1 kHz .. 1 at 4 kHz+
    const float ce = std::cos(kBeamEl), se = std::sin(kBeamEl);
    const float cosg = d[0] * ce + d[2] * se;
    float db = w * kBeamDb * (cosg - 1.f);
    const float wf = std::clamp(std::log2(hz / 1500.f) / 0.5f, 0.f, 1.f) * std::clamp(std::log2(9000.f / hz) / 0.5f, 0.f, 1.f);
    db -= wf * kFloorDb * std::max(0.f, -d[2]);
    return std::pow(10.f, db / 20.f);
  }

  // Real, orthonormal spherical harmonics up to order n at unit direction d:
  // y[l*l + l + m], m = -l..l (cos terms for m > 0, sin for m < 0).
  static void realSH(const float d[3], int n, float* y) {
    const double x = std::clamp(static_cast<double>(d[2]), -1.0, 1.0);
    const double st = std::sqrt(std::max(0.0, 1.0 - x * x));
    const double phi = std::atan2(static_cast<double>(d[1]), static_cast<double>(d[0]));
    const double pi = 3.141592653589793;
    double pmm = 1.0 / std::sqrt(4.0 * pi);  // normalised P_m^m
    for (int m = 0; m <= n; ++m) {
      if (m > 0) pmm *= std::sqrt((2.0 * m + 1.0) / (2.0 * m)) * st;
      const double cm = m == 0 ? 1.0 : std::sqrt(2.0) * std::cos(m * phi);
      const double sm = std::sqrt(2.0) * std::sin(m * phi);
      double p2 = 0.0, p1 = pmm;  // P_{l-2}^m, P_{l-1}^m
      for (int l = m; l <= n; ++l) {
        double p;
        if (l == m) {
          p = pmm;
        } else if (l == m + 1) {
          p = std::sqrt(2.0 * m + 3.0) * x * pmm;
        } else {
          const double a = std::sqrt((4.0 * l * l - 1.0) / (static_cast<double>(l) * l - static_cast<double>(m) * m));
          const double b = std::sqrt((static_cast<double>(l - 1) * (l - 1) - static_cast<double>(m) * m) /
                                     (4.0 * (l - 1) * (l - 1) - 1.0));
          p = a * (x * p1 - b * p2);
        }
        if (l > m) {
          p2 = p1;
          p1 = p;
        }
        y[l * l + l + m] = static_cast<float>(p * cm);
        if (m > 0) y[l * l + l - m] = static_cast<float>(p * sm);
      }
    }
  }

  // Mode m's residue toward d (y: its harmonics there, up to its order).
  float pattern(const Mode& m, const float d[3], const float* y) const {
    if (m.blend <= 0.f) return 1.f;
    const int n = (m.order + 1) * (m.order + 1);
    float s = 0.f;
    const float* c = coef_.data() + m.coef;
    for (int i = 0; i < n; ++i) s += c[i] * y[i];
    return (1.f - m.blend) + m.blend * s * envelope(m.hz, d) * m.envNorm;
  }

  // The pair's directions from the violin, and every mode's residue toward
  // each. Omnis kDistance from the violin at (micAz, micEl), spaced
  // width x kSpacing along the line across the mic's view (left = the
  // listener's left, looking at the player).
  void micGeometry(const RadiationSettings& s) {
    const float az = s.micAz * 3.14159265f / 180.f, el = std::clamp(s.micEl, -89.9f, 89.9f) * 3.14159265f / 180.f;
    const float c[3] = {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};
    // Left, seen from the mic looking at the player: up x (-horizontal view).
    const float lx = std::sin(az), ly = -std::cos(az);
    const float half = 0.5f * std::clamp(s.width, 0.f, 1.f) * kSpacing;
    float dl[3], dr[3];
    for (int i = 0; i < 3; ++i) {
      const float u = i == 0 ? lx : (i == 1 ? ly : 0.f);
      dl[i] = kDistance * c[i] + half * u;
      dr[i] = kDistance * c[i] - half * u;
    }
    for (float* v : {dl, dr}) {
      const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      for (int i = 0; i < 3; ++i) v[i] /= len;
    }
    realSH(dl, kMaxOrder1 - 1, yL_);
    realSH(dr, kMaxOrder1 - 1, yR_);
    for (auto& m : modes_) {
      m.gl = pattern(m, dl, yL_);
      m.gr = pattern(m, dr, yR_);
    }
  }

  // Fit the modes' common levels so the channels' mean power, smoothed,
  // follows the target: 1/12-octave detail below 1 kHz (the low body peaks
  // and dips), 1/3 octave above (where only the envelope is meaningful). A
  // second pass fits the smoothed low band (Body Resonance 0). Responses and
  // smoothing weights are computed once: this runs in prepare, which in the
  // browser is when audio starts.
  void calibrate() {
    std::vector<float> grid;
    for (float f = 160.f; f < std::min(12000.f, 0.45f * fs_); f *= std::pow(2.f, 1.f / 32.f)) grid.push_back(f);
    const std::size_t K = modes_.size(), G = grid.size();
    std::vector<std::complex<float>> H(K * G);
    std::vector<float> hpPow(G), wDetail(K * G), wSmooth(K * G);
    for (std::size_t g = 0; g < G; ++g) {
      double hp = 1;
      for (const auto& h : hp_) hp *= std::norm(h.response(grid[g], fs_));
      hpPow[g] = static_cast<float>(hp);
    }
    std::vector<float> wantDetail(K), wantSmooth(K);
    for (std::size_t k = 0; k < K; ++k) {
      const Mode& m = modes_[k];
      const double sig = m.hz < 1000.f ? 1.0 / 12.0 : 1.0 / 3.0;
      double sd = 0, ss = 0;
      for (std::size_t g = 0; g < G; ++g) {
        H[k * G + g] = std::complex<float>(modeResponse(m, grid[g]));
        const double o = std::log2(grid[g] / m.hz);
        const double wd = std::exp(-0.5 * (o / sig) * (o / sig)), ws = std::exp(-0.5 * (o / 0.5) * (o / 0.5));
        wDetail[k * G + g] = static_cast<float>(wd);
        wSmooth[k * G + g] = static_cast<float>(ws);
        sd += wd;
        ss += ws;
      }
      for (std::size_t g = 0; g < G; ++g) {
        wDetail[k * G + g] /= static_cast<float>(sd);
        wSmooth[k * G + g] /= static_cast<float>(ss);
      }
      wantDetail[k] = smoothTarget(m.hz, static_cast<float>(sig));
      wantSmooth[k] = smoothTarget(m.hz, 0.5f);
    }
    for (auto& m : modes_) {
      m.amp = std::pow(10.f, target(m.hz) / 20.f);
      m.ampSmooth = std::pow(10.f, smoothTarget(m.hz, 0.5f) / 20.f);
    }
    std::vector<float> pw(G);
    for (int pass = 0; pass < 2; ++pass) {
      const bool smoothPass = pass == 1;
      const std::vector<float>& W = smoothPass ? wSmooth : wDetail;
      const std::vector<float>& want = smoothPass ? wantSmooth : wantDetail;
      // The detail pass needs more: a mode whose pattern is near a null toward
      // the calibration place moves its band slowly.
      for (int it = 0; it < (smoothPass ? 10 : 30); ++it) {
        for (std::size_t g = 0; g < G; ++g) {
          std::complex<float> L = 0, R = 0;
          for (std::size_t k = 0; k < K; ++k) {
            const Mode& m = modes_[k];
            const float a = smoothPass ? m.ampSmooth : m.amp;
            const std::complex<float> h = H[k * G + g];
            L += (a * m.gl) * h;
            R += (a * m.gr) * h;
          }
          pw[g] = 0.5f * (std::norm(L) + std::norm(R)) * hpPow[g];
        }
        for (std::size_t k = 0; k < K; ++k) {
          float have = 0.f;
          for (std::size_t g = 0; g < G; ++g) have += W[k * G + g] * pw[g];
          const float haveDb = 10.f * std::log10(have + 1e-30f);
          const float step = std::clamp((want[k] - haveDb) * 0.7f, -6.f, 6.f);
          (smoothPass ? modes_[k].ampSmooth : modes_[k].amp) *= std::pow(10.f, step / 20.f);
        }
      }
    }
    // Overall gain: peak of the response = 0 dB (the target's peak).
    gain_ = 1.f;
    float peak = 0.f;
    for (std::size_t g = 0; g < G; ++g) {
      std::complex<float> L = 0;
      for (std::size_t k = 0; k < K; ++k) L += (modes_[k].amp * modes_[k].gl) * H[k * G + g];
      peak = std::max(peak, std::abs(L));
    }
    norm_ = 1.f / std::max(peak, 1e-9f);
  }

  // Bridge hill as a 2nd-order resonance shape (KB §5.3), |H| at hz.
  static float hillShape(float hz, float centre, float q) {
    const float x = hz / centre;
    return (x / q) / std::sqrt((1.f - x * x) * (1.f - x * x) + (x / q) * (x / q));
  }

  void applySettings(const RadiationSettings& s, bool snap) {
    // Mute: adds mass to the bridge, moving the hill down (to ~65%) and
    // cutting the highs (KB §11.2 "Mute") [T].
    const float hill = s.hillHz * (1.f - 0.35f * s.mute);
    for (auto& m : modes_) {
      float a = m.amp + (m.ampSmooth - m.amp) * (1.f - s.resonance) * (m.hz < 1200.f ? 1.f : 0.f);
      // Move the hill: the ratio of the new hill shape to the measured one,
      // normalised so frequencies well below the hill don't move, limited to
      // ±12 dB.
      const float ratio = hillShape(m.hz, hill, 2.5f) / hillShape(m.hz, baseHill_, 2.5f) * (hill / baseHill_);
      a *= std::clamp(ratio, 0.25f, 4.f);
      // Mute's extra high cut: up to -5 dB above ~1.5 kHz (with the hill's
      // move, about -10 to -12 dB in the bridge-hill region) [T].
      const float hi = 1.f / (1.f + std::pow(1500.f / m.hz, 4.f));
      a *= std::pow(10.f, -5.f * s.mute * hi / 20.f);
      // Body character (KB §5.8): bumps at the regions the research names,
      // placed where the mode sounds (after Voice) [T: widths].
      const float hz = m.hz * voiceScale(m.hz, s.voice);
      const float soft = 1.f / (1.f + std::pow(4000.f / hz, 4.f));
      const float db = s.a0 * bump(hz, 280.f, 0.2f) + s.nasal * bump(hz, 1100.f, 0.4f) +
                       s.brilliance * bump(hz, hill, 0.4f) - s.softness * soft;
      a *= std::pow(10.f, db / 20.f);
      // Each mic's own residue (its direction; the pair's spacing is the width).
      m.tl = a * norm_ * m.gl;
      m.tr = a * norm_ * m.gr;
      if (snap) {
        m.rl = m.tl;
        m.rr = m.tr;
      }
    }
  }

  void syncResidues() {
    for (std::size_t k = 0; k < modes_.size() && k < rl_.size(); ++k) {
      rl_[k] = modes_[k].rl;
      rr_[k] = modes_[k].rr;
    }
  }

  // Radiation geometry [T]: effective radius, speed of sound, the mic pair.
  static constexpr float kRadius = 0.2f;       // m (violin, KB §12.3a)
  static constexpr float kSound = 343.f;       // m/s
  static constexpr int kMaxOrder1 = 40;        // harmonic orders 0..39 (ka ~ 37 at 10 kHz)
  static constexpr float kDistance = 1.5f;     // m, violin to the pair
  static constexpr float kSpacing = 0.6f;      // m between the omnis at width 1
  static constexpr float kBeamEl = 0.61f;      // rad (35 deg): the treble beam's elevation
  static constexpr float kBeamDb = 4.5f;       // dB per unit of (cos angle - 1) from the beam, at 4 kHz+
  static constexpr float kFloorDb = 3.f;       // dB toward straight down, 2-6.3 kHz
  static constexpr int kMicEvery = 16;         // control ticks between pattern re-evaluations
  std::vector<float> coef_;
  float yL_[kMaxOrder1 * kMaxOrder1]{}, yR_[kMaxOrder1 * kMaxOrder1]{};
  bool micPending_ = false;
  int micTick_ = 0;
  std::vector<Mode> modes_;
  std::vector<float> b0_, a1_, a2_, s1_, s2_, rl_, rr_;  // audio-loop copies (SoA)
  static constexpr int kLowCutSections = 3;    // order 6
  static constexpr double kLowCutRipple = 0.5;  // dB
  Biquad hp_[kLowCutSections];
  std::vector<float> tHz_, tDb_;
  RadiationSettings settings_;
  float fs_ = 48000.f, baseHill_ = 2600.f, gain_ = 1.f, norm_ = 1.f, lowCut_ = 230.f, voiceNow_ = 0.f;
};

}  // namespace sm
