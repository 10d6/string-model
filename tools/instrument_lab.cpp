// instrument_lab: drive sm::Instrument directly (no synthkit) and print what
// the bow does. Used to debug the gesture layer.
//   instrument_lab detache [legato 0/1]        nine short notes, level per note
//   instrument_lab hold <note> <seconds> [vel] [out.wav]
//                                              one held note: level every 50 ms,
//                                              the regime, deepest dips
//   instrument_lab chord <note> <note> ...     notes held together (one voice
//                                              each): force and slip rate per voice
//   instrument_lab slurs [runs]                same-string slurs as played: periods
//                                              until Helmholtz motion after the
//                                              change (LAB_SLIPS=1: slips per
//                                              period once the finger is down)
//   instrument_lab attacks [runs]              attacks from rest as played: per
//                                              string, note and dynamic, periods
//                                              from the first slip to Helmholtz
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "LabInstrument.h"
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
  // LAB_Z0: scale every string's impedance (a lower-tension set; the wolf test).
  static InstrumentDef def = labInstrument();
  if (const char* e = std::getenv("LAB_Z0"))
    for (auto& st : def.strings) {
      st.z0 *= static_cast<float>(std::atof(e));
      st.zr *= static_cast<float>(std::atof(e));
    }
  Instrument inst; inst.prepare(sr, def);
  PerformerControls pc; InstrumentControls ic;
  const char* mode = argc > 1 ? argv[1] : "detache";
  if (const char* e = std::getenv("LAB_FRICTION")) ic.friction = static_cast<FrictionModel>(std::atoi(e));  // 0 thermal, 1 classic, 2 enhanced
  if (const char* e = std::getenv("LAB_BOWWIDTH")) ic.bowWidth = static_cast<float>(std::atof(e));
  if (std::getenv("LAB_COUPLING")) ic.coupling = static_cast<float>(std::atof(std::getenv("LAB_COUPLING")));
  pc.avoidOpen = std::getenv("LAB_AVOIDOPEN") && std::atoi(std::getenv("LAB_AVOIDOPEN")) != 0;  // the labs play the string asked for
  if (std::getenv("LAB_STRING")) pc.forcedString = std::atoi(std::getenv("LAB_STRING"));
  if (std::getenv("LAB_MUTE")) ic.mute = static_cast<float>(std::atof(std::getenv("LAB_MUTE")));
  if (std::getenv("LAB_FLOOR")) pc.changeFloor = static_cast<float>(std::atof(std::getenv("LAB_FLOOR")));
  if (std::getenv("LAB_MINACC")) pc.minAccel = static_cast<float>(std::atof(std::getenv("LAB_MINACC")));
  if (std::getenv("LAB_PRESSURE")) pc.pressure = static_cast<float>(std::atof(std::getenv("LAB_PRESSURE")));
  if (std::getenv("LAB_DYN")) pc.dynamics = static_cast<float>(std::atof(std::getenv("LAB_DYN")));
  if (std::getenv("LAB_CONTACT")) pc.contact = static_cast<float>(std::atof(std::getenv("LAB_CONTACT")));
  if (std::getenv("LAB_BETA")) inst.performer().lab().beta = static_cast<float>(std::atof(std::getenv("LAB_BETA")));
  if (std::getenv("LAB_VB")) inst.performer().lab().vb = static_cast<float>(std::atof(std::getenv("LAB_VB")));
  if (std::getenv("LAB_FORCE")) inst.performer().lab().force = static_cast<float>(std::atof(std::getenv("LAB_FORCE")));
  if (std::getenv("LAB_ALIFT")) inst.performer().lab().attackLift = static_cast<float>(std::atof(std::getenv("LAB_ALIFT")));
  if (std::getenv("LAB_ATAU")) inst.performer().lab().attackTau = static_cast<float>(std::atof(std::getenv("LAB_ATAU")));
  if (std::getenv("LAB_AVARYN")) inst.performer().lab().attackVary = static_cast<float>(std::atof(std::getenv("LAB_AVARYN")));
  if (std::getenv("LAB_PIN_AFTER")) inst.performer().lab().pinAfter = static_cast<float>(std::atof(std::getenv("LAB_PIN_AFTER")));
  if (std::getenv("LAB_A4")) pc.a4 = static_cast<float>(std::atof(std::getenv("LAB_A4")));
  if (std::getenv("LAB_VIB")) pc.vibDepth = static_cast<float>(std::atof(std::getenv("LAB_VIB")));
  if (std::getenv("LAB_WOLF")) ic.wolf = static_cast<float>(std::atof(std::getenv("LAB_WOLF")));
  if (std::getenv("LAB_WIDTH")) pc.changeWidth = static_cast<float>(std::atof(std::getenv("LAB_WIDTH")));
  if (!std::strcmp(mode, "release")) {
    // instrument_lab release <note> <mode 1 lift, 2 stop, 3 finger off> [relVel]:
    // hold 1 s, let go; how fast the level falls, and the pitch that rings on.
    const int note = argc > 2 ? std::atoi(argv[2]) : 71;
    pc.releaseMode = argc > 3 ? std::atoi(argv[3]) : 1;
    const float relVel = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.5f;
    inst.performer().noteOn(note, 0.7f, pc);
    std::vector<float> out;
    for (int i = 0; i < static_cast<int>(sr * 2.5f); ++i) {
      if (i == static_cast<int>(sr * 1.0f)) inst.performer().noteOff(note, relVel, pc);
      out.push_back(inst.process(pc, ic));
    }
    auto rms = [&](float t, float len) {
      double e = 0; const int a = static_cast<int>(t * sr), n = static_cast<int>(len * sr);
      for (int i = a; i < a + n; ++i) e += static_cast<double>(out[static_cast<std::size_t>(i)]) * out[static_cast<std::size_t>(i)];
      return 10.0 * std::log10(e / n + 1e-30);
    };
    const double ref = rms(0.9f, 0.1f);
    double t20 = -1, t40 = -1;
    for (float t = 1.0f; t < 2.49f; t += 0.005f) {
      const double l = rms(t, 0.005f) - ref;
      if (t20 < 0 && l < -20) t20 = t - 1.0;
      if (t40 < 0 && l < -40) { t40 = t - 1.0; break; }
    }
    // Pitch 0.15-0.45 s after release (autocorrelation over 60-2000 Hz).
    const int a = static_cast<int>(1.15f * sr), n = static_cast<int>(0.3f * sr);
    int best = 0; double bestC = -1e30;
    for (int lag = static_cast<int>(sr / 2000); lag < static_cast<int>(sr / 60); ++lag) {
      double c = 0;
      for (int i = a; i < a + n; ++i) c += static_cast<double>(out[static_cast<std::size_t>(i)]) * out[static_cast<std::size_t>(i + lag)];
      if (c > bestC) { bestC = c; best = lag; }
    }
    const double hz = sr / best, midi = 69 + 12 * std::log2(hz / 440);
    std::printf("note %d, release %d: -20 dB after %s, -40 dB after %s; rings at %.1f Hz (MIDI %.1f)\n", note, pc.releaseMode,
                t20 < 0 ? "> 1.5 s" : (std::to_string(static_cast<int>(t20 * 1000)) + " ms").c_str(),
                t40 < 0 ? "> 1.5 s" : (std::to_string(static_cast<int>(t40 * 1000)) + " ms").c_str(), hz, midi);
    return 0;
  }
  if (!std::strcmp(mode, "bridge")) {
    // |Y| of the bridge at a few frequencies (s/kg).
    std::printf("%d modes\n", inst.bridge().modeCount());
    if (inst.def().strings[0].openNote < 48)  // the cello's range too, and Z0 |Y| per string
      for (float f : {65.4f, 82.4f, 98.f, 110.f, 131.f, 147.f, 165.f, 175.f, 185.f, 220.f, 300.f})
        std::printf("  %6.1f Hz  |Y| C..A %.3f %.3f %.3f %.3f s/kg  Z0|Y| %.3f %.3f %.3f %.3f\n", f, inst.bridge().magnitude(f, 0),
                    inst.bridge().magnitude(f, 1), inst.bridge().magnitude(f, 2), inst.bridge().magnitude(f, 3),
                    inst.def().strings[0].z0 * inst.bridge().magnitude(f, 0), inst.def().strings[1].z0 * inst.bridge().magnitude(f, 1),
                    inst.def().strings[2].z0 * inst.bridge().magnitude(f, 2), inst.def().strings[3].z0 * inst.bridge().magnitude(f, 3));
    for (float f : {196.f, 280.f, 400.f, 460.f, 550.f, 700.f, 1000.f, 1500.f, 2000.f, 2600.f, 3500.f, 5000.f, 8000.f})
      std::printf("  %6.0f Hz  |Y| %.3f s/kg\n", f, inst.bridge().magnitude(f));
    return 0;
  }
  if (!std::strcmp(mode, "chord")) {
    // Notes held together, pressed 5 ms apart, held 1.5 s: each a voice of
    // its own on the shared bridge. Per voice between 1.0 and 1.5 s: force,
    // slips per period (1 = Helmholtz motion), pitch from slip intervals.
    constexpr int kV = Instrument::kVoices;
    int notes[8], n = 0;
    for (int i = 2; i < argc && n < 8; ++i) notes[n++] = std::atoi(argv[i]);
    int slips[kV] = {}, since[kV] = {}, gaps[kV] = {}, stuck[kV] = {}, samples = 0;
    double gapSum[kV] = {}, gapSq[kV] = {};
    bool prev[kV] = {};
    Performer& pf = inst.performer();
    for (int i = 0; i < static_cast<int>(sr * 1.5f); ++i) {
      for (int k = 0; k < n; ++k)
        if (i == static_cast<int>(0.005f * sr * k)) pf.noteOn(notes[k], 0.7f, pc);
      inst.process(pc, ic);
      if (i < static_cast<int>(sr * 1.0f)) continue;
      ++samples;
      for (int v = 0; v < kV; ++v) {
        if (!pf.loaded(v)) continue;
        const bool sl = inst.voiceString(v).slipping();
        ++since[v];
        if (!sl) ++stuck[v];
        if (sl && !prev[v]) {
          ++slips[v];
          if (since[v] < static_cast<int>(sr)) { gapSum[v] += since[v]; gapSq[v] += double(since[v]) * since[v]; ++gaps[v]; }
          since[v] = 0;
        }
        prev[v] = sl;
      }
    }
    std::printf("%d notes, %d bowed:\n", n, pf.bowingCount());
    for (int v = 0; v < kV; ++v) {
      if (!pf.loaded(v) || !pf.voice(v).bowing()) continue;
      const Player& p = pf.voice(v);
      const double hz = gaps[v] ? sr / (gapSum[v] / gaps[v]) : 0;
      // The regime from the stick test (KB §7.3, after Beigi & Conneely):
      // stick fraction (ideal Helmholtz ~ 1 - beta), slips per period of the
      // note's frequency (the finger's place: lab notes are in tune), the
      // slip intervals' jitter (sd / mean).
      const double eta = double(stuck[v]) / samples;
      const double f0 = 440.0 * std::pow(2.0, (p.fingerNote() - 69) / 12.0);
      const double perPeriod = slips[v] / (0.5 * f0);
      const double mean = gaps[v] ? gapSum[v] / gaps[v] : 0;
      const double jitter = gaps[v] > 1 ? std::sqrt(std::max(0.0, gapSq[v] / gaps[v] - mean * mean)) / mean : 1.0;
      const char* regime = eta > 0.98 ? "stick" : eta < 0.05 ? "slip" : perPeriod > 1.5 ? "multiple" : perPeriod < 0.6 ? "subharmonic"
                           : (jitter > 0.25 || eta < 0.35) ? "raucous" : "helmholtz";
      std::printf("  note %d on %s: force %.2f N, %d slips in 0.5 s, slip rate %.1f Hz, stick %.3f (1-beta %.3f), %.2f slips/period, jitter %.3f: %s\n",
                  p.note(), inst.def().strings[p.string()].name, p.force(), slips[v], hz, eta, 1.0 - p.beta(), perPeriod, jitter, regime);
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
        inst.performer().noteOff(note, 0.5f, pc);
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
  if (!std::strcmp(mode, "slurs")) {
    // Same-string slurs as played: each string, a held note then a slur up
    // or down 1, 2 or 4 semitones (a finger change), three velocities.
    // Cell: periods after the change until Helmholtz motion again (three
    // slips in a row a period apart, within 5%); "P" = within 1.5.
    const int runs = argc > 2 ? std::atoi(argv[2]) : 3;
    const int steps[] = {1, 2, 4, -1, -2};
    const float vels[] = {0.35f, 0.6f, 0.85f};
    std::vector<float> all;
    int clean = 0, cells = 0;
    std::printf("string    ");
    for (int st : steps) for (float v : vels) std::printf(" %+d/%.2f", st, v);
    std::printf("\n");
    for (int s = 0; s < 4; ++s) {
      std::printf("%-8s  ", def.strings[s].name);
      for (int st : steps) {
        for (float v : vels) {
          std::vector<float> per;
          for (int r = 0; r < runs; ++r) {
            Instrument in;
            in.prepare(sr, def);
            PerformerControls p = pc;
            p.forcedString = s;
            p.legato = true;
            const int a = def.strings[s].openNote + 4 + (st < 0 ? 3 : 0) + r, b = a + st;
            in.performer().noteOn(a, v, p);
            for (int i = 0; i < static_cast<int>(0.6f * sr); ++i) in.process(p, ic);
            in.performer().noteOn(b, v, p);
            in.performer().noteOff(a, 0.5f, p);
            const float P = sr / (440.f * std::pow(2.f, (b - 69) / 12.f));
            int last = -1, run = 0, settled = -1;
            bool prev = false;
            for (int i = 0; i < static_cast<int>(0.4f * sr) && settled < 0; ++i) {
              in.process(p, ic);
              const bool sl = in.string(s).slipping();
              if (sl && !prev) {
                if (last >= 0 && std::fabs((i - last) - P) < 0.05f * P) ++run;
                else run = 0;
                if (run >= 3) settled = i - 3 * static_cast<int>(P + 0.5f);
                last = i;
              }
              prev = sl;
            }
            // (with LAB_SLIPS: slips per period over the 20 periods after the
            // finger has arrived, 60 ms on)
            if (std::getenv("LAB_SLIPS")) {
              int slips = 0;
              bool pv = false;
              const int from = static_cast<int>(0.06f * sr);
              Instrument in2;
              in2.prepare(sr, def);
              in2.performer().noteOn(a, v, p);
              for (int i = 0; i < static_cast<int>(0.6f * sr); ++i) in2.process(p, ic);
              in2.performer().noteOn(b, v, p);
              in2.performer().noteOff(a, 0.5f, p);
              for (int i = 0; i < from + static_cast<int>(20 * P); ++i) {
                in2.process(p, ic);
                const bool sl = in2.string(s).slipping();
                if (i >= from && sl && !pv) ++slips;
                pv = sl;
              }
              per.push_back(slips / 20.f);
              continue;
            }
            per.push_back(settled < 0 ? 1e9f : std::max(0.f, settled / P));
          }
          std::sort(per.begin(), per.end());
          for (float x : per) ++cells, clean += x <= 1.5f, all.push_back(x);
          const float m = per[per.size() / 2];
          if (std::getenv("LAB_SLIPS")) std::printf(" %6.2f", m);
          else if (m > 1e8f) std::printf("      -");
          else if (m <= 1.5f) std::printf("      P");
          else std::printf(" %6.0f", m);
        }
      }
      std::printf("\n");
    }
    std::sort(all.begin(), all.end());
    if (std::getenv("LAB_SLIPS")) {
      int ok = 0;
      for (float x : all) ok += x < 1.1f;
      std::printf("slips per period after arrival: median %.2f, 90%% %.2f; clean (< 1.1) %d of %d\n", all[all.size() / 2], all[all.size() * 9 / 10], ok, static_cast<int>(all.size()));
      return 0;
    }
    std::printf("clean (within 1.5 periods) %d of %d; median %.1f periods; 90%%: %.0f\n", clean, cells, all[all.size() / 2], all[all.size() * 9 / 10]);
    return 0;
  }
  if (!std::strcmp(mode, "hand")) {
    // The left hand on one string: a scale up and down (major, slurred
    // quavers at 0.25 s), the hand's place per note and the shifts.
    const int s = argc > 2 ? std::atoi(argv[2]) : 3;
    const int top = argc > 3 ? std::atoi(argv[3]) : 24;
    PerformerControls p = pc;
    p.forcedString = s;
    p.legato = true;
    const int steps[] = {2, 2, 1, 2, 2, 2, 1};
    std::vector<int> ks{0};
    for (int i = 0; ks.back() < top; ++i) ks.push_back(ks.back() + steps[i % 7]);
    for (int i = static_cast<int>(ks.size()) - 2; i >= 0; --i) ks.push_back(ks[static_cast<std::size_t>(i)]);
    int shifts = 0, prevNote = -1;
    float prevHand = inst.performer().handPlace();
    for (int k : ks) {
      const int note = def.strings[s].openNote + k;
      if (prevNote >= 0) inst.performer().noteOff(prevNote, 0.5f, p);
      inst.performer().noteOn(note, 0.6f, p);
      for (int i = 0; i < static_cast<int>(0.25f * sr); ++i) inst.process(p, ic);
      const float h = inst.performer().handPlace();
      if (h != prevHand) ++shifts;
      std::printf("%+3d:%-4.0f", k, h);
      prevHand = h;
      prevNote = note;
    }
    std::printf("\n%d notes, %d shifts\n", static_cast<int>(ks.size()), shifts);
    return 0;
  }
  if (!std::strcmp(mode, "attacks")) {
    // The performer's own attacks (not a fixed force and acceleration, as
    // string_lab guettler): each string open and 2, 4, 7 semitones up, at
    // three velocities, runs times each (the performer's per-note variety).
    // Cell: periods from the first slip until Helmholtz motion (three slips
    // in a row a period apart, within 5%), median over the runs; "P" =
    // perfect (within 1.5 periods), "-" = none within 0.5 s.
    const int runs = argc > 2 ? std::atoi(argv[2]) : 3;
    const float vels[] = {0.3f, 0.6f, 0.9f};
    const int ups[] = {0, 2, 4, 7};
    int perfect = 0, within10 = 0, cells = 0;
    std::vector<float> all, firstMs;
    std::printf("string    ");
    for (int u : ups) for (float v : vels) std::printf(" +%d/%.1f", u, v);
    std::printf("\n");
    for (int s = 0; s < 4; ++s) {
      std::printf("%-8s  ", def.strings[s].name);
      for (int u : ups) {
        for (float v : vels) {
          std::vector<float> per;
          for (int r = 0; r < runs; ++r) {
            Instrument in;
            in.prepare(sr, def);
            in.performer().lab().attackLift = inst.performer().lab().attackLift;
            in.performer().lab().attackTau = inst.performer().lab().attackTau;
            in.performer().lab().attackVary = inst.performer().lab().attackVary;
            PerformerControls p = pc;
            p.forcedString = s;
            for (int k = 0; k < r; ++k) in.performer().noteOn(def.strings[s].openNote, 0.5f, p), in.performer().noteOff(def.strings[s].openNote, 0.5f, p);
            const int note = def.strings[s].openNote + u;
            const float P = sr / (440.f * std::pow(2.f, (note - 69) / 12.f));
            in.performer().noteOn(note, v + 0.02f * r, p);
            int first = -1, last = -1, run = 0, settled = -1;
            bool prev = false;
            for (int i = 0; i < static_cast<int>(0.5f * sr) && settled < 0; ++i) {
              in.process(p, ic);
              const bool sl = in.string(s).slipping();
              if (sl && !prev) {
                if (first < 0) first = i;
                if (last >= 0 && std::fabs((i - last) - P) < 0.05f * P) ++run;
                else run = 0;
                if (run >= 3) settled = i - 3 * static_cast<int>(P + 0.5f);
                last = i;
              }
              prev = sl;
            }
            per.push_back(settled < 0 || first < 0 ? 1e9f : std::max(0.f, (settled - first) / P));
            if (first >= 0) firstMs.push_back(first / sr * 1000.f);
          }
          std::sort(per.begin(), per.end());
          const float m = per[per.size() / 2];
          for (float x : per) {
            ++cells;
            perfect += x <= 1.5f;
            within10 += x <= 10.f;
            all.push_back(x);
          }
          if (m > 1e8f) std::printf("      -");
          else if (m <= 1.5f) std::printf("      P");
          else std::printf(" %6.0f", m);
        }
      }
      std::printf("\n");
    }
    std::sort(all.begin(), all.end());
    std::sort(firstMs.begin(), firstMs.end());
    std::printf("perfect %d, within 10 periods %d, of %d; median %.1f periods; first slip %.0f ms after the key (median; 90%%: %.0f)\n", perfect,
                within10, cells, all[all.size() / 2], firstMs[firstMs.size() / 2], firstMs[firstMs.size() * 9 / 10]);
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
    long hStuck = 0, hN = 0, hSlips = 0, hGaps = 0, hLast = -1; double hSum = 0, hSq = 0; bool hPrev = false;
    const bool hf = std::getenv("LAB_HF") != nullptr;  // measure above ~1 kHz (2nd-order high-pass)
    float h1x = 0, h1y = 0, h2x = 0, h2y = 0;
    const float ra = std::exp(-2.f * 3.14159265f * 1000.f / sr);
    for (int i = 0; i < n; ++i) {
      const float y = inst.process(pc, ic);
      out.push_back(y);
      if (i >= static_cast<int>(0.6f * sr)) {  // the stick test from 0.6 s (KB §7.3)
        const int cs = inst.performer().currentString();
        const bool sl = inst.string(cs).slipping();
        hStuck += !sl;
        ++hN;
        if (sl && !hPrev) {
          if (hLast >= 0) { const double g = i - hLast; hSum += g; hSq += g * g; ++hGaps; }
          hLast = i;
          ++hSlips;
        }
        hPrev = sl;
      }
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
    {
      const int cs = inst.performer().currentString();
      const double f0 = 440.0 * std::pow(2.0, (inst.performer().lead().fingerNote() - 69) / 12.0) * pc.a4 / 440.0;
      const double eta = hN ? double(hStuck) / hN : 0, per = hN ? hSlips / (hN / sr * f0) : 0;
      const double mean = hGaps ? hSum / hGaps : 0;
      const double jit = hGaps > 1 ? std::sqrt(std::max(0.0, hSq / hGaps - mean * mean)) / mean : 1.0;
      const char* regime = eta > 0.98 ? "stick" : eta < 0.05 ? "slip" : per > 1.5 ? "multiple" : per < 0.6 ? "subharmonic"
                           : (jit > 0.25 || eta < 0.35) ? "raucous" : "helmholtz";
      std::printf("from 0.6 s: stick %.3f, %.2f slips/period, jitter %.3f: %s\n", eta, per, jit, regime);
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
  pc.legato = argc > 2 && std::atoi(argv[2]) != 0;  // 0: polyphonic, 1: legato
  int notes[] = {69, 71, 73, 74, 76, 74, 73, 71, 69};
  const float dur = 0.22f, step = 0.25f;
  int ni = 0; bool on = false;
  double e = 0; int cnt = 0;
  for (int i = 0; i < static_cast<int>(sr * 3); ++i) {
    const float t = i / sr;
    const int idx = static_cast<int>(t / step);
    if (idx < 9 && !on && idx == ni) { inst.performer().noteOn(notes[idx], 0.7f, pc); on = true; }
    if (on && t >= ni * step + dur) {
      inst.performer().noteOff(notes[ni], 0.5f, pc); on = false;
      std::printf("note %d str %d  rms %.1f dB  vb %+.3f  beta %.3f  bowpos %.2f\n", notes[ni], inst.performer().currentString(),
                  10 * std::log10(e / cnt + 1e-20), inst.performer().bowVelocity(), inst.performer().beta(), inst.performer().bowPosition());
      e = 0; cnt = 0; ++ni;
    }
    const float y = inst.process(pc, ic);
    if (on && t > ni * step + 0.08f) { e += static_cast<double>(y) * y; ++cnt; }
  }
}
