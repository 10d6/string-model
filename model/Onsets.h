// Recorded onsets: the catch and scratch of a real bow, added to the bridge
// force at the start of a stroke (docs/MODEL.md "Recorded onsets").
//
// The bowed string gives the note; what it lacks at the attack (measured,
// docs/EVALUATION.md "Attacks and sustains by register") is the full-band grit
// of the bow catching the string: the model's treble grows with its swelling
// force, the recordings' is there in the first ~20 ms. Each clip is a real
// cello's onset with its harmonics removed (tools/make_onsets.py), equalised
// into bridge force re its held note's harmonic RMS, and goes through the same
// body, mics and room as the note. From the air it is scaled by the synth's
// own bridge-force envelope over the recording's tone envelope (kEnv), so the
// scratch keeps the recording's noise-to-tone ratio while the synth's note
// builds up (its swell starts ~20 dB under the note; bigcat's attacks near
// it): otherwise the scratch would be heard before the note. A cap (the
// note's level by the performer's law) bounds it; bow changes on the string
// use the cap alone.
#pragma once
#include <cmath>
#include <cstdint>

namespace sm {

// A set of clips (model/data/*Onsets.h), by note, dynamic layer and bow.
struct OnsetBank {
  const int8_t* data = nullptr;  // mu-law, kLen per clip
  const float* scale = nullptr;  // each clip's peak (re the held note's RMS)
  const float* hz = nullptr;     // each clip's recorded pitch
  const int* midi = nullptr;     // the measured notes
  const int* dynVel = nullptr;   // the velocity (0-127) each layer stands for
  const float* env = nullptr;    // each clip's tone envelope re its held RMS, envLen steps
  int notes = 0, dyns = 0, bows = 0, len = 0, pre = 0, envLen = 0;
  float rate = 32000.f, envRate = 400.f;
};

class OnsetPlayer {
 public:
  void prepare(float hostRate) {
    host_ = hostRate;
    for (int i = 0; i < 256; ++i) {  // mu-law (255) decode table
      const float q = static_cast<float>(static_cast<int8_t>(static_cast<uint8_t>(i))) / 127.f;
      lut_[i] = (q < 0.f ? -1.f : 1.f) * (std::pow(256.f, std::fabs(q)) - 1.f) / 255.f;
    }
    reset();
  }
  void reset() {
    for (auto& v : v_) v.on = false;
  }

  // Start a clip: the nearest measured note (transposed to `hz`), the layer
  // nearest `vel` (0..1), down- or up-bow. `cap`: N of bridge force per unit
  // of the held note's RMS, at most; `follow`: scale by the synth's envelope
  // over the recording's (process), else by the cap alone. `fadeEnd` (s)
  // shortens it (short strokes), fading out over its last 40%.
  // `amount` scales the result (Bow Catch).
  void trigger(const OnsetBank& b, int midi, float hz, float vel, bool down, float cap, bool follow, float amount,
               float fadeEnd) {
    const float gain = cap;
    if (!b.data || gain <= 0.f || amount <= 0.f) return;
    int n = 0;
    for (int i = 1; i < b.notes; ++i)
      if (std::abs(b.midi[i] - midi) < std::abs(b.midi[n] - midi)) n = i;
    int d = 0;
    const float v = vel * 127.f;
    for (int i = 1; i < b.dyns; ++i)
      if (std::fabs(b.dynVel[i] - v) < std::fabs(b.dynVel[d] - v)) d = i;
    const int clip = (n * b.dyns + d) * b.bows + (down ? 0 : 1);
    // A free voice, else the older one.
    Voice* w = &v_[0];
    if (v_[0].on) w = !v_[1].on || v_[1].age > v_[0].age ? &v_[1] : &v_[0];
    w->on = true;
    w->age = 0;
    w->src = b.data + static_cast<long>(clip) * b.len;
    w->len = b.len;
    w->pos = 0.0;
    w->step = static_cast<double>(hz / b.hz[clip]) * b.rate / host_;
    w->gain = gain;
    w->scale = b.scale[clip] * amount;
    w->env = follow && b.env ? b.env + static_cast<long>(clip) * b.envLen : nullptr;
    w->envLen = b.envLen;
    w->envStep = b.envRate / b.rate;
    const float end = fadeEnd > 0.f ? std::fmin(fadeEnd * b.rate, static_cast<float>(b.len)) : static_cast<float>(b.len);
    w->fadeTo = end;
    w->fadeFrom = fadeEnd > 0.f ? 0.6f * end : end;
  }

  // `level`: the synth's bridge-force envelope now (N RMS, before the onsets).
  float process(float level) {
    float y = 0.f;
    for (auto& v : v_) {
      if (!v.on) continue;
      const int i = static_cast<int>(v.pos);
      if (i + 1 >= v.len || v.pos >= v.fadeTo) {
        v.on = false;
        continue;
      }
      const float f = static_cast<float>(v.pos - i);
      float s = lut_[static_cast<uint8_t>(v.src[i])] * (1.f - f) + lut_[static_cast<uint8_t>(v.src[i + 1])] * f;
      if (v.pos > v.fadeFrom) {
        const float u = static_cast<float>((v.pos - v.fadeFrom) / (v.fadeTo - v.fadeFrom));
        s *= 0.5f * (1.f + std::cos(3.14159265f * u));
      }
      float g = v.gain;
      if (v.env) {
        const float e = static_cast<float>(v.pos) * v.envStep;
        const int k = static_cast<int>(e);
        const float a = k + 1 < v.envLen ? v.env[k] + (v.env[k + 1] - v.env[k]) * (e - k) : v.env[v.envLen - 1];
        g = std::fmin(level / std::fmax(a, 1e-3f), v.gain);
      }
      y += g * v.scale * s;
      v.pos += v.step;
      ++v.age;
    }
    return y;
  }

 private:
  struct Voice {
    bool on = false;
    const int8_t* src = nullptr;
    const float* env = nullptr;
    int len = 0, envLen = 0;
    float scale = 1.f, envStep = 0.f;
    long age = 0;
    double pos = 0.0, step = 1.0;
    float gain = 0.f, fadeFrom = 0.f, fadeTo = 0.f;
  };
  Voice v_[2];
  float lut_[256]{};
  float host_ = 48000.f;
};

}  // namespace sm
