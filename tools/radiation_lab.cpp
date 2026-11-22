// radiation_lab: check the synthetic radiation against its target (no synthkit).
//   radiation_lab [sampleRate]
//   radiation_lab character [a0 nasal brilliance softness voice%]
//     body character: per 1/3-octave band, the change from the measured
//     response for each control at its extremes (or the given setting).
// Per 1/3-octave band: the target, the channels' mean power response (1/3-oct
// smoothed), the left/right difference, and how much the fine structure
// varies within the band (what vibrato sweeps through).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../model/Radiation.h"
#include "../model/data/ViolinRadiation.h"
using namespace sm;
// Mean power (dB) of the two channels over a 1/3-octave band centred at c.
static double bandDb(const Radiation& rad, float c) {
  double pw = 0;
  int n = 0;
  for (float f = c * std::pow(2.f, -1.f / 6); f < c * std::pow(2.f, 1.f / 6); f *= std::pow(2.f, 1.f / 96)) {
    pw += 0.5 * (std::pow(10.0, rad.responseDb(f, 0) / 10) + std::pow(10.0, rad.responseDb(f, 1) / 10));
    ++n;
  }
  return 10 * std::log10(pw / n);
}

static void settle(Radiation& rad, const RadiationSettings& s) {
  for (int i = 0; i < 400; ++i) rad.control(s);  // the glides
}

int character(int argc, char** argv) {
  const float fs = 48000.f;
  Radiation rad;
  rad.prepare(fs, data::kViolinRadiationHz, data::kViolinRadiationDb, data::kViolinRadiationCount, 0x5717a1u, 2600.f, 230.f);
  std::vector<float> bands;
  for (int b = -9; b <= 10; ++b) bands.push_back(1000.f * std::pow(2.f, b / 3.f));
  std::vector<double> base;
  for (float c : bands) base.push_back(bandDb(rad, c));
  std::vector<std::pair<const char*, RadiationSettings>> cases;
  if (argc >= 7) {
    RadiationSettings s;
    s.a0 = static_cast<float>(std::atof(argv[2]));
    s.nasal = static_cast<float>(std::atof(argv[3]));
    s.brilliance = static_cast<float>(std::atof(argv[4]));
    s.softness = static_cast<float>(std::atof(argv[5]));
    s.voice = 0.01f * static_cast<float>(std::atof(argv[6]));
    cases.push_back({"given", s});
  } else {
    RadiationSettings s;
    s.a0 = 9; cases.push_back({"a0+9", s}); s.a0 = 0;
    s.nasal = 9; cases.push_back({"nas+9", s}); s.nasal = 0;
    s.brilliance = 9; cases.push_back({"bril+9", s}); s.brilliance = 0;
    s.softness = 12; cases.push_back({"soft12", s}); s.softness = 0;
    s.voice = 0.1f; cases.push_back({"voice+10", s}); s.voice = -0.1f; cases.push_back({"voice-10", s});
  }
  std::printf("  band ");
  for (auto& c : cases) std::printf("%9s", c.first);
  std::printf("   (dB change from the measured response)\n");
  std::vector<std::vector<double>> d(cases.size());
  for (std::size_t i = 0; i < cases.size(); ++i) {
    settle(rad, cases[i].second);
    for (std::size_t b = 0; b < bands.size(); ++b) d[i].push_back(bandDb(rad, bands[b]) - base[b]);
    settle(rad, RadiationSettings{});
  }
  for (std::size_t b = 0; b < bands.size(); ++b) {
    std::printf("%6.0f ", bands[b]);
    for (std::size_t i = 0; i < cases.size(); ++i) std::printf("%+9.1f", d[i][b]);
    std::printf("\n");
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && !std::strcmp(argv[1], "character")) return character(argc, argv);
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
      lr += static_cast<double>(l - r) * (l - r);
      s1 += l; s2 += static_cast<double>(l) * l;
      ++n;
    }
    const double mean = s1 / n;
    std::printf("%6.0f  %+6.1f  %+6.1f   %5.1f dB   %5.1f dB\n", c, 10 * std::log10(tw / n), 10 * std::log10(pw / n),
                std::sqrt(lr / n), std::sqrt(std::max(0.0, s2 / n - mean * mean)));
  }
}
