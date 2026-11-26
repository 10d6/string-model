// String Model: a physically modelled bowed string instrument: violin and
// cello behind the Instrument switch (viola and bass planned).
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
#include "ir/CelloIR.h"
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
  Param micAngle, micElevation, micDistance;
  Param attackSwell;
  Param avoidOpen;
  Param bendRange, a4;
  Param wolf;
  Param bowCatch;

  void defineParams(ParamBuilder& b) override {
    instrument = b.add("instrument").name("Instrument").choices({"Violin", "Cello"}).group("Instrument");
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
    sustainSpeed = b.add("bow.sustain").name("Long-Note Bow Speed").range(0.25, 1).def(1).smoothing(0).group("Bow");
    releaseMode = b.add("bow.release").name("Release").choices({"Auto (Key-Up Speed)", "Lift", "Stop on String", "Finger Off"}).def(0).group("Bow");
    noise = b.add("bow.noise").name("Rosin Noise").range(0, 0.5).def(0.12).smoothing(0).group("Bow");
    friction = b.add("bow.friction").name("Friction Model").choices({"Thermal", "Classic", "Enhanced Thermal"}).def(2).group("Bow");
    chordWindow = b.add("chord.window").name("Chord Window").range(0, 0.08).def(0.035).unit("s").smoothing(0).group("Bow");
    rollTime = b.add("chord.roll").name("Chord Roll").range(0.02, 0.2).def(0.06).unit("s").smoothing(0).group("Bow");
    vibDepth = b.add("vib.depth").name("Vibrato Depth").range(0, 60).def(26).unit("ct").smoothing(0).group("Left Hand");
    vibRate = b.add("vib.rate").name("Vibrato Rate").range(3, 9).def(5.5).unit("Hz").smoothing(0).group("Left Hand");
    vibDelay = b.add("vib.delay").name("Vibrato Delay").range(0, 1).def(0.1).unit("s").smoothing(0).group("Left Hand");
    shiftTime = b.add("finger.shift").name("Shift Time").range(0, 0.3).def(0.04).unit("s").smoothing(0).group("Left Hand");
    // Strings lowest to highest: violin G D A E, cello C G D A.
    stringSel = b.add("string.select").name("String").choices({"Auto", "G / C", "D / G", "A / D", "E / A"}).def(0).group("Left Hand");
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
    // Phase 7b: the pair's distance (far field from 0.4 m until the near field, 7c).
    micDistance = b.add("mic.distance").name("Mic Distance").range(0.05, 10).log().def(1.5).unit("m").group("Output");
    // A player habit (the Philharmonia recordings): bowed notes grow into their level.
    attackSwell = b.add("bow.swell").name("Attack Swell").range(0, 1).def(0.75).smoothing(0).group("Bow");
    // A player habit: notes that would be open strings are stopped on the
    // string below, so they can take vibrato.
    avoidOpen = b.add("finger.avoidOpen").name("Avoid Open Strings").choices({"Off", "On"}).def(1).group("Left Hand");
    // Phase 8.2: a wolf eliminator on the cello's main body mode (KB §5.4).
    wolf = b.add("body.wolf").name("Wolf Eliminator").range(0, 1).def(0).smoothing(0).group("Instrument");
    // The recorded onsets (cello): a real bow's catch and scratch at each stroke.
    bowCatch = b.add("bow.catch").name("Bow Catch").range(0, 2).def(1).smoothing(0).group("Bow");
  }

  std::unique_ptr<Voice> makeVoice() override { return std::make_unique<SilentVoice>(); }

  void prepare(const PrepareInfo& info) override {
    sr_ = info.sampleRate;
    bendRange = info.param("bendRange");
    a4 = info.param("sk.a4");
    // Every instrument is prepared here, so the Instrument switch needs no
    // allocation: it fades one out and the other in (processGlobal).
    for (int k = 0; k < sm::kInstrumentCount; ++k) {
      Body& b = body_[k];
      const sm::InstrumentDef& d = sm::instrumentDef(k);
      b.inst.prepare(sr_, d);
      b.rad.prepare(static_cast<float>(sr_), d.radiationHz, d.radiationDb, d.radiationCount, d.bodySeed ^ 0x9e3779b9u, d.hill.hz,
                    d.radiationLowCut, d.shape);
      // The CC0 library-mic responses (bridge force -> recording): the solo
      // violin, the cello section (KB §12.7).
      const LibraryIr& ir = kLibraryIr[k];
      auto l = resampleIr(ir.left, ir.leftLen, ir.rate, sr_);
      auto r = resampleIr(ir.right, ir.rightLen, ir.rate, sr_);
      auto m = resampleIr(ir.smooth, ir.smoothLen, ir.rate, sr_);
      // The IRs are peak-normalised; scale so their sum keeps a level that is
      // independent of the sample rate.
      const float rateGain = static_cast<float>(48000.0 / sr_);
      for (auto* v : {&l, &r, &m})
        for (float& x : *v) x *= rateGain;
      const std::size_t n = std::max(l.size(), r.size());
      l.resize(n, 0.f);
      r.resize(n, 0.f);
      const float* st[2] = {l.data(), r.data()};
      b.stereoIr.setImpulse(st, 2, static_cast<int>(n), sr_, false);
      const float* mo[1] = {m.data()};
      b.smoothIr.setImpulse(mo, 1, static_cast<int>(m.size()), sr_, false);
    }
    cur_ = 0;
    old_ = -1;
    fade_ = 0;
    fadeLen_ = std::max(1, static_cast<int>(kSwitchFade * sr_));
    tailLen_ = static_cast<int>(kSwitchTail * sr_);
    muteSmooth_ = 0.f;
    radTick_ = 0;
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
    auto& p = body_[cur_].inst.performer();
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
    // The Instrument switch: held notes stop; the old instrument's sound fades
    // over kSwitchFade and its body (library mic or radiation) rings out
    // over kSwitchTail, then it is reset, ready for next time. A switch
    // during a fade waits for it.
    const int want = std::clamp(ctx.choice(instrument), 0, sm::kInstrumentCount - 1);
    if (want != cur_ && old_ < 0) {
      old_ = cur_;
      body_[old_].inst.performer().allNotesOff();
      cur_ = want;
      fade_ = 0;
      trim_ = 0.f;
    }
    pc_.dynamics = ctx.get(dynamics);
    pc_.velSens = ctx.get(velSens);
    pc_.pressure = std::clamp(ctx.get(pressure) + ctx.get(atPressure) * pressureAt_, -1.f, 1.f);
    pc_.contact = ctx.get(contact);
    pc_.bite = ctx.get(bite);
    pc_.swell = ctx.get(attackSwell);
    pc_.avoidOpen = ctx.choice(avoidOpen) != 0;
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
    ic_.wolf = ctx.get(wolf);
    ic_.bowCatch = ctx.get(bowCatch);
    // Bridge Hill is the violin's hill frequency; other instruments scale
    // it to their own (the cello's ~1.6 kHz at the default).
    const float hillScale = body_[cur_].inst.def().hill.hz / 2600.f;
    ic_.hillHz = ctx.get(hill) * hillScale;
    // The mute goes on over ~80 ms (it's put on by hand), on the samples'
    // own clock below.
    const float muteTarget = ctx.on(mute) ? 1.f : 0.f;
    sm::RadiationSettings rs;
    rs.hillHz = ctx.get(hill) * hillScale;
    rs.resonance = ctx.get(resonance);
    rs.width = ctx.get(width);
    rs.micAz = ctx.get(micAngle);
    rs.micEl = ctx.get(micElevation);
    rs.micDist = ctx.get(micDistance);
    rs.a0 = ctx.get(a0);
    rs.nasal = ctx.get(nasal);
    rs.brilliance = ctx.get(brilliance);
    rs.softness = ctx.get(softness);
    rs.voice = 0.01f * ctx.get(voice);
    const int mode = ctx.choice(output);
    const float bendSt = ctx.get(bendRange);
    Body& cb = body_[cur_];
    const float gain = cb.inst.def().outputGain;
    Body* ob = old_ >= 0 ? &body_[old_] : nullptr;

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
        cb.rad.control(rs);
        if (ob) ob->rad.control(rs);
      }
      radTick_ = (radTick_ + 1) & 15;
      const float force = cb.inst.process(pc_, ic_) * gain;
      float l = 0.f, r = 0.f;
      play(cb, mode, force, l, r, feedL_[i & 31], feedR_[i & 31]);
      if (ob) {
        // The old instrument: its force fades (a raised cosine), then its
        // body rings out with no input.
        float of = 0.f;
        if (fade_ < fadeLen_) {
          const float g = 0.5f + 0.5f * std::cos(3.14159265f * static_cast<float>(fade_) / static_cast<float>(fadeLen_));
          of = ob->inst.process(pc_, ic_) * ob->inst.def().outputGain * g;
        }
        float ol = 0.f, orr = 0.f, fa = 0.f, fb = 0.f;
        play(*ob, mode, of, ol, orr, fa, fb);
        l += ol;
        r += orr;
        feedL_[i & 31] += fa;
        feedR_[i & 31] += fb;
        if (++fade_ >= fadeLen_ + tailLen_) {
          ob->inst.reset();
          ob->rad.reset();
          old_ = -1;
          ob = nullptr;
        }
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

    // Room. The Modelled output feeds it the violin's power response (the
    // radiation's room channels, KB §12.3e: what the room hears is the sound
    // radiated every way, smoother and darker than the mic's view); the other
    // outputs feed it what they play.
    const Ramp mix = ctx.ramp(roomMix);
    const int n = std::min(io.frames, 32);
    const int space = ctx.choice(roomSpace);
    bool on = false;
    for (int i = 0; i < n; ++i) {
      dryL_[i] = mode == 3 ? feedL_[i] : io.left[i];
      dryR_[i] = mode == 3 ? feedR_[i] : io.right[i];
      wetL_[i] = wetR_[i] = 0.f;
      on = on || mix.at(i) > 0;
    }
    dsp::Reverb::Settings st{space, 1.f, 1.f, 50.f, 8.f, 0.f, 90.f, 11000.f, on};
    if (reverb_.process(st, dryL_, dryR_, wetL_, wetR_, n))
      for (int i = 0; i < n; ++i) {
        io.left[i] += mix.at(i) * 0.01f * kWet * wetL_[i];
        io.right[i] += mix.at(i) * 0.01f * kWet * wetR_[i];
      }
    // Distance (Modelled): the direct sound fell as 1/r in the radiation, the
    // room didn't. Re-gain the total as an engineer would, so the loudness
    // stays about the same and only the balance moves: power (d0^2 + W m^2)
    // / (d^2 + W m^2), d = the direct gain re the reference distance, m the
    // mix, W the space's wet/direct power at 100% (measured, kRoomPower).
    if (mode == 3) {
      const float d = cb.rad.directGain();
      const float w = kRoomPower[cur_][std::clamp(space, 0, 4)];
      const float wm0 = w * 0.0001f * mix.at(0) * mix.at(0);
      const float wm1 = w * 0.0001f * mix.at(n - 1) * mix.at(n - 1);
      const float t0 = std::sqrt((1.f + wm0) / (d * d + wm0)), t1 = std::sqrt((1.f + wm1) / (d * d + wm1));
      const float t = trim_ > 0.f ? trim_ : t0;
      for (int i = 0; i < n; ++i) {
        const float g = t + (t1 - t) * static_cast<float>(i + 1) / static_cast<float>(n);
        io.left[i] *= g;
        io.right[i] *= g;
      }
      trim_ = t1;
    } else {
      trim_ = 0.f;
    }
  }

 private:
  // One instrument with its own body outputs.
  struct Body {
    sm::Instrument inst;
    sm::Radiation rad;
    wa::Convolver stereoIr, smoothIr;
  };
  struct LibraryIr {
    const float *left, *right, *smooth;
    int leftLen, rightLen, smoothLen;
    double rate;
  };
  static constexpr LibraryIr kLibraryIr[sm::kInstrumentCount] = {
      {sm::ir::kViolinLeft, sm::ir::kViolinRight, sm::ir::kViolinMonoSmooth, sm::ir::kViolinLeftLen, sm::ir::kViolinRightLen,
       sm::ir::kViolinMonoSmoothLen, sm::ir::kViolinRate},
      {sm::ir::kCelloLeft, sm::ir::kCelloRight, sm::ir::kCelloMonoSmooth, sm::ir::kCelloLeftLen, sm::ir::kCelloRightLen,
       sm::ir::kCelloMonoSmoothLen, sm::ir::kCelloRate},
  };

  // One sample of bridge force through the chosen output.
  static void play(Body& b, int mode, float force, float& l, float& r, float& roomA, float& roomB) {
    if (mode == 3) {
      b.rad.process(force * b.inst.def().radiationGain, l, r, roomA, roomB);
    } else if (mode == 0) {
      b.stereoIr.processMono(force, l, r);
    } else if (mode == 1) {
      b.smoothIr.processMono(force, l, r);
    } else {
      l = r = 0.5f * force;
    }
  }

  static constexpr int kQueue = 512;
  static constexpr float kSwitchFade = 0.03f;  // s: the old instrument fades out
  static constexpr float kSwitchTail = 0.25f;  // s: then its body rings out (the longest library IR is ~0.17 s)
  static constexpr int kKeyswitchLow = 24;  // C1
  static constexpr float kWet = 0.12f;
  // Each space's wet/direct power ratio at Room 100% (Modelled output, the
  // reference distance; tools/room_power.py).
  static constexpr float kRoomPower[sm::kInstrumentCount][5] = {
      {0.326f, 0.160f, 0.200f, 0.192f, 0.200f},  // violin
      {0.191f, 0.200f, 0.226f, 0.171f, 0.160f},  // cello (tools/room_power.py --instrument cello)
  };
  double sr_ = 48000;
  Body body_[sm::kInstrumentCount];
  int cur_ = 0, old_ = -1;            // the instrument playing; the one fading out (-1: none)
  int fade_ = 0, fadeLen_ = 1440, tailLen_ = 12000;  // samples into the switch; its fade and tail
  sm::PerformerControls pc_;
  sm::InstrumentControls ic_;
  float muteSmooth_ = 0.f;
  int radTick_ = 0;
  dsp::Reverb reverb_;
  MidiEvent queue_[kQueue];
  Transport transport_{};
  int queued_ = 0, blockPos_ = 0;
  float pitchBend_ = 0.f, bendSmooth_ = 0.f, pressureAt_ = 0.f;
  float dryL_[32]{}, dryR_[32]{}, wetL_[32]{}, wetR_[32]{};
  float feedL_[32]{}, feedR_[32]{};  // the radiation's room feed (Modelled)
  float trim_ = 0.f;                 // the distance trim at the end of the last block
};

SK_SYNTH(StringModel)
