// instrument_lab: drive sm::Instrument directly with a note list and print
// what the bow does per note (no synthkit). Used to debug the gesture layer.
//   instrument_lab [legato 0/1] [intonation]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../model/Instrument.h"
using namespace sm;
int main(int argc, char** argv) {
  const float sr = 48000;
  Instrument inst; inst.prepare(sr);
  PerformerControls pc; InstrumentControls ic;
  pc.legato = argc > 1 ? std::atoi(argv[1]) : 1;
  if (argc > 2) ic.intonation = static_cast<float>(std::atof(argv[2]));
  int notes[] = {69, 71, 73, 74, 76, 74, 73, 71, 69};
  const float dur = 0.22f, step = 0.25f;
  int ni = 0; bool on = false;
  double e = 0; int cnt = 0;
  for (int i = 0; i < static_cast<int>(sr * 3); ++i) {
    const float t = i / sr;
    const int idx = static_cast<int>(t / step);
    if (idx < 9 && !on && t >= idx * step && idx == ni) {
      inst.performer().noteOn(notes[idx], 0.7f, pc); on = true;
    }
    if (on && t >= ni * step + dur) {
      inst.performer().noteOff(notes[ni], pc); on = false;
      std::printf("note %d str %d  rms %.1f dB  vb %+.3f  beta %.3f  bowpos %.2f\n", notes[ni], inst.performer().currentString(),
                  10 * std::log10(e / cnt + 1e-20), inst.performer().bowVelocity(), inst.performer().beta(), inst.performer().bowPosition());
      e = 0; cnt = 0; ++ni;
    }
    const float y = inst.process(pc, ic);
    if (on && t > ni * step + 0.08f) { e += y * y; ++cnt; }
    if (on && (i % 480) == 0 && ni == 2)
      std::printf("   t %.3f vb %+.3f\n", t, inst.performer().bowVelocity());
  }
}
