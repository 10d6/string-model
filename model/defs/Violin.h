// Violin data. Sources: instrument-research KB §2.1 (Guettler's string
// table), §2.3 (damping), §3.6 (torsion), §11.5 (Demoucron's measured
// dynamics). [T] marks tuning guesses.
#pragma once

#include "../InstrumentDef.h"
#include "../data/ViolinRadiation.h"

namespace sm {

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
      // String damping, rates 1/s (tools/partial_q.py on the Philharmonia
      // pizzicato) [T]: low, high, transition Hz; finger low, high; bridge share
      {2.6f, 40.f, 2000.f, 7.5f, 18.f, 0.1f},
      0.62f,
      0.22f,
      // Demoucron at 13 cm/s: mf beta 0.07, 0.47 N (pp 0.16/0.17 N, ff 0.04/1.15 N)
      0.35f, 0.13f, 0.07f,  // forceRef trimmed from 0.47 to sit mid-window in our model [T]
      5.0f,  // the players' line asks ~4-5 N on the G at ff [T]
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
      // With the frequency-dependent losses, ff near the bridge sits at the
      // bottom of the window on G, D and A, most on the G (the E is already
      // near the top).
      {0.6f, 0.3f, 0.3f, 0.f},
      // ...and pp on the G a little lighter (tools/calibrate_force.py).
      {-0.5f, 0.f, 0.f, 0.f},
      // Full compensation of the flattening under the bow (enhanced friction,
      // two-point bow) at p-mf; the performer scales it down above mf
      // (tools/open_strings.py).
      {15.5f, 10.3f, 11.f, 9.2f},
      data::kViolinRadiationHz, data::kViolinRadiationDb, data::kViolinRadiationCount,
      245.f,  // below A0 (~280 Hz) the violin radiates little; weak at the open G (KB §12.7)
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
