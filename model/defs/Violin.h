// Violin data. Sources: instrument-research KB §2.1 (Guettler's string
// table), §2.3 (damping), §3.6 (torsion), §11.5 (Demoucron's measured
// dynamics). [T] marks tuning guesses.
#pragma once

#include "../BowedString.h"

namespace sm {

struct InstrumentDef {
  const char* name;
  StringDef strings[4];
  LossDef loss;
  float bowLength;     // m, usable hair length
  float zRef;          // impedance the force law below was measured on (kg/s)
  float forceRef;      // N at vbRef and betaRef (players' line, KB §11.5)
  float vbRef, betaRef;
  float forceMax;      // N, clamp for the assistant
  float outputGain;    // bridge force (N) -> output units
};

inline const InstrumentDef& violin() {
  // Z0 = mu*c from mid-range values of KB §2.1; ZR ~ 3.5 Z0 [T]
  // (cello D measured ZR/ZT ~ 3.3, KB §2.4).
  static const InstrumentDef d{
      "Violin",
      {
          {"G", 55, 0.33f, 1.15f},
          {"D", 62, 0.24f, 0.85f},
          {"A", 69, 0.20f, 0.70f},
          {"E", 76, 0.19f, 0.65f},
      },
      // open T60, stopped T60, bridge LP, nut LP, finger LP  [T]
      {2.2f, 0.9f, 9000.f, 12000.f, 5000.f},
      0.62f,
      0.22f,
      // Demoucron at 13 cm/s: mf beta 0.07, 0.47 N (pp 0.16/0.17 N, ff 0.04/1.15 N)
      0.35f, 0.13f, 0.07f,  // forceRef trimmed from 0.47 to sit mid-window in our model [T]
      3.0f,
      0.28f,
  };
  return d;
}

}  // namespace sm
