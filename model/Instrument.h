// The whole instrument: the performer's voices, each a string of its own,
// and four undamped open strings that ring in sympathy, all on one bridge,
// run at an oversampled internal rate and decimated to the host rate. Output
// is the summed transverse bridge force (N) at the host rate; the body/output
// stage lives in the synth.
//
// A voice's string stands at one of the four strings' notches on the bridge
// (the string the performer chose for its note: its impedance, losses and
// coupling to the body's modes). While a voice is on a notch, that notch's
// open sympathetic string is damped and taken off (it is the same string on a
// real instrument); it comes back when the notch is free again.
//
// Also the player's ear: on stopped notes a slow loop compares the measured
// slip-to-slip period with the wanted period and moves the finger, as a
// player corrects intonation by ear (bowed strings go flat under force,
// KB §1.2). Open strings can't be corrected.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "BowedString.h"
#include "Bridge.h"
#include "Performer.h"
#include "defs/Instruments.h"

namespace sm {

// Band-pass biquad (RBJ, constant 0 dB peak) for the winding noise.
struct BandPass {
  float b0 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
  void setBandPass(float hz, float q, float fs) {
    const float w0 = 6.2831853f * hz / fs, al = std::sin(w0) / (2.f * q), a0 = 1.f + al;
    b0 = al / a0;
    b2 = -b0;
    a1 = -2.f * std::cos(w0) / a0;
    a2 = (1.f - al) / a0;
  }
  float process(float x) {
    const float y = b0 * x + s1;
    s1 = -a1 * y + s2;
    s2 = b2 * x - a2 * y;
    return y;
  }
};

// Linear-phase low-pass FIR decimator by an integer factor (1, 2 or 4).
class Decimator {
 public:
  void prepare(int factor) {
    factor_ = factor;
    if (factor_ <= 1) return;
    const int taps = 24 * factor_ + 1;
    h_.assign(static_cast<std::size_t>(taps), 0.f);
    const double fc = 0.45 / factor_;  // of the internal rate: just under the host Nyquist
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
      const double m = i - (taps - 1) / 2.0;
      const double sinc = m == 0 ? 2 * fc : std::sin(2 * 3.141592653589793 * fc * m) / (3.141592653589793 * m);
      // Blackman-Harris window
      const double w = 0.35875 - 0.48829 * std::cos(2 * 3.141592653589793 * i / (taps - 1)) +
                       0.14128 * std::cos(4 * 3.141592653589793 * i / (taps - 1)) -
                       0.01168 * std::cos(6 * 3.141592653589793 * i / (taps - 1));
      h_[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
      sum += sinc * w;
    }
    for (float& v : h_) v = static_cast<float>(v / sum);
    buf_.assign(static_cast<std::size_t>(2 * taps), 0.f);
    pos_ = 0;
  }
  int factor() const { return factor_; }
  // Push `factor` internal samples, get one host sample.
  float process(const float* x) {
    if (factor_ <= 1) return x[0];
    const int taps = static_cast<int>(h_.size());
    for (int k = 0; k < factor_; ++k) {
      buf_[static_cast<std::size_t>(pos_)] = buf_[static_cast<std::size_t>(pos_ + taps)] = x[k];
      pos_ = (pos_ + 1) % taps;
    }
    // buf_[pos_ .. pos_+taps) is oldest..newest
    float acc = 0.f;
    const float* b = &buf_[static_cast<std::size_t>(pos_)];
    for (int i = 0; i < taps; ++i) acc += h_[static_cast<std::size_t>(taps - 1 - i)] * b[i];
    return acc;
  }
  void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); }

 private:
  int factor_ = 1, pos_ = 0;
  std::vector<float> h_, buf_;
};

