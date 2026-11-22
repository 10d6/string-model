// radiation_lab: check the synthetic radiation against its target (no synthkit).
//   radiation_lab [sampleRate]
// Per 1/3-octave band: the target, the channels' mean power response (1/3-oct
// smoothed), the left/right difference, and how much the fine structure
// varies within the band (what vibrato sweeps through).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../model/Radiation.h"
#include "../model/data/ViolinRadiation.h"
using namespace sm;
int main(int argc, char** argv) {
  const float fs = argc > 1 ? static_cast<float>(std::atof(argv[1])) : 48000.f;
  Radiation rad;
  rad.prepare(fs, data::kViolinRadiationHz, data::kViolinRadiationDb, data::kViolinRadiationCount, 0x5717a1u, 2600.f, 230.f);
  std::printf("%d modes at %.0f Hz\n  band   target   model   L-R rms   fine-structure sd (L)\n", rad.modeCount(), fs);
  for (int b = -9; b <= 10; ++b) {
    const float c = 1000.f * std::pow(2.f, b / 3.f);
    if (c > 0.45f * fs) break;
    double pw = 0, tw = 0, lr = 0, s1 = 0, s2 = 0;
    int n = 0;
    for (float f = c * std::pow(2.f, -1.f / 6); f < c * std::pow(2.f, 1.f / 6); f *= std::pow(2.f, 1.f / 96)) {
      const float l = rad.responseDb(f, 0), r = rad.responseDb(f, 1);
      pw += 0.5 * (std::pow(10.0, l / 10) + std::pow(10.0, r / 10));
      tw += std::pow(10.0, rad.targetDb(f) / 10);
      lr += (l - r) * (l - r);
      s1 += l; s2 += l * l;
      ++n;
    }
    const double mean = s1 / n;
    std::printf("%6.0f  %+6.1f  %+6.1f   %5.1f dB   %5.1f dB\n", c, 10 * std::log10(tw / n), 10 * std::log10(pw / n),
                std::sqrt(lr / n), std::sqrt(std::max(0.0, s2 / n - mean * mean)));
  }
}
