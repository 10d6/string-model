// String Model: a physically modelled bowed string instrument (violin first;
// cello, viola and bass planned behind the Instrument switch).
//
// The whole instrument (strings, bow, gesture layer) is one object in the
// synth, run in processGlobal: the strings will share a bridge (sympathetic
// ringing, wolf notes), which per-note voices can't do sample by sample.
// processEvents hands the notes and controllers to the instrument; the kit's
// voices are silent placeholders that only track the keys. See docs/MODEL.md.
#include <synthkit/synth.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../model/Instrument.h"
#include "../model/Meter.h"
#include "../model/Radiation.h"
#include "ir/ViolinIR.h"

using namespace sk;

struct StringModel;

// The kit's voices only follow the keys (for the page's meter and lit keys);
// they make no sound. The instrument plays every note itself.
struct SilentVoice : Voice {
  void start(const NoteOn&, const StartInfo&) override { held = true; }
  void release() override { held = false; }
  void render(VoiceContext&, Buffer&) override {}
  bool active() const override { return held; }
  bool held = false;
};

namespace {

// Resample an impulse response with a windowed sinc (prepare time only).
std::vector<float> resampleIr(const float* x, int n, double from, double to) {
  if (std::fabs(from - to) < 1.0) return std::vector<float>(x, x + n);
  const double ratio = to / from;
  const int m = static_cast<int>(std::ceil(n * ratio));
  std::vector<float> y(static_cast<std::size_t>(m), 0.f);
  const double cutoff = std::min(1.0, ratio) * 0.97;  // of the lower Nyquist
  const int half = 32;
  for (int i = 0; i < m; ++i) {
    const double t = i / ratio;  // position in source samples
    const int c = static_cast<int>(std::floor(t));
    double acc = 0;
    for (int k = c - half + 1; k <= c + half; ++k) {
      if (k < 0 || k >= n) continue;
      const double d = t - k;
      const double arg = 3.141592653589793 * d * cutoff;
      const double sinc = d == 0 ? 1.0 : std::sin(arg) / arg;
      const double w = 0.5 + 0.5 * std::cos(3.141592653589793 * d / half);
      acc += x[k] * sinc * w * cutoff;
    }
    y[static_cast<std::size_t>(i)] = static_cast<float>(acc);
  }
  return y;
}

}  // namespace

struct StringModel : Synth {
  Param autoChange, sustainSpeed, coupling, chordWindow, rollTime, hill, mute, resonance, a0, nasal, brilliance, softness, voice, width, releaseMode, slideNoise, stringSel, keyswitches, artic, tremRate, intonStyle, vibShape, metric, portamento;
  Param instrument, dynamics, velSens, pressure, atPressure, contact, bite, legato, bowChange, noise, friction;
  Param vibDepth, vibRate, vibDelay, shiftTime, intonation, output, roomMix, roomSpace;
  Param slurRange;
  Param micAngle, micElevation;
  Param bendRange, a4;