struct InstrumentControls {
  FrictionModel friction = FrictionModel::Enhanced;
  float noise = 0.12f;       // rosin noise during slip (fixed in the synth; the labs may vary it)
  float intonation = 0.8f;   // 0..1: how much the player corrects flattening
  float coupling = 1.f;      // bridge admittance scale (fixed at 1 in the synth; 0 = a rigid bridge, for the labs)
  float hillHz = 2600.f;     // bridge hill centre (admittance; the radiation follows in the synth)
  float mute = 0.f;          // 0..1: the mute's mass on the bridge
  float slideNoise = 0.4f;   // 0..1: winding noise under a sliding finger
  float bowWidth = 0.006f;   // m: hair in contact (two contact points this far apart; 0 = one point) [T]
  float wolf = 0.f;          // 0..1: the wolf eliminator (instruments with a wolfMode)
  float bowCatch = 1.f;      // 0..2: the recorded onsets' level (instruments with onsets; 1 = as recorded)
  float bowNoise = 1.f;      // 0..2: the held tone's added bow noise (instruments with a table; 1 = as measured)
};

class Instrument {
 public:
  static constexpr int kVoices = Performer::kVoices;

  // def: which instrument (model/defs/; it must outlive this object).
  void prepare(double hostRate, const InstrumentDef& def = violin()) {
    host_ = static_cast<float>(hostRate);
    os_ = hostRate < 70000.0 ? 2 : 1;
    fs_ = host_ * static_cast<float>(os_);
    harmGlide_ = 1.f - std::exp(-1.f / (0.02f * host_));  // ~20 ms in/out
    dec_.prepare(os_);
    def_ = &def;
    vertLoss_ = def_->loss;
    for (float* r : {&vertLoss_.lowRate, &vertLoss_.highRate, &vertLoss_.fingerLow, &vertLoss_.fingerHigh}) *r /= def_->vertT60;
    for (auto& d : v_) {
      d.str.prepare(fs_, 40.f);
      d.vert.prepare(fs_, 40.f);
      d.onsets.prepare(host_);
      if (def_->bowNoise) d.bowNoise.prepare(host_, *def_->bowNoise);
    }
    for (int s = 0; s < 4; ++s) {
      sym_[s].prepare(fs_, 40.f);
      sym_[s].setDef(def_->strings[s], def_->loss);
    }
    perf_.prepare(host_, *def_);
    noiseLevelA_ = 1.f - std::exp(-1.f / (0.02f * host_));
    levelA_ = 1.f - std::exp(-1.f / (0.0025f * host_));
    {
      float z[4];
      for (int s = 0; s < 4; ++s) z[s] = def_->strings[s].z0;
      bridge_.setStringImpedances(z);
    }
    bridge_.prepare(fs_, def_->modes, def_->modeCount, def_->hill, def_->bodySeed, def_->denseFrom, def_->denseTo,
                    def_->denseSpacing);
    perf_.setBridge(&bridge_);
    reset();
  }

  void reset() {
    perf_.reset();
    for (int v = 0; v < kVoices; ++v) {
      Voice& d = v_[v];
      d.str.reset();
      d.vert.reset();
      d.onsets.reset();
      d.bowNoise.reset();
      d.claimSeen = perf_.voice(v).claimId();
      d.onsetSeen = perf_.voice(v).onset().id;
      d.slot = perf_.voice(v).string();
      d.quiet = 0;
      d.level2 = d.noiseLevel2 = 0.f;
      d.corr = 0.f;
      d.sinceSlip = 0;
      d.prevSlip = false;
      d.harmMix = d.harmMixTarget = 0.f;
      for (auto& t : d.tilt) t.lp = 0.f;
      d.harmLP.reset();
      d.pluck = d.tap = d.pluckV = 0.f;
    }
    for (int s = 0; s < 4; ++s) {
      sym_[s].reset();
      symOn_[s] = true;
      symDamp_[s] = 0.f;
      symZ_[s] = def_->strings[s].z0;
    }
    bridge_.setSlotImpedances(symZ_);
    dec_.reset();
    tick_ = 0;
    dcX_ = dcY_ = 0.f;
    bridge_.reset();
    uPrev_ = 0.f;
  }

  Performer& performer() { return perf_; }
  const Performer& performer() const { return perf_; }
  const Bridge& bridge() const { return bridge_; }
  const InstrumentDef& def() const { return *def_; }
  // A voice's string (labs).
  BowedString& voiceString(int v) { return v_[v].str; }
  // The string at notch s (labs): the newest voice's if it plays there, else
  // the open sympathetic string.
  BowedString& string(int s) {
    const int lead = perf_.leadVoice();
    if (perf_.loaded(lead) && v_[lead].slot == s) return v_[lead].str;
    return sym_[s];
  }
  BowedString& sympatheticString(int s) { return sym_[s]; }

