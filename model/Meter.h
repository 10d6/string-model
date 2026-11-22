#pragma once
// Metric strength of a note's position in the bar (plain C++, no synthkit).
// Used by the performer's metric habits (player doc §3.1, §4: KTH-style
// metric patterns): 1 the downbeat, 0.7 a mid-bar strong beat, 0.5 a beat,
// 0.25 a subdivision of the beat, 0.1 anything finer [T: levels].
#include <algorithm>
#include <cmath>

namespace sm {

// pos: quarter notes from the bar line. Compound meters (6/8, 9/8, 12/8)
// have dotted-quarter beats, subdivided in three.
inline float metricStrength(double pos, int num, int den) {
  if (num <= 0 || den <= 0) return -1.f;
  const double unit = 4.0 / den;
  const bool compound = den >= 8 && num % 3 == 0 && num > 3;
  const double beat = compound ? 3.0 * unit : unit;
  const double bar = num * unit;
  pos = std::fmod(pos, bar);
  if (pos < 0) pos += bar;
  // Played timing isn't quantised: within a tenth of a beat (at most a third
  // of the grid step), early or late.
  auto on = [&](double grid) {
    const double tol = std::min(0.1 * beat, 0.3 * grid);
    const double r = std::fmod(pos, grid);
    return r < tol || grid - r < tol;
  };
  if (on(bar)) return 1.f;
  const int beats = static_cast<int>(std::lround(bar / beat));
  if (beats >= 4 && beats % 2 == 0 && on(bar / 2)) return 0.7f;
  if (on(beat)) return 0.5f;
  if (on(beat / (compound ? 3.0 : 2.0))) return 0.25f;
  return 0.1f;
}

}  // namespace sm