  void defineParams(ParamBuilder& b) override {
    instrument = b.add("instrument").name("Instrument").choices({"Violin"}).group("Instrument");
    artic = b.add("artic").name("Articulation").choices({"Arco", "Martelé", "Spiccato", "Tremolo", "Pizzicato", "Harmonics", "Sautillé"}).def(0).group("Instrument");
    tremRate = b.add("artic.tremRate").name("Tremolo Rate").range(6, 18).def(12.2).unit("/s").smoothing(0).group("Bow");
    dynamics = b.add("bow.dynamics").name("Dynamics").range(0, 1).def(0.55).smoothing(0).group("Bow").cc(1).cc(11);
    velSens = b.add("bow.velSens").name("Velocity → Dynamics").range(0, 1).def(0.7).smoothing(0).group("Bow");
    pressure = b.add("bow.pressure").name("Pressure").range(-1, 1).def(0).smoothing(0).group("Bow");
    atPressure = b.add("bow.atPressure").name("Aftertouch → Pressure").range(0, 1).def(0.5).smoothing(0).group("Bow");
    contact = b.add("bow.contact").name("Contact Point").range(-1, 1).def(0).smoothing(0).group("Bow").cc(74);
    bite = b.add("bow.bite").name("Attack Bite").range(0, 1).def(0.3).smoothing(0).group("Bow");
    legato = b.add("bow.legato").name("Legato Slurs").toggle().def(1).group("Bow");
    bowChange = b.add("bow.change").name("Bow Change").range(0.008, 0.2).def(0.02).unit("s").smoothing(0).group("Bow");
    autoChange = b.add("bow.auto").name("Auto Bow Change").toggle().def(0).group("Bow");
    sustainSpeed = b.add("bow.sustain").name("Long-Note Bow Speed").range(0.25, 1).def(0.6).smoothing(0).group("Bow");
    releaseMode = b.add("bow.release").name("Release").choices({"Auto (Key-Up Speed)", "Lift", "Stop on String", "Finger Off"}).def(0).group("Bow");
    noise = b.add("bow.noise").name("Rosin Noise").range(0, 0.5).def(0.12).smoothing(0).group("Bow");
    friction = b.add("bow.friction").name("Friction Model").choices({"Thermal", "Classic", "Enhanced Thermal"}).def(2).group("Bow");
    chordWindow = b.add("chord.window").name("Chord Window").range(0, 0.08).def(0.035).unit("s").smoothing(0).group("Bow");
    rollTime = b.add("chord.roll").name("Chord Roll").range(0.02, 0.2).def(0.06).unit("s").smoothing(0).group("Bow");
    vibDepth = b.add("vib.depth").name("Vibrato Depth").range(0, 60).def(16).unit("ct").smoothing(0).group("Left Hand");
    vibRate = b.add("vib.rate").name("Vibrato Rate").range(3, 9).def(5.5).unit("Hz").smoothing(0).group("Left Hand");
    vibDelay = b.add("vib.delay").name("Vibrato Delay").range(0, 1).def(0.25).unit("s").smoothing(0).group("Left Hand");
    shiftTime = b.add("finger.shift").name("Shift Time").range(0, 0.3).def(0.04).unit("s").smoothing(0).group("Left Hand");
    stringSel = b.add("string.select").name("String").choices({"Auto", "G", "D", "A", "E"}).def(0).group("Left Hand");
    keyswitches = b.add("string.keys").name("Keyswitches").toggle().def(1).group("Left Hand");
    slideNoise = b.add("finger.noise").name("Slide Noise").range(0, 1).def(0.4).smoothing(0).group("Left Hand");
    intonation = b.add("finger.intonation").name("Intonation").range(0, 1).def(0.8).smoothing(0).group("Left Hand");
    coupling = b.add("body.coupling").name("Body Coupling").range(0, 2).def(1).smoothing(0).group("Instrument");
    hill = b.add("body.hill").name("Bridge Hill").range(1800, 3400).log().def(2600).unit("Hz").smoothing(0).group("Instrument");
    mute = b.add("body.mute").name("Mute").toggle().def(0).group("Instrument");
    resonance = b.add("body.resonance").name("Body Resonance").range(0, 1).def(1).smoothing(0).group("Instrument");
    output = b.add("out.mode").name("Output").choices({"Library Mic", "Library Mic (Smooth)", "Pickup", "Modelled"}).def(3).group("Output");
    width = b.add("out.width").name("Stereo Width").range(0, 1).def(1).smoothing(0).group("Output");
    roomMix = b.add("room.mix").name("Room").range(0, 100).def(20).unit("%").group("Output");
    roomSpace = b.add("room.space").name("Space").choices({"Room", "Chamber", "Hall", "Cathedral", "Plate"}).def(2).group("Output");
    // Body character (KB §5.8): offsets from the measured violin, in dB.
    a0 = b.add("body.a0").name("Air Resonance").range(-9, 9).def(0).unit("dB").smoothing(0).group("Body");
    nasal = b.add("body.nasal").name("Nasality").range(-9, 9).def(0).unit("dB").smoothing(0).group("Body");
    brilliance = b.add("body.brilliance").name("Brilliance").range(-9, 9).def(0).unit("dB").smoothing(0).group("Body");
    softness = b.add("body.softness").name("Softness").range(-6, 12).def(0).unit("dB").smoothing(0).group("Body");
    voice = b.add("body.voice").name("Voice").range(-10, 10).def(0).unit("%").smoothing(0).group("Body");
    // Player habits (instrument-research player_technique_and_responsiveness.md).
    intonStyle = b.add("finger.style").name("Intonation Style").range(0, 1).def(0.7).smoothing(0).group("Left Hand");
    vibShape = b.add("vib.shape").name("Vibrato Shaping").range(0, 1).def(0.7).smoothing(0).group("Left Hand");
    metric = b.add("bow.metric").name("Metric Emphasis").range(0, 1).def(0.5).smoothing(0).group("Bow");
    portamento = b.add("finger.portamento").name("Portamento").range(0, 1).def(0.3).smoothing(0).group("Left Hand");
    slurRange = b.add("bow.slurRange").name("Slur Range").range(1, 24).integer().def(12).unit("st").group("Bow");
    // Phase 7a: where the mic pair is (Modelled output), in the player's frame.
    micAngle = b.add("mic.angle").name("Mic Angle").range(-180, 180).def(0).unit("°").group("Output");
    micElevation = b.add("mic.elevation").name("Mic Elevation").range(-30, 90).def(30).unit("°").group("Output");
  }