  // One host sample of summed bridge force (N).
  float process(const PerformerControls& pc, const InstrumentControls& ic) {
    if (tick_ == 0) controlTick(pc, ic);
    tick_ = (tick_ + 1) % kControl;
    // A voice given a new note: its string starts afresh.
    for (int v = 0; v < kVoices; ++v)
      if (perf_.loaded(v) && perf_.voice(v).claimId() != v_[v].claimSeen) startVoice(v, pc, ic);
    perf_.tickSample(pc);
    for (int v = 0; v < kVoices; ++v) {
      Voice& d = v_[v];
      if (!perf_.loaded(v)) continue;
      const Player& p = perf_.voice(v);
      d.pluck = p.pluckForce();
      d.tap = p.fingerTap();
      d.pluckV = p.pluckForceVertical();
      d.vb = p.bowVelocity();
      d.slipSteps = 0;
      d.hostAcc = 0.f;
    }
    float x[4];
    for (int k = 0; k < os_; ++k) x[k] = internalSample(ic);
    float y = dec_.process(x);
    if (def_->onsets) {
      // Per voice: the recorded onsets and the held tone's bow noise follow
      // that voice's own level (its share of the bridge force).
      const float inv = 1.f / static_cast<float>(os_);
      for (int v = 0; v < kVoices; ++v) {
        if (!perf_.loaded(v)) continue;
        Voice& d = v_[v];
        const float yv = d.hostAcc * inv;
        // The note's envelope for the onsets (Onsets.h): RMS over ~2.5 ms
        // (tools/make_onsets.py follows the recordings' tone the same way).
        d.level2 += (yv * yv - d.level2) * levelA_;
        y += d.onsets.process(std::sqrt(d.level2));
        if (def_->bowNoise) {
          // The held tone's bow noise (BowNoise.h): re the note's level over
          // ~20 ms, in bursts while the bowed string slips.
          d.noiseLevel2 += (yv * yv - d.noiseLevel2) * noiseLevelA_;
          rngN_ = rngN_ * 1664525u + 1013904223u;
          const float white = static_cast<float>(rngN_ >> 8) * (2.f / 16777216.f) - 1.f;
          y += d.bowNoise.process(std::sqrt(d.noiseLevel2), static_cast<float>(d.slipSteps) * inv, white);
        }
      }
    }
    // DC blocker (5 Hz).
    const float out = y - dcX_ + dcR_ * dcY_;
    dcX_ = y;
    dcY_ = out;
    return out;
  }

 private:
  static constexpr int kControl = 16;        // host samples per control tick
  static constexpr float kWindDirect = 1.f;  // winding noise straight to the bridge [T]
  static constexpr float kTiltDb = 5.f;      // dB (at the top) per octave of bow force re mf [T]
  static constexpr float kTiltMax = 9.f;     // dB
  static constexpr float kHarmLP = 3.f;      // harmonics' low-pass, x the sounding pitch [T]
  // A released voice's string is let go once this quiet (m/s, its waves'
  // recent peak; a bowed note's are ~0.1) for kFreeTicks control ticks: what
  // the bridge still drives it with is far below hearing then [T].
  static constexpr float kFreeLevel = 2e-5f;
  static constexpr int kFreeTicks = 120;  // ~40 ms at 48 kHz
  static constexpr int kDyingTicks = 60;  // a voice let go to make room: freed this long after it's fully damped

