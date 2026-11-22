// instrument_lab: drive sm::Instrument directly (no synthkit) and print what
// the bow does. Used to debug the gesture layer.
//   instrument_lab detache [legato 0/1]        nine short notes, level per note
//   instrument_lab hold <note> <seconds> [vel] [out.wav]
//                                              one held note: level every 50 ms,
//                                              bow changes marked, deepest dips
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../model/Instrument.h"
using namespace sm;

static void writeWav(const char* path, const std::vector<float>& x, float fs) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return;
  float peak = 1e-9f;
  for (float v : x) peak = std::fmax(peak, std::fabs(v));
  const unsigned n = static_cast<unsigned>(x.size()), sr = static_cast<unsigned>(fs), bytes = n * 2;
  auto w32 = [&](unsigned v) { std::fwrite(&v, 4, 1, fp); };
  auto w16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, fp); };
  std::fwrite("RIFF", 1, 4, fp); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, fp);
  w32(16); w16(1); w16(1); w32(sr); w32(sr * 2); w16(2); w16(16);
  std::fwrite("data", 1, 4, fp); w32(bytes);
  for (float v : x) { short s = static_cast<short>(std::lrint(v / peak * 0.8f * 32767)); std::fwrite(&s, 2, 1, fp); }
  std::fclose(fp);
}