  std::unique_ptr<Voice> makeVoice() override { return std::make_unique<SilentVoice>(); }

  void prepare(const PrepareInfo& info) override {
    sr_ = info.sampleRate;
    bendRange = info.param("bendRange");
    a4 = info.param("sk.a4");
    inst_.prepare(sr_);
    {
      const sm::InstrumentDef& d = inst_.def();
      rad_.prepare(static_cast<float>(sr_), d.radiationHz, d.radiationDb, d.radiationCount, d.bodySeed ^ 0x9e3779b9u, d.hill.hz,
                   d.radiationLowCut);
    }
    muteSmooth_ = 0.f;
    radTick_ = 0;
    // Body: the CC0 library-mic responses (bridge force -> recording).
    auto l = resampleIr(sm::ir::kViolinLeft, sm::ir::kViolinLeftLen, sm::ir::kViolinRate, sr_);
    auto r = resampleIr(sm::ir::kViolinRight, sm::ir::kViolinRightLen, sm::ir::kViolinRate, sr_);
    auto m = resampleIr(sm::ir::kViolinMonoSmooth, sm::ir::kViolinMonoSmoothLen, sm::ir::kViolinRate, sr_);
    // The IRs are peak-normalised; scale so their sum keeps a level that is
    // independent of the sample rate.
    const float rateGain = static_cast<float>(48000.0 / sr_);
    for (auto* v : {&l, &r, &m})
      for (float& x : *v) x *= rateGain;
    const std::size_t n = std::max(l.size(), r.size());
    l.resize(n, 0.f);
    r.resize(n, 0.f);
    const float* st[2] = {l.data(), r.data()};
    stereoIr_.setImpulse(st, 2, static_cast<int>(n), sr_, false);
    const float* mo[1] = {m.data()};
    smoothIr_.setImpulse(mo, 1, static_cast<int>(m.size()), sr_, false);
    reverb_.prepare(sr_, 1200);
    queued_ = 0;
    blockPos_ = 0;
    pitchBend_ = 0.f;
    pressureAt_ = 0.f;
  }

  void processEvents(EventContext& ctx) override {
    // Anything left from the last host block (shouldn't happen) applies now.
    for (int i = 0; i < queued_; ++i) queue_[i].sample = 0;
    blockPos_ = 0;
    const bool keys = ctx.on(keyswitches);
    transport_ = ctx.transport();
    for (auto& e : ctx.in()) {
      // Keyswitches set a parameter (the page and host follow) and don't sound:
      // C1 Auto, C#1 G, D1 D, D#1 A, E1 E (String);
      // F1 Arco, F#1 Martelé, G1 Spiccato, G#1 Tremolo, A1 Pizzicato, A#1 Harmonics, B1 Sautillé.
      if (keys && (e.isNoteOn() || e.isNoteOff()) && e.note >= kKeyswitchLow && e.note < kKeyswitchLow + 12) {
        if (e.isNoteOn()) {
          const int k = e.note - kKeyswitchLow;
          if (k < 5) ctx.setParam(stringSel, static_cast<float>(k));
          else ctx.setParam(artic, static_cast<float>(k - 5));
        }
        continue;
      }
      if (queued_ < kQueue) queue_[queued_++] = e;
      ctx.out(e);  // the silent voices follow the keys
    }
    // Keep the queue sorted by sample (insertion sort, small n).
    for (int i = 1; i < queued_; ++i) {
      MidiEvent e = queue_[i];
      int j = i - 1;
      while (j >= 0 && queue_[j].sample > e.sample) {
        queue_[j + 1] = queue_[j];
        --j;
      }
      queue_[j + 1] = e;
    }
  }