  // First-order high shelf: x + (g - 1) x highpass(x) at fc.
  struct Shelf {
    float a = 0.f, g = 1.f, lp = 0.f;
    void set(float fc, float gain, float fs) {
      a = 1.f - std::exp(-2.f * 3.14159265f * fc / fs);
      g = gain;
    }
    float process(float x) {
      lp += a * (x - lp);
      return x + (g - 1.f) * (x - lp);
    }
  };
  // Harmonics' purity (see voiceControl): 2-pole low-pass, crossfaded in.
  struct LowPass2 {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    void set(float fc, float fs) {  // RBJ, Q 0.707
      const float w = 2.f * 3.14159265f * std::min(fc, 0.45f * fs) / fs, al = std::sin(w) / (2.f * 0.70710678f), c = std::cos(w);
      const float n = 1.f / (1.f + al);
      b0 = b2 = 0.5f * (1.f - c) * n;
      b1 = (1.f - c) * n;
      a1 = -2.f * c * n;
      a2 = (1.f - al) * n;
    }
    void reset() { z1 = z2 = 0.f; }
    float process(float x) {  // transposed direct form II
      const float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };

  // One voice's string and what goes with it.
  struct Voice {
    BowedString str, vert;  // vert: the vertical polarisation (pizzicato)
    StringTarget target;
    int slot = 0;           // the notch it stands at (which string)
    int claimSeen = 0, onsetSeen = 0, landSeen = 0;
    float vb = 0.f, pluck = 0.f, tap = 0.f, pluckV = 0.f;  // this host sample
    float sv = 0.f, direct = 0.f;  // this internal sample: 2 Z a, the winding noise's force
    bool active = false;
    float corr = 0.f, wantHz = 440.f;  // the ear's correction (cents) and the pitch wanted
    int sinceSlip = 0;
    bool prevSlip = false;
    BandPass windBp;
    float windAmp = 0.f;
    Shelf tilt[2];
    LowPass2 harmLP;
    float harmMix = 0.f, harmMixTarget = 0.f;
    OnsetPlayer onsets;
    BowNoise bowNoise;
    float level2 = 0.f, noiseLevel2 = 0.f;  // the voice's envelope (squared): onsets (~2.5 ms), bow noise (~20 ms)
    int slipSteps = 0;                      // internal steps this host sample with the bowed string slipping
    float hostAcc = 0.f;                    // this host sample's output, summed over the internal steps
    int quiet = 0;                          // control ticks quiet (freeing)
  };