int main(int argc, char** argv) {
  const float sr = 48000;
  Instrument inst; inst.prepare(sr);
  PerformerControls pc; InstrumentControls ic;
  const char* mode = argc > 1 ? argv[1] : "detache";
  if (std::getenv("LAB_COUPLING")) ic.coupling = static_cast<float>(std::atof(std::getenv("LAB_COUPLING")));
  if (std::getenv("LAB_MUTE")) ic.mute = static_cast<float>(std::atof(std::getenv("LAB_MUTE")));
  if (std::getenv("LAB_FLOOR")) pc.changeFloor = static_cast<float>(std::atof(std::getenv("LAB_FLOOR")));
  if (std::getenv("LAB_MINACC")) pc.minAccel = static_cast<float>(std::atof(std::getenv("LAB_MINACC")));
  if (std::getenv("LAB_PRESSURE")) pc.pressure = static_cast<float>(std::atof(std::getenv("LAB_PRESSURE")));
  if (std::getenv("LAB_DYN")) pc.dynamics = static_cast<float>(std::atof(std::getenv("LAB_DYN")));
  if (std::getenv("LAB_SUSTAIN")) pc.sustainSpeed = static_cast<float>(std::atof(std::getenv("LAB_SUSTAIN")));
  if (std::getenv("LAB_BMIN")) pc.sustainBetaMin = static_cast<float>(std::atof(std::getenv("LAB_BMIN")));
  if (std::getenv("LAB_WIDTH")) pc.changeWidth = static_cast<float>(std::atof(std::getenv("LAB_WIDTH")));
  if (std::getenv("LAB_CHANGE")) pc.bowChange = static_cast<float>(std::atof(std::getenv("LAB_CHANGE")));
  if (!std::strcmp(mode, "bridge")) {
    // |Y| of the bridge at a few frequencies (s/kg).
    std::printf("%d modes\n", inst.bridge().modeCount());
    for (float f : {196.f, 280.f, 400.f, 460.f, 550.f, 700.f, 1000.f, 1500.f, 2000.f, 2600.f, 3500.f, 5000.f, 8000.f})
      std::printf("  %6.0f Hz  |Y| %.3f s/kg\n", f, inst.bridge().magnitude(f));
    return 0;
  }
  if (!std::strcmp(mode, "chord")) {
    // Press the notes 5 ms apart, hold 2 s. Per string between 1.0 and 1.5 s:
    // bowed or not, force, slips per period (1 = Helmholtz motion), pitch
    // from slip intervals.
    int notes[4], n = 0;
    for (int i = 2; i < argc && n < 4; ++i) notes[n++] = std::atoi(argv[i]);
    int slips[4] = {}, since[4] = {}, gaps[4] = {};
    double gapSum[4] = {};
    bool prev[4] = {};
    for (int i = 0; i < static_cast<int>(sr * 1.5f); ++i) {
      for (int k = 0; k < n; ++k)
        if (i == static_cast<int>(0.005f * sr * k)) inst.performer().noteOn(notes[k], 0.7f, pc);
      inst.process(pc, ic);
      if (i < static_cast<int>(sr * 1.0f)) continue;
      for (int s = 0; s < 4; ++s) {
        const bool sl = inst.string(s).slipping();
        ++since[s];
        if (sl && !prev[s]) {
          ++slips[s];
          if (since[s] < static_cast<int>(sr)) { gapSum[s] += since[s]; ++gaps[s]; }
          since[s] = 0;
        }
        prev[s] = sl;
      }
    }
    std::printf("chord of %d, %d on the bow:\n", n, inst.performer().chordSize());
    for (int s = 0; s < 4; ++s) {
      if (!inst.performer().bowedString(s)) continue;
      const double hz = gaps[s] ? sr / (gapSum[s] / gaps[s]) : 0;
      std::printf("  %s: force %.2f N, %d slips in 0.5 s, slip rate %.1f Hz\n", inst.def().strings[s].name,
                  inst.performer().forceOn(s), slips[s], hz);
    }
    return 0;
  }
  if (!std::strcmp(mode, "sympathy")) {
    // Bow one note for 1.5 s, then let go: each string's level while bowing
    // and 0.5 s after release (dB re 1 m/s).
    const int note = argc > 2 ? std::atoi(argv[2]) : 67;
    inst.performer().noteOn(note, 0.7f, pc);
    auto db = [](float v) { return 20.f * std::log10(v + 1e-12f); };
    for (int i = 0; i < static_cast<int>(sr * 2.5f); ++i) {
      if (i == static_cast<int>(sr * 1.5f)) {
        std::printf("note %d on string %d, bowing:   ", note, inst.performer().currentString());
        for (int s = 0; s < 4; ++s) std::printf("%s %6.1f  ", inst.def().strings[s].name, db(inst.string(s).level()));
        std::printf("\n");
        inst.performer().noteOff(note, pc);
      }
      inst.process(pc, ic);
    }
    for (int t = 0; t < 8; ++t) {
      for (int i = 0; i < static_cast<int>(sr * 0.5f); ++i) inst.process(pc, ic);
      std::printf("   +%.1f s: ", 1.0 + 0.5 * (t + 1));
      for (int s = 0; s < 4; ++s) std::printf("%s %6.1f  ", inst.def().strings[s].name, db(inst.string(s).level()));
      std::printf("\n");
    }
    std::printf("note %d, 1 s after release: ", note);
    for (int s = 0; s < 4; ++s) std::printf("%s %6.1f  ", inst.def().strings[s].name, db(inst.string(s).level()));
    std::printf("\n");
    return 0;
  }
  if (!std::strcmp(mode, "hold")) {
    const int note = argc > 2 ? std::atoi(argv[2]) : 69;
    const float secs = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 12.f;
    const float vel = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.7f;
    std::vector<float> out;
    inst.performer().noteOn(note, vel, pc);
    const int n = static_cast<int>(secs * sr), win = static_cast<int>(0.005f * sr);
    double e = 0; float prevDir = 0; std::vector<float> lev; std::vector<int> changes;
    const bool hf = std::getenv("LAB_HF") != nullptr;  // measure above ~1 kHz (2nd-order high-pass)
    float h1x = 0, h1y = 0, h2x = 0, h2y = 0;
    const float ra = std::exp(-2.f * 3.14159265f * 1000.f / sr);
    for (int i = 0; i < n; ++i) {
      const float y = inst.process(pc, ic);
      out.push_back(y);
      float m = y;
      if (hf) {
        const float a = ra * (h1y + y - h1x); h1x = y; h1y = a;
        const float b = ra * (h2y + a - h2x); h2x = a; h2y = b;
        m = b;
      }
      e += static_cast<double>(m) * m;
      const float v = inst.performer().bowVelocity();
      const float dir = v > 0 ? 1.f : (v < 0 ? -1.f : 0.f);
      if (prevDir != 0 && dir != 0 && dir != prevDir) {
        std::printf("bow change at %.3f s (bow pos %.2f)\n", i / sr, inst.performer().bowPosition());
        changes.push_back(static_cast<int>(lev.size()));
      }
      if (dir != 0) prevDir = dir;
      if ((i + 1) % win == 0) {
        lev.push_back(static_cast<float>(10 * std::log10(e / win + 1e-20)));
        e = 0;
        static const char* tr = std::getenv("LAB_TRACE");  // "from,to" seconds
        if (tr) {
          float a = 0, b = 0;
          std::sscanf(tr, "%f,%f", &a, &b);
          const float t = (i + 1) / sr;
          if (t >= a && t <= b) {
            const int st = inst.performer().currentString();
            std::printf("    t %.3f  vb %+.3f  F %.3f  beta %.3f  T %5.1f K  level %6.1f dB\n", t, inst.performer().bowVelocity(),
                        inst.performer().forceOn(st), inst.performer().beta(), inst.string(st).temperature(), lev.back());
          }
        }
      }
    }
    // Report: steady level (median after 0.5 s) and dips below it.
    std::vector<float> s(lev.begin() + 100, lev.end());
    std::vector<float> sorted = s; std::sort(sorted.begin(), sorted.end());
    const float med = sorted[sorted.size() / 2];
    std::printf("steady level %.1f dB (5 ms RMS)\n", med);
    {
      int t20 = -1, t6 = -1;
      for (std::size_t k = 0; k < lev.size(); ++k) {
        if (t20 < 0 && lev[k] > med - 20) t20 = static_cast<int>(k) * 5;
        if (t6 < 0 && lev[k] > med - 6) t6 = static_cast<int>(k) * 5;
      }
      std::printf("attack: within 20 dB of steady at %d ms, within 6 dB at %d ms\n", t20, t6);
    }
    // Each bow change: the deepest point within 200 ms, and how long the level
    // stays more than 6 dB down (5 ms frames).
    for (int c : changes) {
      float lo = 1e9f; int d6 = 0;
      for (int k = std::max(0, c - 4); k < std::min<int>(static_cast<int>(lev.size()), c + 40); ++k) {
        lo = std::min(lo, lev[static_cast<std::size_t>(k)]);
        d6 += lev[static_cast<std::size_t>(k)] < med - 6 ? 5 : 0;
      }
      std::printf("  change at %.2f s: %.1f dB, >6 dB down %d ms\n", c * 0.005, med - lo, d6);
    }
    // Each dip: depth, and how long the level stays more than 6 / 3 dB down.
    for (std::size_t k = 100; k < lev.size(); ++k) {
      if (lev[k] >= med - 3) continue;
      std::size_t j = k; float lo = lev[k]; int d6 = 0, d3 = 0;
      while (j < lev.size() && lev[j] < med - 3) { lo = std::min(lo, lev[j]); d3 += 5; d6 += lev[j] < med - 6 ? 5 : 0; ++j; }
      std::printf("  dip at %.2f s: %.1f dB, >6 dB down %d ms, >3 dB down %d ms\n", k * 0.005, med - lo, d6, d3);
      k = j;
    }
    if (argc > 5) writeWav(argv[5], out, sr);
    return 0;
  }
  pc.legato = argc > 2 ? std::atoi(argv[2]) : 1;
  int notes[] = {69, 71, 73, 74, 76, 74, 73, 71, 69};
  const float dur = 0.22f, step = 0.25f;
  int ni = 0; bool on = false;
  double e = 0; int cnt = 0;
  for (int i = 0; i < static_cast<int>(sr * 3); ++i) {
    const float t = i / sr;
    const int idx = static_cast<int>(t / step);
    if (idx < 9 && !on && idx == ni) { inst.performer().noteOn(notes[idx], 0.7f, pc); on = true; }
    if (on && t >= ni * step + dur) {
      inst.performer().noteOff(notes[ni], pc); on = false;
      std::printf("note %d str %d  rms %.1f dB  vb %+.3f  beta %.3f  bowpos %.2f\n", notes[ni], inst.performer().currentString(),
                  10 * std::log10(e / cnt + 1e-20), inst.performer().bowVelocity(), inst.performer().beta(), inst.performer().bowPosition());
      e = 0; cnt = 0; ++ni;
    }
    const float y = inst.process(pc, ic);
    if (on && t > ni * step + 0.08f) { e += static_cast<double>(y) * y; ++cnt; }
  }
}
