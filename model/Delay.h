// Fractional delay line for the waveguide strings.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace sm {

// A power-of-two ring buffer read with 4-point Lagrange (cubic) interpolation.
// Convention: read(d) returns the sample written d samples ago (d = 1 is the
// last write). Read a line before writing it in the same sample; d >= 2.
class Delay {
 public:
  void prepare(int maxSamples) {
    int n = 8;
    while (n < maxSamples + 8) n <<= 1;
    buf_.assign(static_cast<std::size_t>(n), 0.f);
    mask_ = n - 1;
    w_ = 0;
  }
  void clear() {
    for (auto& x : buf_) x = 0.f;
  }
  void write(float x) {
    w_ = (w_ + 1) & mask_;
    buf_[static_cast<std::size_t>(w_)] = x;
  }
  float read(float d) const {
    const float fl = std::floor(d);
    const float t = d - fl;
    const int i = static_cast<int>(fl);
    // Points at delays i-1, i, i+1, i+2; t in [0,1) between i and i+1.
    const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
    // Lagrange 3rd order with nodes -1, 0, 1, 2.
    const float c0 = y0;
    const float c1 = y1 - ym1 * (1.f / 3.f) - y0 * 0.5f - y2 * (1.f / 6.f);
    const float c2 = 0.5f * (ym1 + y1) - y0;
    const float c3 = (1.f / 6.f) * (y2 - ym1) + 0.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + c0;
  }
  // Integer delay d >= 1 (no interpolation).
  float tap(int d) const { return at(d); }
  int capacity() const { return mask_ - 4; }

 private:
  // d samples ago, with d counted so that d = 1 is the last write.
  float at(int d) const { return buf_[static_cast<std::size_t>((w_ - d + 1) & mask_)]; }
  std::vector<float> buf_;
  int mask_ = 0, w_ = 0;
};

// One-pole low-pass y = (1-a) x + a y[-1], used as termination loss.
struct OnePoleLP {
  float a = 0.f, y = 0.f;
  void setCutoff(float hz, float fs) { a = std::exp(-2.f * 3.14159265f * hz / fs); }
  float process(float x) { return y = (1.f - a) * x + a * y; }
  // Phase delay in samples at frequency hz (for tuning the loop).
  float delayAt(float hz, float fs) const {
    const float w = 2.f * 3.14159265f * hz / fs;
    // H = (1-a)/(1 - a e^{-jw}); phase = -atan(a sin w / (1 - a cos w))
    const float ph = std::atan2(a * std::sin(w), 1.f - a * std::cos(w));
    return w > 0 ? ph / w : a / (1.f - a);
  }
  float gainAt(float hz, float fs) const {
    const float w = 2.f * 3.14159265f * hz / fs;
    return (1.f - a) / std::sqrt(1.f - 2.f * a * std::cos(w) + a * a);
  }
};

// First-order loss shelf for a string termination: per pass, gain gLow on the
// low partials and gHigh (< gLow) on the high ones, crossing over around
// fT. On a log scale the loss is ~ ln(1/gLow) + ln(gLow/gHigh) f^2/(f^2 + fT^2):
// Valette-Cuesta's damping (KB §2.3), rising like f^2 and saturating.
// Analog H(s) = gHigh (s + wz)/(s + wp), wp = wT sqrt(gHigh/gLow), wz = wT
// sqrt(gLow/gHigh); bilinear, pre-warped at fT. Minimum phase, stable.
struct LossShelf {
  float b0 = 1.f, b1 = 0.f, a1 = 0.f, x1 = 0.f, y = 0.f;
  void set(float gLow, float gHigh, float fT, float fs) {
    gHigh = std::min(gHigh, gLow);
    const float k = std::tan(3.14159265f * std::min(fT, 0.45f * fs) / fs);  // pre-warped wT / (2 fs)
    const float r = std::sqrt(gHigh / gLow);
    const float p = k * r, z = k / r;  // pole and zero, warped
    // H(z) = gHigh (1 + z - (1 - z) z^-1) / (1 + p - (1 - p) z^-1)
    const float n = 1.f / (1.f + p);
    b0 = gHigh * (1.f + z) * n;
    b1 = -gHigh * (1.f - z) * n;
    a1 = -(1.f - p) * n;
  }
  float process(float x) {
    y = b0 * x + b1 * x1 - a1 * y;
    x1 = x;
    return y;
  }
  void clear() { x1 = y = 0.f; }
  // Phase delay in samples at frequency hz (for tuning the loop).
  float delayAt(float hz, float fs) const {
    const float w = 2.f * 3.14159265f * hz / fs;
    if (w <= 0.f) return (b1 + b0 * 0.f) == 0.f ? 0.f : 0.f;
    // H = (b0 + b1 e^{-jw}) / (1 + a1 e^{-jw})
    const float nr = b0 + b1 * std::cos(w), ni = -b1 * std::sin(w);
    const float dr = 1.f + a1 * std::cos(w), di = -a1 * std::sin(w);
    const float ph = std::atan2(ni, nr) - std::atan2(di, dr);
    return -ph / w;
  }
  float gainAt(float hz, float fs) const {
    const float w = 2.f * 3.14159265f * hz / fs;
    const float nr = b0 + b1 * std::cos(w), ni = -b1 * std::sin(w);
    const float dr = 1.f + a1 * std::cos(w), di = -a1 * std::sin(w);
    return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
  }
};

}  // namespace sm