  // One internal-rate sample: every string's bow junction, then the bridge
  // they share, solved together without a delay (Bridge.h):
  //   F = Σ Z_i (2 a_i - u) = S - Zs u,   u = Y0 F + h
  //   =>  u = (Y0 S + h) / (1 + Y0 Zs)
  // A sleeping string still loads the bridge (its Z is in Zs). The
  // sympathetic strings wake when the bridge moves, which is how they ring.
  float internalSample(const InstrumentControls& ic) {
    float S[4] = {0.f, 0.f, 0.f, 0.f}, Z[4] = {0.f, 0.f, 0.f, 0.f}, symS[4] = {0.f, 0.f, 0.f, 0.f};
    bool any = false;
    const bool coupled = bridge_.coupling() > 0.f;
    const bool bridgeMoving = coupled && std::fabs(uPrev_) > 1e-7f;
    for (int v = 0; v < kVoices; ++v) {
      if (!perf_.loaded(v)) continue;
      Voice& d = v_[v];
      BowedString& st = d.str;
      const StringTarget& t = d.target;
      Z[d.slot] += st.z0();
      d.active = !st.asleep() || t.force > 0.f || t.damp > 0.f || d.windAmp > 0.f || d.pluck != 0.f || d.tap != 0.f;
      d.sv = d.direct = 0.f;
      if (!d.active) continue;
      any = true;
      st.wake();
      rng_ = rng_ * 1664525u + 1013904223u;
      const float noise = static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
      // Winding noise: injected at the finger (it travels the string), and,
      // because a sticking bow reflects it back toward the finger, also
      // straight into the bridge force (in reality it also reaches the body
      // through the neck and the string's other wave types) [T].
      float wn = 0.f;
      if (d.windAmp > 0.f) wn = d.windAmp * d.windBp.process(2.f * noise - 1.f);
      // A finger landing or lifting (slurs) taps the string at the finger.
      st.setFingerNoise(wn + d.tap);
      d.direct = kWindDirect * st.z0() * wn;
      d.sv = 2.f * st.z0() * st.step(d.vb, t.force, ic.friction, ic.noise, noise, t.damp, d.pluck);
      S[d.slot] += d.sv;
      // Intonation: slip onsets mark periods in Helmholtz motion.
      const bool sl = st.slipping();
      if (sl && t.force > 0.f) ++d.slipSteps;
      ++d.sinceSlip;
      if (sl && !d.prevSlip) {
        onPeriod(d, static_cast<float>(d.sinceSlip), ic);
        d.sinceSlip = 0;
      }
      d.prevSlip = sl;
    }
    bool symAwake = false;
    for (int s = 0; s < 4; ++s) {
      if (!symOn_[s]) continue;
      BowedString& st = sym_[s];
      Z[s] += st.z0();
      if (st.asleep() && !bridgeMoving && symDamp_[s] <= 0.f) continue;
      st.wake();
      symAwake = true;
      symS[s] = 2.f * st.z0() * st.step(0.f, 0.f, ic.friction, 0.f, 0.f, symDamp_[s], 0.f);
      S[s] += symS[s];
    }
    if (!any && !symAwake && (!coupled || bridge_.activity() < 1e-12f) && vertAsleep()) {
      uPrev_ = 0.f;
      return 0.f;
    }
    // The bridge velocity at each notch.
    float u[4] = {0.f, 0.f, 0.f, 0.f};
    const bool weighted = coupled && bridge_.weighted();
    if (weighted) {
      // Per-string mode strengths (Bridge.h): each notch sees its own
      // bridge velocity.
      bridge_.setSlotImpedances(Z);
      bridge_.solveWeighted(S, u);
    } else if (coupled) {
      const float y0 = bridge_.y0();
      const float Ssum = S[0] + S[1] + S[2] + S[3], zs = Z[0] + Z[1] + Z[2] + Z[3];
      const float uu = (y0 * Ssum + bridge_.history()) / (1.f + y0 * zs);
      u[0] = u[1] = u[2] = u[3] = uu;
    }
    // Each voice's force on the bridge, through its own brightness tilt and
    // harmonics filter (outside the loop, as the output's colour), into the
    // output; the sympathetic strings and the sleeping strings' load as they
    // are.
    float out = 0.f;
    for (int v = 0; v < kVoices; ++v) {
      if (!perf_.loaded(v)) continue;
      Voice& d = v_[v];
      const float uv = u[d.slot];
      if (d.active) d.str.finish(uv);
      float f = d.sv - d.str.z0() * uv + d.direct;
      f = d.tilt[1].process(d.tilt[0].process(f));
      if (d.harmMix > 0.f) f += d.harmMix * (d.harmLP.process(f) - f);
      d.hostAcc += f;
      out += f;
    }
    for (int s = 0; s < 4; ++s) {
      if (!symOn_[s]) continue;
      if (!sym_[s].asleep()) sym_[s].finish(u[s]);
      out += symS[s] - sym_[s].z0() * u[s];
    }
    float F[4];
    float um = 0.f;
    for (int s = 0; s < 4; ++s) {
      F[s] = S[s] - Z[s] * u[s];
      um = std::max(um, std::fabs(u[s]));
    }
    if (weighted) bridge_.advanceWeighted(F);
    else if (coupled) bridge_.advance(F[0] + F[1] + F[2] + F[3]);
    uPrev_ = um;
    // Vertical polarisations (plucks only): on a rigid bridge, into the output.
    float vert = 0.f;
    for (int v = 0; v < kVoices; ++v) {
      if (!perf_.loaded(v)) continue;
      Voice& d = v_[v];
      BowedString& vs = d.vert;
      if (vs.asleep() && d.pluckV == 0.f) continue;
      vs.wake();
      // A damping hand or resting bow stops both polarisations.
      vs.step(0.f, 0.f, ic.friction, 0.f, 0.f, d.target.damp, d.pluckV);
      vert += vs.finish(0.f);
    }
    return out + def_->vertGain * vert;
  }

  bool vertAsleep() const {
    for (int v = 0; v < kVoices; ++v)
      if (perf_.loaded(v) && (!v_[v].vert.asleep() || v_[v].pluckV != 0.f)) return false;
    return true;
  }