  void handle(const MidiEvent& e) {
    auto& p = inst_.performer();
    if (e.isNoteOn()) {
      // The meter only while a transport plays (the kit's clock always runs).
      const auto& t = transport_;
      p.setMeter(t.playing ? sm::metricStrength(e.beat - t.barStart, t.timeSigNum, t.timeSigDen) : -1.f);
      p.noteOn(e.note, e.vel, pc_);
    } else if (e.isNoteOff()) {
      p.noteOff(e.note, e.type == MidiEvent::Type::NoteOff && e.vel > 0.f ? e.vel : 64.f / 127.f, pc_);
    } else if (e.type == MidiEvent::Type::PitchBend) {
      pitchBend_ = e.value;
    } else if (e.type == MidiEvent::Type::ChannelPressure || e.type == MidiEvent::Type::PolyPressure) {
      pressureAt_ = e.value;
    } else if (e.type == MidiEvent::Type::CC && (e.cc == 120 || e.cc == 123)) {
      p.allNotesOff();
    }
  }

  void processGlobal(GlobalContext& ctx, Buffer& io) override {
    // The instrument's inputs have no kit smoothing (smoothing(0)): they step
    // at the controller's own sample, the same for any host block size, and
    // the performer smooths them on its own fixed clock. Bowing is chaotic
    // enough that block-dependent smoothing would change the notes.
    pc_.dynamics = ctx.get(dynamics);
    pc_.velSens = ctx.get(velSens);
    pc_.pressure = std::clamp(ctx.get(pressure) + ctx.get(atPressure) * pressureAt_, -1.f, 1.f);
    pc_.contact = ctx.get(contact);
    pc_.bite = ctx.get(bite);
    pc_.legato = ctx.on(legato);
    pc_.slurRange = ctx.get(slurRange);
    pc_.bowChange = ctx.get(bowChange);
    pc_.autoChange = ctx.on(autoChange);
    pc_.releaseMode = ctx.choice(releaseMode);
    pc_.forcedString = ctx.choice(stringSel) - 1;
    pc_.articulation = ctx.choice(artic);
    pc_.tremoloRate = ctx.get(tremRate);
    pc_.intonStyle = ctx.get(intonStyle);
    pc_.vibShape = ctx.get(vibShape);
    pc_.metric = ctx.get(metric);
    pc_.portamento = ctx.get(portamento);
    pc_.sustainSpeed = ctx.get(sustainSpeed);
    pc_.chordWindow = ctx.get(chordWindow);
    pc_.rollTime = ctx.get(rollTime);
    pc_.vibDepth = ctx.get(vibDepth);
    pc_.vibRate = ctx.get(vibRate);
    pc_.vibDelay = ctx.get(vibDelay);
    pc_.shiftTime = ctx.get(shiftTime);
    pc_.a4 = ctx.get(a4);
    static constexpr sm::FrictionModel kModels[3] = {sm::FrictionModel::Thermal, sm::FrictionModel::Classic, sm::FrictionModel::Enhanced};
    ic_.friction = kModels[std::clamp(ctx.choice(friction), 0, 2)];
    ic_.noise = ctx.get(noise);
    ic_.intonation = ctx.get(intonation);
    ic_.coupling = ctx.get(coupling);
    ic_.slideNoise = ctx.get(slideNoise);
    ic_.hillHz = ctx.get(hill);
    // The mute goes on over ~80 ms (it's put on by hand), on the samples'
    // own clock below.
    const float muteTarget = ctx.on(mute) ? 1.f : 0.f;
    sm::RadiationSettings rs;
    rs.hillHz = ctx.get(hill);
    rs.resonance = ctx.get(resonance);
    rs.width = ctx.get(width);
    rs.micAz = ctx.get(micAngle);
    rs.micEl = ctx.get(micElevation);
    rs.a0 = ctx.get(a0);
    rs.nasal = ctx.get(nasal);
    rs.brilliance = ctx.get(brilliance);
    rs.softness = ctx.get(softness);
    rs.voice = 0.01f * ctx.get(voice);
    const int mode = ctx.choice(output);
    const float bendSt = ctx.get(bendRange);
    const float gain = inst_.def().outputGain;

    int qi = 0;
    for (int i = 0; i < io.frames; ++i) {
      const int pos = blockPos_ + i;
      while (qi < queued_ && queue_[qi].sample <= pos) handle(queue_[qi++]);
      bendSmooth_ += (pitchBend_ - bendSmooth_) * 0.004f;
      pc_.bend = bendSmooth_ * bendSt;
      if (radTick_ == 0) {
        muteSmooth_ += (muteTarget - muteSmooth_) * 0.18f;  // per 16 samples: ~80 ms at 48 kHz
        if (std::fabs(muteTarget - muteSmooth_) < 1e-3f) muteSmooth_ = muteTarget;
        ic_.mute = muteSmooth_;
        rs.mute = muteSmooth_;
        rad_.control(rs);
      }
      radTick_ = (radTick_ + 1) & 15;
      const float force = inst_.process(pc_, ic_) * gain;
      float l = 0.f, r = 0.f;
      if (mode == 3) {
        rad_.process(force * inst_.def().radiationGain, l, r);
      } else if (mode == 0) {
        stereoIr_.processMono(force, l, r);
      } else if (mode == 1) {
        smoothIr_.processMono(force, l, r);
      } else {
        l = r = 0.5f * force;
      }
      io.left[i] += l;
      io.right[i] += r;
    }
    // Drop the events handled in this sub-block.
    if (qi > 0) {
      for (int k = qi; k < queued_; ++k) queue_[k - qi] = queue_[k];
      queued_ -= qi;
    }
    blockPos_ += io.frames;

    // Room.
    const Ramp mix = ctx.ramp(roomMix);
    const int n = std::min(io.frames, 32);
    bool on = false;
    for (int i = 0; i < n; ++i) {
      dryL_[i] = io.left[i];
      dryR_[i] = io.right[i];
      wetL_[i] = wetR_[i] = 0.f;
      on = on || mix.at(i) > 0;
    }
    dsp::Reverb::Settings st{ctx.choice(roomSpace), 1.f, 1.f, 50.f, 8.f, 0.f, 90.f, 11000.f, on};
    if (reverb_.process(st, dryL_, dryR_, wetL_, wetR_, n))
      for (int i = 0; i < n; ++i) {
        io.left[i] += mix.at(i) * 0.01f * kWet * wetL_[i];
        io.right[i] += mix.at(i) * 0.01f * kWet * wetR_[i];
      }
  }

 private:
  static constexpr int kQueue = 512;
  static constexpr int kKeyswitchLow = 24;  // C1
  static constexpr float kWet = 0.12f;
  double sr_ = 48000;
  sm::Instrument inst_;
  sm::PerformerControls pc_;
  sm::InstrumentControls ic_;
  wa::Convolver stereoIr_, smoothIr_;
  sm::Radiation rad_;
  float muteSmooth_ = 0.f;
  int radTick_ = 0;
  dsp::Reverb reverb_;
  MidiEvent queue_[kQueue];
  Transport transport_{};
  int queued_ = 0, blockPos_ = 0;
  float pitchBend_ = 0.f, bendSmooth_ = 0.f, pressureAt_ = 0.f;
  float dryL_[32]{}, dryR_[32]{}, wetL_[32]{}, wetR_[32]{};
};

SK_SYNTH(StringModel)
