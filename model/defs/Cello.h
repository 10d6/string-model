// Cello data: a first version from the KB alone (Phase 8, step 1). The
// strings are measured (KB §2.1, D'Addario Kaplan Solutions); the body is
// from the published modes (KB §5.4); the bow, losses and output levels are
// starting guesses until they are calibrated against the recordings
// (docs/PLAN.md "Phase 8"). [T] marks tuning guesses.
#pragma once

#include "../InstrumentDef.h"
#include "../data/CelloRadiation.h"

namespace sm {

inline const InstrumentDef& cello() {
  static const InstrumentDef d = [] {
    InstrumentDef c{
        "Cello",
        {
            // name, open note, Z0 (Kaplan, KB §2.1), ZR ~ 3.3 Z0 (cello D measured,
            // KB §2.4), HF loss scale [T: ordered as the Kaplan strings' treble
            // loss: the C loses its high partials fastest]
            {"C", 36, 1.46f, 4.8f, 0.40f},
            {"G", 43, 1.00f, 3.3f, 0.55f},
            {"D", 50, 0.67f, 2.2f, 0.70f},
            {"A", 57, 0.56f, 1.85f, 0.85f},
        },
        // String damping, rates 1/s: low, high, transition Hz; finger low, high;
        // bridge share [T]. The Kaplan strings on rigid ends ring far longer
        // (Q 1200-3000 at the 2nd-3rd mode, KB §2.1; about half on the
        // instrument); the violin's fit to recordings is ~10x lossier than its
        // own strings' Q too, so this starts between the two, to be fitted to
        // the Iowa and Philharmonia cello pizzicato (tools/partial_q.py).
        {1.2f, 40.f, 1500.f, 4.f, 12.f, 0.1f},
        0.60f,  // hair length (a cello bow is shorter and heavier than a violin bow) [T]
        // The force law measured on the violin (Demoucron, KB §11.5), scaled by
        // impedance (Z/zRef)^1.39 per string: the C needs ~14x the violin's
        // force, a few newtons at mf (KB §7.1: "forces up to several N").
        0.22f,
        0.35f, 0.13f, 0.07f,
        12.0f,  // [T]
        // Bridge force -> output: the cello's strings push ~4x harder for the
        // same speed (Z0 ~4x). Set so tests/cello.json through the Library Mic
        // (Smooth) is as loud (60 Hz-8 kHz) as the violin's
        // tests/legato-scale.json [T].
        0.06f,
        // Signature modes (KB §5.4): A0 ~102 Hz (an air mode: weak at the
        // bridge), the main body resonance ~175 Hz, strong and lightly damped
        // (the wolf, on E-F of the G and C strings, should appear by itself),
        // the 226 Hz cavity mode, ~320 Hz (a peak in the library curve, KB
        // §12.7), a measured corpus mode at 433 Hz (Q 53, m_eff 180 g -> 0.11
        // s/kg), and the 547 / 742 Hz cavity modes [T: levels other than 433].
        {
            {102.f, 30.f, 0.03f},
            {175.f, 50.f, 0.25f},
            {226.f, 30.f, 0.05f},
            {320.f, 30.f, 0.10f},
            {433.f, 53.f, 0.108f},
            {547.f, 30.f, 0.05f},
            {742.f, 30.f, 0.05f},
            {0.f, 0.f, 0.f},
        },
        7,
        // Bridge hill: lower than the violin's (KB §10: ~1-2 kHz [T]), and a
        // heavier body: lower admittance.
        {1600.f, 3.f, 0.06f},
        800.f, 5000.f, 100.f,  // dense band: the bigger plates' modes are closer together [T]
        0xce110u,
        {1.f, 1.f, 1.f, 1.f},
        {0.6f, 0.6f, 0.3f, 0.3f},
        {0.f, 0.f, 0.f, 0.f},
        {10.f, 10.f, 10.f, 10.f},  // [T] until tools/open_strings.py runs on the cello
        data::kCelloRadiationHz, data::kCelloRadiationDb, data::kCelloRadiationCount,
        80.f,   // below A0 (~100 Hz) the cello radiates little: the open C's fundamental is weak [T]
        1.12f,  // Modelled vs Library Mic (Smooth), 300 Hz-5 kHz: the same +1.5 dB as the violin's [T]
        0.69f,  // KB §2.1
        {1.f, 1.f, 1.f, 0.8f},  // all four wound [T]
        0.15e-3f,               // [T]
    };
    // Radiation (KB §12.1, §12.3a): a monopole below ~300 Hz, a 0.4 m body,
    // the treble beam to the front and a little below horizontal (at 1250 Hz,
    // -11 degrees). Air Resonance at A0; Nasality where the violin's region
    // lands on a body this much bigger (its hill is ~0.6x) [T].
    c.shape.lowestHz = 60.f;
    c.shape.monopoleHz = 300.f;
    c.shape.radius = 0.4f;
    c.shape.beamEl = -0.19f;
    c.shape.calibFromHz = 64.f;
    c.shape.a0Hz = 102.f;
    c.shape.nasalHz = 660.f;
    // The hand (in the neck positions): the four fingers span a minor third
    // (first to fourth finger), a forward extension a major third; a shift
    // lands on the third finger (two above the hand's place) going up, the
    // second going down; chords across adjacent strings span less than on
    // the violin [T]. Vibrato widens above the open A, as the violin's above
    // its open A.
    c.hand.reachBelow = 1.f;
    c.hand.reachAbove = 4.f;
    c.hand.upFinger = 2.f;
    c.hand.downFinger = 1.f;
    c.hand.chordSpan = 5;
    c.hand.vibRefNote = 57;
    return c;
  }();
  return d;
}

}  // namespace sm