  // A voice given a new note: its string, at its notch, starts at rest.
  void startVoice(int v, const PerformerControls& pc, const InstrumentControls& ic) {
    Voice& d = v_[v];
    const Player& p = perf_.voice(v);
    d.claimSeen = p.claimId();
    d.onsetSeen = p.claimOnset();  // a stroke started with the note still gets its onset
    setSlot(d, p.string());
    d.str.reset();
    d.vert.reset();
    d.corr = 0.f;
    d.sinceSlip = 0;
    d.prevSlip = false;
    d.quiet = 0;
    d.level2 = d.noiseLevel2 = 0.f;
    d.onsets.reset();
    d.bowNoise.reset();
    for (auto& t : d.tilt) t.lp = 0.f;
    d.harmLP.reset();
    d.harmMix = 0.f;
    d.windAmp = 0.f;
    // Its pitch and the rest from the first sample (not the next control tick).
    d.landSeen = -1;
    p.target(d.target, pc);
    d.landSeen = d.target.landId;  // no landing to play at the start
    voiceControl(v, ic, true);
  }
  void setSlot(Voice& d, int s) {
    d.slot = std::clamp(s, 0, 3);
    d.str.setDef(def_->strings[d.slot], def_->loss);
    d.vert.setDef(def_->strings[d.slot], vertLoss_);
  }

  void controlTick(const PerformerControls& pc, const InstrumentControls& ic) {
    bridge_.setCoupling(ic.coupling);
    if (def_->wolfMode >= 0) bridge_.setDamper(def_->wolfMode, ic.wolf);
    // A mute moves the hill to ~65% and weakens it (KB §11.2) [T level].
    bridge_.setHill(ic.hillHz * (1.f - 0.35f * ic.mute), 1.f - 0.4f * ic.mute);
    perf_.controlTick(pc, kControl / host_);
    bool claimed[4] = {false, false, false, false};
    for (int v = 0; v < kVoices; ++v) {
      if (!perf_.loaded(v)) continue;
      Voice& d = v_[v];
      const Player& p = perf_.voice(v);
      if (p.string() != d.slot) setSlot(d, p.string());  // (a voice keeps its string; just in case)
      p.target(d.target, pc);
      startOnset(v, ic);
      voiceControl(v, ic, false);
      // Free a voice whose string is quiet and that has nothing left to do.
      const float level = std::max(d.str.level(), d.vert.level());
      perf_.setLevel(v, level);
      const bool done = !p.busy() && (level < kFreeLevel || (p.dying() && p.damp() >= 1.f));
      d.quiet = done ? d.quiet + 1 : 0;
      if (d.quiet >= (p.dying() ? kDyingTicks : kFreeTicks)) {
        perf_.freeVoice(v);
        d.str.reset();
        d.vert.reset();
        continue;
      }
      claimed[d.slot] = true;
    }
    // The sympathetic strings: open, at true pitch (in pure fifths around A
    // with Intonation Style), undamped; damped and taken off a notch a voice
    // stands at, back when it's free.
    const float kd = static_cast<float>(kControl) / (0.015f * host_);
    for (int s = 0; s < 4; ++s) {
      if (claimed[s]) {
        if (symOn_[s]) {
          symDamp_[s] = std::min(1.f, symDamp_[s] + kd);
          if (symDamp_[s] >= 1.f && (sym_[s].asleep() || sym_[s].level() < kFreeLevel)) {
            symOn_[s] = false;
            sym_[s].reset();
          }
        }
      } else if (!symOn_[s]) {
        symOn_[s] = true;
        symDamp_[s] = 0.f;
        sym_[s].reset();
      } else {
        symDamp_[s] = std::max(0.f, symDamp_[s] - kd);
      }
      if (!symOn_[s]) continue;
      const float n = static_cast<float>(def_->strings[s].openNote) + 0.01f * pc.intonStyle * Player::openPureCents(*def_, s);
      const float hz = Player::mtof(n, pc.a4);
      sym_[s].setTouch(0.f, 0.f);
      sym_[s].setBowWidth(0);
      sym_[s].setGeometry(hz, hz, kSymBeta, false, 0.f);
    }
    dcR_ = std::exp(-2.f * 3.14159265f * 5.f / host_);
  }

