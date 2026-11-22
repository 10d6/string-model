// string_lab: offline experiments on one bowed string (no synthkit).
//
//   string_lab tone   <string 0-3> <note> <vb m/s> <fb N> <beta> [classic] [out.wav]
//   string_lab sweep  <string 0-3> <note> <vb m/s> [classic]      Schelleng map (beta x force)
//
// For each run it reports the measured pitch (cents from the note), slips per
// nominal period (1 = Helmholtz motion), the slip fraction (Helmholtz: ~beta)
// and the contact temperature.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../model/BowedString.h"
#include "../model/defs/Violin.h"

using namespace sm;

namespace {

float kFs = std::getenv("LAB_FS") ? static_cast<float>(std::atof(std::getenv("LAB_FS"))) : 96000.f;

struct Stats {
  float cents = 0, slipsPerPeriod = 0, slipFraction = 0, tempMean = 0, tempMin = 0, tempMax = 0, rms = 0;
};

// LAB_HFLOOR, LAB_HSPEED, LAB_TDROPE: the enhanced model's h(u) and Y(T).
void tuneFriction(BowedString& str) {
  if (const char* e = std::getenv("LAB_WIDTH")) str.setBowWidth(std::atoi(e), std::getenv("LAB_SHARE") ? static_cast<float>(std::atof(std::getenv("LAB_SHARE"))) : 0.8f);  // contact points, samples apart; force share of the first
  if (const char* e = std::getenv("LAB_HFLOOR")) str.thermal.hFloor = static_cast<float>(std::atof(e));
  if (const char* e = std::getenv("LAB_HSPEED")) str.thermal.hSpeed = static_cast<float>(std::atof(e));
  if (const char* e = std::getenv("LAB_TDROPE")) str.thermal.tDropEnhanced = static_cast<float>(std::atof(e));
}

float midiHz(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }

Stats run(int s, float note, float vb, float fb, float beta, FrictionModel model, std::vector<float>* out, float seconds = 1.5f) {
  BowedString str;
  str.prepare(kFs, 60.f);
  const InstrumentDef& inst = violin();
  str.setDef(inst.strings[s], inst.loss);
  tuneFriction(str);
  if (std::getenv("LAB_HEAT")) str.thermal.heatScale = static_cast<float>(std::atof(std::getenv("LAB_HEAT")));
  if (std::getenv("LAB_TDROP")) str.thermal.tDrop = static_cast<float>(std::atof(std::getenv("LAB_TDROP")));
  const float hz = midiHz(note);
  const bool stopped = static_cast<int>(note) != inst.strings[s].openNote;
  str.setGeometry(hz, hz, beta, stopped);
  const int n = static_cast<int>(seconds * kFs);
  const int a0 = static_cast<int>(0.75f * kFs);  // analyse the second half
  std::vector<float> f(static_cast<std::size_t>(n));
  int slips = 0, slipSamples = 0;
  bool prev = false;
  double tsum = 0, tmin = 1e9, tmax = 0, e = 0;
  unsigned seed = 1;
  for (int i = 0; i < n; ++i) {
    // Bow: 30 ms ramp of speed and force.
    const float ramp = std::min(1.f, i / (0.03f * kFs));
    seed = seed * 1664525u + 1013904223u;
    const float noise = (seed >> 8) * (1.f / 16777216.f);
    f[static_cast<std::size_t>(i)] = str.process(vb * ramp, fb * ramp, model, 0.f, noise);
    if (i >= a0) {
      const bool sl = str.slipping();
      if (sl && !prev) ++slips;
      slipSamples += sl;
      prev = sl;
      const float t = str.temperature();
      tsum += t;
      tmin = std::min<double>(tmin, t);
      tmax = std::max<double>(tmax, t);
      e += static_cast<double>(f[static_cast<std::size_t>(i)]) * f[static_cast<std::size_t>(i)];
    }
  }
  Stats st;
  const int m = n - a0;
  st.slipsPerPeriod = slips / (m / kFs * hz);
  st.slipFraction = static_cast<float>(slipSamples) / m;
  st.tempMean = static_cast<float>(tsum / m);
  st.tempMin = static_cast<float>(tmin);
  st.tempMax = static_cast<float>(tmax);
  st.rms = static_cast<float>(std::sqrt(e / m));
  // Pitch by autocorrelation around the nominal period (±8%).
  const float P = kFs / hz;
  int lo = static_cast<int>(P * 0.92f), hi = static_cast<int>(P * 1.08f) + 1;
  double best = -1e30;
  int bl = lo;
  std::vector<double> ac(static_cast<std::size_t>(hi + 2), 0.0);
  for (int l = lo - 1; l <= hi + 1; ++l) {
    double c = 0;
    for (int i = a0; i + l < n; ++i) c += static_cast<double>(f[static_cast<std::size_t>(i)]) * f[static_cast<std::size_t>(i + l)];
    ac[static_cast<std::size_t>(l)] = c;
  }
  for (int l = lo; l <= hi; ++l)
    if (ac[static_cast<std::size_t>(l)] > best) best = ac[static_cast<std::size_t>(l)], bl = l;
  const double y0 = ac[static_cast<std::size_t>(bl - 1)], y1 = ac[static_cast<std::size_t>(bl)], y2 = ac[static_cast<std::size_t>(bl + 1)];
  const double den = y0 - 2 * y1 + y2;
  const double lag = bl + (den != 0 ? 0.5 * (y0 - y2) / den : 0);
  st.cents = static_cast<float>(1200.0 * std::log2(P / lag));
  if (out) *out = f;
  return st;
}

void writeWav(const char* path, const std::vector<float>& x, float fs) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return;
  float peak = 1e-9f;
  for (float v : x) peak = std::max(peak, std::fabs(v));
  const unsigned n = static_cast<unsigned>(x.size()), sr = static_cast<unsigned>(fs), bytes = n * 2;
  auto w32 = [&](unsigned v) { std::fwrite(&v, 4, 1, fp); };
  auto w16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, fp); };
  std::fwrite("RIFF", 1, 4, fp); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, fp);
  w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
  std::fwrite("data", 1, 4, fp); w32(bytes);
  for (float v : x) { short s = static_cast<short>(std::lrint(v / peak * 0.8f * 32767)); std::fwrite(&s, 2, 1, fp); }
  std::fclose(fp);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: string_lab tone <s> <note> <vb> <fb> <beta> [classic] [out.wav]\n"
                         "       string_lab sweep <s> <note> <vb> [classic]\n");
    return 1;
  }
  const int s = std::atoi(argv[2]);
  const float note = static_cast<float>(std::atof(argv[3]));
  const float vb = static_cast<float>(std::atof(argv[4]));
  auto hasArg = [&](const char* a) { for (int i = 5; i < argc; ++i) if (!std::strcmp(argv[i], a)) return true; return false; };
  const FrictionModel model = hasArg("classic") ? FrictionModel::Classic : (hasArg("enhanced") ? FrictionModel::Enhanced : FrictionModel::Thermal);
  if (!std::strcmp(argv[1], "guettler")) {
    // Attacks from rest at constant bow acceleration (Guettler's plane, KB
    // §7.2): rows acceleration, columns force, at beta = argv[4]. Cell: ms of
    // pre-Helmholtz motion after the first slip ("P" = perfect: Helmholtz
    // from the first slip), "-" = no Helmholtz within 0.4 s.
    const float beta = vb;
    const float accs[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
    const float forces[] = {0.05f, 0.1f, 0.2f, 0.4f, 0.8f, 1.6f, 3.2f};
    const float hz = midiHz(note), P = kFs / hz;
    std::printf("acc \\ F  ");
    for (float f : forces) std::printf("%6.2f", f);
    std::printf("\n");
    int perfect = 0, good = 0, cells = 0;
    for (float ac : accs) {
      std::printf("%7.2f  ", ac);
      for (float fb : forces) {
        BowedString str;
        str.prepare(kFs, 60.f);
        const InstrumentDef& inst = violin();
        str.setDef(inst.strings[s], inst.loss);
        tuneFriction(str);
        const bool stopped = static_cast<int>(note) != inst.strings[s].openNote;
        str.setGeometry(hz, hz, beta, stopped);
        const int n = static_cast<int>(0.4f * kFs);
        int first = -1, last = -1, run = 0, settled = -1;
        bool prev = false;
        unsigned seed = 1;
        for (int i = 0; i < n && settled < 0; ++i) {
          const float v = std::min(ac * i / kFs, 0.5f);
          seed = seed * 1664525u + 1013904223u;
          str.process(v, fb, model, 0.f, (seed >> 8) * (1.f / 16777216.f));
          const bool sl = str.slipping();
          if (sl && !prev) {
            if (first < 0) first = i;
            if (last >= 0 && std::fabs((i - last) - P) < 0.05f * P) ++run;
            else run = 0;
            if (run >= 3) settled = i - 4 * static_cast<int>(P + 0.5f);
            last = i;
          }
          prev = sl;
        }
        ++cells;
        if (settled < 0 || first < 0) { std::printf("     -"); continue; }
        const float ms = std::max(0.f, (settled - first) / kFs * 1000.f);
        if (settled - first <= static_cast<int>(1.5f * P)) { ++perfect; ++good; std::printf("     P"); }
        else { if (ms <= 50.f) ++good; std::printf("%6.0f", ms); }
      }
      std::printf("\n");
    }
    std::printf("perfect %d, within 50 ms %d, of %d\n", perfect, good, cells);
    return 0;
  }
  if (!std::strcmp(argv[1], "tone")) {
    const float fb = static_cast<float>(std::atof(argv[5])), beta = static_cast<float>(std::atof(argv[6]));
    std::vector<float> out;
    Stats st = run(s, note, vb, fb, beta, model, &out);
    std::printf("cents %+.1f  slips/period %.2f  slip fraction %.3f (beta %.3f)  T %.1f [%.1f..%.1f] K  rms %.3f N\n",
                st.cents, st.slipsPerPeriod, st.slipFraction, beta, st.tempMean, st.tempMin, st.tempMax, st.rms);
    for (int i = 7; i < argc; ++i)
      if (std::strstr(argv[i], ".wav")) writeWav(argv[i], out, kFs);
    return 0;
  }
  // Sweep: rows beta, columns force. Cell: H = Helmholtz (1 slip/period within
  // 3%, pitch within 15 cents), m = multiple slip, r = raucous/other, . = no tone.
  const float betas[] = {0.02f, 0.03f, 0.045f, 0.065f, 0.09f, 0.13f, 0.18f};
  const float forces[] = {0.02f, 0.04f, 0.08f, 0.15f, 0.3f, 0.6f, 1.2f, 2.4f, 4.8f};
  std::printf("beta \\ F  ");
  for (float f : forces) std::printf("%6.2f", f);
  std::printf("\n");
  for (float b : betas) {
    std::printf("%8.3f  ", b);
    for (float f : forces) {
      Stats st = run(s, note, vb, f, b, model, nullptr, 1.0f);
      char c = '.';
      if (st.rms > 1e-3f) {
        if (std::fabs(st.slipsPerPeriod - 1.f) < 0.03f && std::fabs(st.cents) < 15.f) c = 'H';
        else if (st.slipsPerPeriod > 1.5f) c = 'm';
        else c = 'r';
      }
      std::printf("   %c%+3.0f", c, std::fmin(99.f, std::fmax(-99.f, st.cents)));
    }
    std::printf("\n");
  }
  return 0;
}
