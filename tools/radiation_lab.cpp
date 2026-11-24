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
#include "LabInstrument.h"
#include "../model/Radiation.h"
#include "../model/data/ViolinRadiation.h"
#include "../model/defs/Violin.h"
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
  prepareLabRadiation(rad, fs);
  std::vector<float> bands;
  for (int b = labInstrument().shape.lowestHz < 100.f ? -13 : -9; b <= 10; ++b) bands.push_back(1000.f * std::pow(2.f, b / 3.f));
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


// Directivity (KB §12.1): one omni (width 0) on a sphere around the violin.
// Per octave band: level toward front, front-up, sides, back, top and bottom
// re the mean over the sphere, the share of directions within 3 dB of the
// loudest, and the directivity index; then how much the directivity index
// varies from semitone to semitone (KB: 2.2-8.1 dB for the violin's class).
int directivity() {
  const float fs = 48000.f;
  Radiation rad;
  prepareLabRadiation(rad, fs);
  const int G = 200;
  std::vector<std::pair<float, float>> dirs;  // az, el (deg)
  for (int i = 0; i < G; ++i) {
    const float z = 1.f - (2.f * i + 1.f) / G, phi = 2.39996323f * i;
    dirs.push_back({std::atan2(std::sin(phi), std::cos(phi)) * 57.29578f, std::asin(z) * 57.29578f});
  }
  const std::pair<const char*, std::pair<float, float>> named[] = {
      {"front", {0, 0}}, {"fr-up", {0, 35}}, {"left", {90, 0}}, {"right", {-90, 0}}, {"back", {180, 0}}, {"top", {0, 90}}, {"bottom", {0, -60}},
      {"head", {-110, 44}}, {"torso", {-150, -44}}};
  const float octs[] = {250, 500, 1000, 2000, 4000, 8000};
  // Power per direction per 1/12-octave point from 177 Hz to 11.3 kHz.
  std::vector<float> fr;
  for (float f = 177.f; f < 11300.f; f *= std::pow(2.f, 1.f / 12.f)) fr.push_back(f);
  auto measure = [&](float az, float el, std::vector<double>& out) {
    RadiationSettings s;
    s.width = 0.f;
    s.micAz = az;
    s.micEl = el;
    rad.setNow(s);
    out.resize(fr.size());
    for (std::size_t j = 0; j < fr.size(); ++j) out[j] = std::pow(10.0, rad.responseDb(fr[j], 0) / 10);
  };
  std::vector<std::vector<double>> P(G);
  for (int i = 0; i < G; ++i) measure(dirs[i].first, dirs[i].second, P[i]);
  std::vector<std::vector<double>> N(9);
  for (int i = 0; i < 9; ++i) measure(named[i].second.first, named[i].second.second, N[i]);
  auto band = [&](const std::vector<double>& p, float c) {
    double s = 0;
    int n = 0;
    for (std::size_t j = 0; j < fr.size(); ++j)
      if (fr[j] >= c / std::sqrt(2.f) && fr[j] < c * std::sqrt(2.f)) s += p[j], ++n;
    return s / n;
  };
  std::printf("octave  front  fr-up   left  right   back    top bottom   head  torso   (dB re sphere mean)  within 3 dB of max   DI\n");
  for (float c : octs) {
    double mean = 0, mx = 0;
    std::vector<double> b(G);
    for (int i = 0; i < G; ++i) b[i] = band(P[i], c), mean += b[i] / G, mx = std::max(mx, b[i]);
    int within = 0;
    for (int i = 0; i < G; ++i) within += b[i] > mx / 2 ? 1 : 0;
    std::printf("%6.0f ", c);
    for (int i = 0; i < 9; ++i) std::printf(" %+6.1f", 10 * std::log10(band(N[i], c) / mean));
    std::printf("   %25.0f%%   %4.1f dB\n", 100.0 * within / G, 10 * std::log10(mx / mean));
  }
  double lo = 1e9, hi = -1e9;
  for (std::size_t j = 0; j < fr.size(); ++j) {
    if (fr[j] < 1000.f || fr[j] > 4000.f) continue;
    double mean = 0, mx = 0;
    for (int i = 0; i < G; ++i) mean += P[i][j] / G, mx = std::max(mx, P[i][j]);
    const double di = 10 * std::log10(mx / mean);
    lo = std::min(lo, di);
    hi = std::max(hi, di);
  }
  std::printf("semitone to semitone, 1-4 kHz: directivity index %.1f to %.1f dB (variation %.1f dB)\n", lo, hi, hi - lo);
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && !std::strcmp(argv[1], "character")) return character(argc, argv);
  if (argc > 1 && !std::strcmp(argv[1], "directivity")) return directivity();
  const float fs = argc > 1 ? static_cast<float>(std::atof(argv[1])) : 48000.f;
  Radiation rad;
  prepareLabRadiation(rad, fs);
  std::printf("%d modes at %.0f Hz\n  band   target   model   L-R rms   fine-structure sd (L)\n", rad.modeCount(), fs);
  for (int b = labInstrument().shape.lowestHz < 100.f ? -13 : -9; b <= 10; ++b) {
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