  // A voice's string at control rate: geometry, the ear's correction,
  // winding noise, its output colour.
  void voiceControl(int v, const InstrumentControls& ic, bool first) {
    Voice& d = v_[v];
    const Player& p = perf_.voice(v);
    const StringTarget& t = d.target;
    const int s = d.slot;
    // Force-dependent brightness (KB §2 "corner rounding", §11.5: log centroid
    // rises linearly with log bow force). With the loss shelves the string
    // gives about half of the recordings' p -> f brightening by itself (VSCO
    // G3-E5 median, 2-4/4-8/8-16 kHz: +1.3/+2.9/+3.4 dB of +2.5/+6.4/+8.0); the
    // output is tilted for the rest, outside the loop: kTiltDb per octave of
    // force re mf, half on a shelf at 2.5 kHz, half at 7 kHz [T, fitted to
    // VSCO p/f: +3.0/+6.1/+8.2 dB]. Each voice by its own bow.
    const float tiltDb = std::clamp(kTiltDb * p.brightness(), -kTiltMax, kTiltMax);
    const float g = std::pow(10.f, 0.5f * tiltDb / 20.f);
    d.tilt[0].set(2500.f, g, fs_);
    d.tilt[1].set(7000.f, g, fs_);
    // Harmonics sound purer than a stopped note at the same pitch: partials
    // 3-6 are 5-12 dB weaker in the Philharmonia recordings (harmonic vs
    // arco-normal, same pitch: mics and body cancel), while the touched
    // waveguide gives the stopped note's spectrum. The finger pad (~1 cm)
    // damps the higher partials, but any loss in the loop stops the light bow
    // a harmonic needs, so the voice's output is low-passed instead, outside
    // the loop, at kHarmLP x the sounding pitch while it's bowed touched [T].
    d.harmMixTarget = 0.f;
    if (t.touchQ > 0.f && t.force > 0.f) {
      d.harmLP.set(kHarmLP * t.hz / t.touchQ, fs_);
      d.harmMixTarget = 1.f;
    }
    // (glides ~20 ms; per control tick)
    const float hg = 1.f - std::pow(1.f - harmGlide_, static_cast<float>(kControl));
    d.harmMix += (d.harmMixTarget - d.harmMix) * hg;
    if (d.harmMix < 1e-4f && d.harmMixTarget == 0.f) d.harmMix = 0.f;
    if (!t.stopped || ic.intonation <= 0.f) d.corr *= 0.995f;
    const float k = std::pow(2.f, d.corr / 1200.f);
    d.wantHz = t.hz;
    d.str.setTouch(t.touchQ, t.touchR);
    if (!first && t.landId != d.landSeen) {
      d.landSeen = t.landId;
      d.str.land(t.landUp, t.landT);
    }
    const float beta = p.beta();
    // Width in samples: the wave speed is 2 x scale x open pitch on any stop.
    const float openHz = 440.f * std::pow(2.f, (static_cast<float>(def_->strings[s].openNote) - 69.f) / 12.f);
    d.str.setBowWidth(ic.bowWidth > 0.f ? static_cast<int>(std::lround(ic.bowWidth * fs_ / (2.f * def_->scaleLength * openHz))) : 0);
    d.str.setGeometry(t.hz * k, t.noteHz * k, beta, t.stopped, t.slide);
    const float kv = k * std::pow(2.f, def_->vertDetuneCents / 1200.f);
    d.vert.setGeometry(t.hz * kv, t.noteHz * kv, beta, t.stopped, t.slide);
    // Winding noise: the finger rubbing turns of winding at speed v gives a
    // band of noise around v / pitch (KB §11.2 "Shifts") [T level].
    const float wv = t.slide * def_->winding[s];
    d.windAmp = ic.slideNoise * 0.3f * std::min(wv, 2.f);  // ~-18 dB re the note at 1, ~-26 dB at 0.4 [T]
    if (d.windAmp > 0.f) {
      const float fc = std::clamp(wv / def_->windingPitch, 300.f, 0.4f * fs_);
      d.windBp.setBandPass(fc, 2.f, fs_);
    }
    if (def_->bowNoise) {
      const OnsetEvent& ev = p.onset();
      d.bowNoise.setTarget(s, ev.vel, ic.bowNoise, def_->bowNoiseTrimDb);
    }
  }

