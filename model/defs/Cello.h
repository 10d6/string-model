// Cello data (Phase 8). The strings are measured (KB §2.1, D'Addario
// Kaplan Solutions); their losses are fitted to the Iowa MIS pizzicato (8.2);
// the body is from the published modes (KB §5.4) with its main mode set for a
// wolf; the playability assistant is calibrated on these strings
// (tools/playability.py, 8.2). The radiation and the player's habits are
// still the first version (docs/PLAN.md "Phase 8"). [T] marks tuning guesses.
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
            // KB §2.4), HF loss scale (the Iowa pizzicato: the C a little
            // lossier in the treble, the others alike)
            {"C", 36, 1.46f, 4.8f, 0.80f},
            {"G", 43, 1.00f, 3.3f, 1.00f},
            {"D", 50, 0.67f, 2.2f, 1.00f},
            {"A", 57, 0.56f, 1.85f, 1.00f},
        },
        // String damping, rates 1/s: low, high, transition Hz; finger low, high;
        // bridge share. Fitted to every partial's decay in the Iowa MIS
        // pizzicato, pp/mf/ff, all strings (tools/partial_q.py --iowa): the
        // median Q per band within ~15% of the recordings' up to 2 kHz. Far
        // less treble loss than the violin's (σ_H 24/s vs 40/s, rising from
        // 900 Hz): cello partials ring long.
        {1.4f, 24.f, 900.f, 2.9f, 5.f, 0.1f},
        0.60f,  // hair length (a cello bow is shorter and heavier than a violin bow) [T]
        // The force law measured on the violin (Demoucron, KB §11.5), scaled by
        // impedance per string (forceZExp below): a few newtons on the C at mf
        // (KB §7.1: "forces up to several N"), up to ~10 N at ff.
        0.22f,
        0.35f, 0.13f, 0.07f,
        20.0f,  // [T]: ff on the C reaches ~10 N at Pressure +1
        // Bridge force -> output: the cello's strings push ~4x harder for the
        // same speed (Z0 ~4x). Set so tests/cello.json through the Library Mic
        // (Smooth) is as loud (60 Hz-8 kHz) as the violin's
        // tests/legato-scale.json [T].
        0.06f,
        // Signature modes (KB §5.4): A0 ~102 Hz (an air mode: weak at the
        // bridge), the main body resonance ~175 Hz, strong and lightly damped:
        // the wolf appears by itself on F3 of the G and C strings
        // (tools/wolf_check.py). At 0.25 s/kg (8.1) the note doubled its
        // period at every dynamic; 0.13 (m_eff ~0.35 kg at Q 50; Z0|Y| 0.13 on
        // the G string, twice the violin's strongest coupling) gives the
        // classic wolf: no Helmholtz at pp, beating at mf-ff, worse on the C,
        // and gone with the Wolf Eliminator at ~0.5 (G) to 1 (C) [T].
        // the 226 Hz cavity mode, ~320 Hz (a peak in the library curve, KB
        // §12.7), a measured corpus mode at 433 Hz (Q 53, m_eff 180 g -> 0.11
        // s/kg), and the 547 / 742 Hz cavity modes [T: levels other than 433].
        {
            {102.f, 30.f, 0.03f},
            {175.f, 50.f, 0.13f},
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
        {0.6f, 0.6f, 0.6f, 1.0f},  // ff lift: E4 on the A string needed the most (tools/playability.py)
        {0.f, 0.f, 0.f, 0.f},
        // The bow's flattening of the open strings, corrected at p-mf
        // (tools/open_strings.py --instrument cello: within ~4 cents p..ff).
        {17.f, 19.f, 13.f, 10.5f},
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
    // The playability assistant on these strings (tools/playability.py: clean
    // at Pressure 0 with a mean margin of 0.56, the violin 0.62):
    // - force across strings grows as Z0 (string_lab sweep: the window's
    //   centre does), not Z0^1.39 (2x too much on the C);
    // - the body-force term only near real resonances (25 counted almost
    //   every cello note, Z0 |Y| ~0.04 is common on these strings);
    // - the contact point's range compressed toward the middle (pp nearer
    //   the bridge, ff farther than the violin's law), and farther from the
    //   bridge on the heavier strings: Schelleng's window narrows as Z0 / beta
    //   [T: no measured cellists' contact points in the KB].
    c.forceZExp = 1.0f;
    c.bodyForceK = 8.f;
    c.betaPow = 0.6f;
    c.betaZExp = 0.7f;
    c.betaZRef = 0.67f;
    c.wolfMode = 1;  // the Wolf Eliminator acts on the 175 Hz mode
    return c;
  }();
  return d;
}

}  // namespace sm
