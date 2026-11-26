// What makes one instrument of the family: its strings, losses, bridge and
// body, bow, and the few player constants that differ between instruments.
// Each instrument is one of these in model/defs/ (Violin.h, Cello.h).
// [T] marks tuning guesses.
#pragma once

#include <cstdint>

#include "BowedString.h"
#include "Bridge.h"
#include "Onsets.h"
#include "Radiation.h"

namespace sm {

// The left hand: how far it reaches and where it goes (Performer). Positions
// are counted in semitones above the open string; the hand's place is where
// the first finger stops a whole tone above it in first position.
struct HandDef {
  float reachBelow = 1.f;  // semitones below the hand's place (a low first finger)
  float reachAbove = 5.f;  // semitones above it (an extended fourth finger)
  float upFinger = 3.f;    // a shift up lands the note this far above the new hand place
  float downFinger = 2.f;  // a shift down, this far above
  int chordSpan = 7;       // the widest chord across adjacent strings (semitones between its lowest and highest stop)
  int vibRefNote = 69;     // vibrato is wider above this note and narrower below (player doc §3.1)
  // Thumb position (the cello): with the hand's place this far above the
  // open string or higher, the thumb lies across the strings as a finger and
  // the hand reaches thumbBelow under it to thumbReach above; a shift down
  // into it lands thumbDown above the new place (none on the violin).
  float thumbFrom = 99.f;
  float thumbReach = 5.f, thumbBelow = 1.f, thumbDown = 3.f;
};

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
  // Bridge admittance in the bowing direction (KB §5.1-5.4).
  BridgeMode modes[8];
  int modeCount;
  BridgeMode hill;     // bridge hill: shapes the dense band's levels
  float denseFrom, denseTo, denseSpacing;
  uint32_t bodySeed;
  float forceTrim[4];  // per-string force calibration (tools/calibrate_force.py) [T]
  float ffLift[4];     // extra Pressure (x d^2) per string: ff close to the bridge needs more on the low strings [T]
  float ppLift[4];     // extra Pressure (x (1-d)^2) per string [T]
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
  // energy to the body (the two-stage decay) [T].
  float vertT60 = 2.5f;          // x the string's own T60
  float vertGain = 1.f;          // its bridge force into the output, x the horizontal's
  float vertDetuneCents = 1.5f;  // slightly different stiffness/termination: slow beating
  float pluckAngleMin = 0.25f, pluckAngleMax = 0.8f;  // rad from the bowing plane
  // The force law across strings: x (Z0 / zRef)^forceZExp. Schelleng's
  // window has F_max ~ Z0 and F_min ~ Z0^2; on the violin's narrow range of
  // impedances 1.39 fits, on the cello's heavy strings the window's centre
  // grows about as Z0 (string_lab sweep).
  float forceZExp = 1.39f;
  // Notes whose fundamental sits on a body resonance need more force (the
  // body absorbs, F_min rises, KB §5.4): log2 F += 1.5 min(1, bodyForceK Z0 |Y(f0)|).
  float bodyForceK = 25.f;
  // The dynamics law's bow speed at the attack: vbPP e^(vbGrowth d), m/s
  // (the violin's: Demoucron, 0.10 at pp to 0.35 at ff).
  float vbPP = 0.10f, vbGrowth = 1.4f;
  float vbPPScale[4]{1.f, 1.f, 1.f, 1.f};  // per string, x^(1 - d): the soft end's speed
  // Limits on the dynamics law's contact point (Performer::computeTargets),
  // per string: beta >= betaFloorA + betaFloorB v_b (v_b in m/s), and <=
  // betaCeil unless the floor is higher. Off (limitBeta false) for the violin.
  bool limitBeta = false;
  float betaFloorA[4]{}, betaFloorB[4]{}, betaCeil[4]{0.3f, 0.3f, 0.3f, 0.3f};
  // The body mode a wolf eliminator acts on (index into modes; -1: none).
  int wolfMode = -1;
  // Attacks from the air (KB §7.2; cello doc §4.2): starting from rest needs
  // ~4x the force that sustains the note (Galluzzo), so players press harder
  // at first and let the force settle into the window. Extra force at the
  // first contact, x the note's force (0: none), decaying with attackLiftTau
  // (s); varied per note by attackVary (fraction: identical gestures don't
  // give identical onsets, cello doc §4.3).
  float attackLift = 0.f, attackLiftTau = 0.05f, attackVary = 0.f;
  // The attack swell (Performer: a note grows into its level), by dynamic at
  // d = 0.24, 0.35, 0.46, 0.571, 0.681, 0.792 (velocities 20..120): depth
  // (dB at Attack Swell 1) and length (s). The violin's: fitted to the
  // Philharmonia violinist's rise times.
  float swellDb[6]{18.5f, 20.f, 23.f, 12.f, 6.6f, 5.9f};
  float swellT[6]{0.36f, 0.36f, 0.356f, 0.2f, 0.268f, 0.26f};
  // Recorded onsets (Onsets.h): the bow's catch and scratch at a stroke's
  // start, added to the bridge force (none: the model's attack alone).
  // onsetGain scales a clip into bridge force: N per unit of the held note's
  // RMS at Z0 (v_b / beta)^0.886 = 1 (the performer's level law), fitted with
  // tools/attack_sustain.py [T].
  // Per string: the law's exponent (the C string's dynamic range is far
  // narrower than the law's) and a trim (dB).
  const OnsetBank* onsets = nullptr;
  float onsetGain = 0.f;
  float onsetExp[4]{0.886f, 0.886f, 0.886f, 0.886f};
  float onsetTrimDb[4]{};
  // The body's size and radiation patterns, and where the body controls act.
  RadiationShape shape{};
  HandDef hand{};
};

}  // namespace sm