  // Recorded onsets (Onsets.h, MODEL.md "Recorded onsets"): a new stroke
  // starts the nearest clip, at the note's level by the performer's level law
  // (L = 7.7 ln(v_b / beta) dB, so amplitude ~ (v_b / beta)^0.886, x Z0).
  // From the air the clip follows the note's own build-up, capped at
  // kAirHeadroom x that level; bow changes on the string catch less than a
  // bow landing (kRebowCatch) and use the level alone;
  // short strokes (spiccato, sautille) are cut to kShortCatch; harmonics and
  // pizzicato have none [T].
  static constexpr float kRebowCatch = 0.5f;
  static constexpr float kAirHeadroom = 2.f;
  static constexpr float kShortCatch = 0.08f;  // s
  void startOnset(int v, const InstrumentControls& ic) {
    Voice& d = v_[v];
    const OnsetEvent& ev = perf_.voice(v).onset();
    if (ev.id == d.onsetSeen) return;
    d.onsetSeen = ev.id;
    if (!def_->onsets || ic.bowCatch <= 0.f || ev.art == kHarmonics || ev.art == kPizzicato) return;
    const int s = std::clamp(ev.string, 0, 3);
    // The fitted scale (InstrumentDef::onsetGain, per-string trims) applies to
    // both: the clips' sawtooth convention against this model's bridge force.
    const float k = def_->onsetGain * std::pow(10.f, def_->onsetTrimDb[s] / 20.f);
    float cap = def_->strings[s].z0 * std::pow(std::max(ev.levelRatio, 1e-3f), def_->onsetExp[s]);
    if (!ev.rebow) cap *= kAirHeadroom;
    const float amount = ic.bowCatch * k * (ev.rebow ? kRebowCatch : 1.f);
    const float fadeEnd = ev.art == kSpiccato || ev.art == kSautille ? kShortCatch : 0.f;
    const float hz = d.target.noteHz;
    const int midi = static_cast<int>(std::lround(69.f + 12.f * std::log2(std::max(hz, 20.f) / 440.f)));
    d.onsets.trigger(*def_->onsets, midi, hz, ev.vel, ev.down, cap, !ev.rebow, amount, fadeEnd);
  }

  void onPeriod(Voice& d, float period, const InstrumentControls& ic) {
    const StringTarget& t = d.target;
    if (!t.stopped || ic.intonation <= 0.f || t.force <= 0.f) return;
    // A touched (harmonic) string slips k times per fundamental period: the
    // player tunes an artificial harmonic by ear the same way.
    const float k = t.touchQ > 0.f ? std::round(1.f / t.touchQ) : 1.f;
    const float want = fs_ / (d.wantHz * k);
    const float err = period / want;
    if (err < 0.96f || err > 1.04f) return;  // not steady one-slip-per-period motion
    // Flat (long period) -> raise the finger pitch. ~60 periods to settle.
    d.corr += ic.intonation * 0.016f * 1200.f * std::log2(err);
    d.corr = std::clamp(d.corr, -60.f, 60.f);
  }

  static constexpr float kSymBeta = 0.1f;  // where the (absent) bow would be on a sympathetic string: no effect unbowed

  const InstrumentDef* def_ = nullptr;
  LossDef vertLoss_{};
  Voice v_[kVoices];
  BowedString sym_[4];
  bool symOn_[4]{true, true, true, true};
  float symDamp_[4]{}, symZ_[4]{};
  Performer perf_;
  float levelA_ = 0.f, noiseLevelA_ = 0.f;
  uint32_t rngN_ = 0x2545f491u;  // the bow noise's own generator
  float harmGlide_ = 0.f;
  Decimator dec_;
  Bridge bridge_;
  float uPrev_ = 0.f;
  float host_ = 48000.f, fs_ = 96000.f;
  int os_ = 2, tick_ = 0;
  float dcX_ = 0.f, dcY_ = 0.f, dcR_ = 0.999f;
  uint32_t rng_ = 0x9e3779b9u;
};

}  // namespace sm
