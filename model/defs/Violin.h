// Violin data. Sources: instrument-research KB §2.1 (Guettler's string
// table), §2.3 (damping), §3.6 (torsion), §11.5 (Demoucron's measured
// dynamics). [T] marks tuning guesses.
#pragma once

#include "../BowedString.h"
#include "../Bridge.h"
#include "../data/ViolinRadiation.h"

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
  // Bridge admittance in the bowing direction (KB §5.1-5.3).
  BridgeMode modes[8];
  int modeCount;
  BridgeMode hill;     // bridge hill: shapes the dense band's levels
  float denseFrom, denseTo, denseSpacing;
  uint32_t bodySeed;
  float forceTrim[4];  // per-string force calibration (tools/calibrate_force.py) [T]
  // The bow flattens an open string, and no finger can correct it: the
  // performer raises a bowed open string by this much, only while the bow is
  // on it (unbowed open strings stay at true pitch to ring in sympathy) [T].
  float openTuneCents[4];
  // Radiation (Radiation.h): the calibration target and its measured hill,
  // the low cut below the lowest air mode, and a gain matching the
  // library-mic output's loudness.
  const float* radiationHz;
  const float* radiationDb;
  int radiationCount;
  float radiationLowCut;
  float radiationGain;
  float scaleLength;      // m, nut to bridge (for the finger's speed along the string)
  float winding[4];       // 0..1 per string: how much a sliding finger rubs a winding (0 = plain)
  float windingPitch;     // m between turns of the winding
  // Pizzicato's second (vertical) polarisation, KB §4/§11.2: weakly coupled
  // to the bridge, so it rings on after the horizontal one has given its
  // energy to the body (the two-stage decay). Fitted to VSCO-2-CE solo
  // violin pizzicato (docs/MODEL.md) [T].
  float vertT60 = 2.5f;          // x the string's own T60
  float vertGain = 1.f;          // its bridge force into the output, x the horizontal's
  float vertDetuneCents = 1.5f;  // slightly different stiffness/termination: slow beating
  float pluckAngleMin = 0.25f, pluckAngleMax = 0.8f;  // rad from the bowing plane
};

inline const InstrumentDef& violin() {
  // Z0 = mu*c from mid-range values of KB §2.1; ZR ~ 3.5 Z0 [T]
  // (cello D measured ZR/ZT ~ 3.3, KB §2.4).
  static const InstrumentDef d{
      "Violin",
      {
          // name, open note, Z0, ZR, HF loss scale [T]
          {"G", 55, 0.33f, 1.15f, 0.55f},
          {"D", 62, 0.24f, 0.85f, 0.70f},
          {"A", 69, 0.20f, 0.70f, 0.85f},
          {"E", 76, 0.19f, 0.65f, 1.00f},
      },
      // open T60, stopped T60, bridge LP, nut LP, finger LP  [T]
      {2.2f, 0.9f, 9000.f, 12000.f, 5000.f},
      0.62f,
      0.22f,
      // Demoucron at 13 cm/s: mf beta 0.07, 0.47 N (pp 0.16/0.17 N, ff 0.04/1.15 N)
      0.35f, 0.13f, 0.07f,  // forceRef trimmed from 0.47 to sit mid-window in our model [T]
      3.0f,
      0.28f,
      // Signature modes, KB §5.2 / §10 (frequencies [V] ranges, Q and peak
      // admittance [T]: Q 20-60, peaks 0.05-0.5 s/kg, KB §5.2): A0 air mode
      // (weak at the bridge), CBR (weak), B1-, B1+ (strong radiators), and
      // the ~1 kHz cluster. Levels set from coupled tests (docs/MODEL.md):
      // higher made the body absorb too much (partials' Q ~40) and made
      // notes matching open-string harmonics wolf-like at pp.
      {
          {280.f, 40.f, 0.024f},
          {400.f, 30.f, 0.030f},
          {460.f, 40.f, 0.150f},
          {550.f, 40.f, 0.180f},
          {660.f, 30.f, 0.060f},
          {820.f, 30.f, 0.072f},
          {1000.f, 25.f, 0.120f},
          {1150.f, 25.f, 0.090f},
      },
      8,
      {2600.f, 3.f, 0.15f},  // bridge hill ~2.5 kHz, low Q (KB §5.3) [T level]
      1300.f, 7000.f, 150.f, // dense band, mean spacing [T]
      0x5717a1u,
      // The steel E needed ~+0.2 pressure (x1.22) to sit mid-window.
      {1.f, 1.f, 1.f, 1.22f},
      // Full compensation of the flattening under the bow (enhanced friction,
      // two-point bow), averaged over dynamics 0.15-0.85 (tools/open_strings.py).
      {15.5f, 11.5f, 14.f, 16.f},
      data::kViolinRadiationHz, data::kViolinRadiationDb, data::kViolinRadiationCount,
      230.f,  // below A0 (~280 Hz) the violin radiates little; weak at the open G (KB §12.7)
      1.19f,  // same loudness (300 Hz-5 kHz) as the Library Mic (Smooth) output
      0.328f,  // KB §2.1
      // G, D and A are wound (silver/aluminium on synthetic cores); the E is
      // plain steel [T].
      {1.f, 1.f, 0.7f, 0.f},
      0.12e-3f,  // [T]
  };
  return d;
}

}  // namespace sm
