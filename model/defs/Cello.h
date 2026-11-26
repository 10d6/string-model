// Cello data (Phase 8). The strings are measured (KB §2.1, D'Addario
// Kaplan Solutions); their losses are fitted to the Iowa MIS pizzicato (8.2);
// the body is the measured cello modes (cello doc §3, 8.3) with per-string
// strengths for the wolf mode and the 209 Hz mode; the playability assistant is calibrated on these strings
// (tools/calibrate_force.py, stick test, 8.2). Friction is the violin's
// rosin: the measured cello rosin (mu_s 0.65) put this model's window far
// above Galluzzo's (MODEL.md). The radiation and the player's habits are
// still the first version (docs/PLAN.md "Phase 8"). [T] marks tuning guesses.
#pragma once

#include "../InstrumentDef.h"
#include "../data/CelloOnsets.h"
#include "../data/CelloRadiation.h"

namespace sm {

// The Karoryfer x bigcat cello's onsets (CC0; tools/make_onsets.py).
inline const OnsetBank& celloOnsets() {
  static const OnsetBank b = [] {
    OnsetBank o;
    o.data = onsets::kData;
    o.scale = onsets::kScale;
    o.hz = onsets::kHz;
    o.midi = onsets::kMidi;
    o.dynVel = onsets::kDynVel;
    o.env = onsets::kEnv;
    o.envLen = onsets::kEnvLen;
    o.envRate = static_cast<float>(onsets::kEnvRate);
    o.notes = onsets::kNotes;
    o.dyns = onsets::kDyns;
    o.bows = onsets::kBows;
    o.len = onsets::kLen;
    o.pre = onsets::kPre;
    o.rate = static_cast<float>(onsets::kRate);
    return o;
  }();
  return b;
}

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
        // Signature modes, measured (cello doc §3.1, §8): A0 ~100 Hz (97-104
        // measured; an air mode, weak at the bridge), T1 ~140 Hz (the top
        // pivoting on the soundpost, weak), the main body mode at 185 Hz
        // (165-196 measured; Q 23-71) that makes the wolf, ~209 Hz and ~281
        // Hz, and a corpus mode measured at 433 Hz (Q 53, m_eff 180 g).
        // Per-string strengths (Zhang et al.): the wolf mode is strongest at
        // the C string's notch and falls toward the A, the 209 Hz mode the
        // reverse, 281 Hz the same on all four [T: the weights; the data
        // gives the order, not the ratios]. The wolf mode: Q 50, 0.17 s/kg at
        // the C (m_eff 0.25 kg, in the measured 0.12-0.36 kg, bridge-
        // referred), 0.14 at the G: F#3 wolfs on the G at mf and on the C at
        // every dynamic, beating at 5-8 Hz (tools/wolf_check.py: five of the
        // cello doc's six acceptance tests pass).
        {
            {100.f, 40.f, 0.03f},
            {140.f, 40.f, 0.02f},
            {185.f, 50.f, 0.17f, {1.f, 0.9f, 0.75f, 0.6f}},
            {209.f, 40.f, 0.06f, {0.6f, 0.75f, 0.9f, 1.f}},
            {281.f, 40.f, 0.07f},
            {433.f, 53.f, 0.108f},
            {0.f, 0.f, 0.f},
            {0.f, 0.f, 0.f},
        },
        6,
        // Bridge hill: the cello bridge's first in-situ resonance, 1.2-1.5 kHz
        // on three cellos (cello doc §3.2); a heavier body: lower admittance.
        {1350.f, 3.f, 0.06f},
        // Dense band from 800 Hz, every ~100 Hz [T]. The cello doc's guess of
        // modal overlap from ~400 Hz (§8, [D]: the violin's ~1.3 kHz scaled by
        // the mode ratio) made the open strings' partials at 500 Hz-1 kHz decay
        // twice as fast as the Iowa pizzicato's (Q 160-190 vs 296,
        // tools/partial_q.py); from 800 Hz they are within ~25%.
        800.f, 5000.f, 100.f,
        0xce110u,
        {0.85f, 1.f, 1.f, 1.f},  // force trim: the C sat a little high in its window (calibrate_force.py)
        {0.4f, 0.6f, 0.6f, 1.0f},  // ff lift (calibrate_force.py): E4 on the A string needs the most, the open C the least
        {0.f, 0.1f, 0.f, 0.f},  // pp lift: a little more on the G (calibrate_force.py)
        // The bow's flattening of the open strings, corrected at p-mf
        // (tools/open_strings.py --instrument cello: within ~4 cents p..ff).
        {13.f, 16.f, 12.f, 11.f},
        data::kCelloRadiationHz, data::kCelloRadiationDb, data::kCelloRadiationCount,
        80.f,   // below A0 (~100 Hz) the cello radiates little: the open C's fundamental is weak [T]
        1.21f,  // Modelled vs Library Mic (Smooth), 300 Hz-5 kHz: the same +1.5 dB as the violin's [T]
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
    c.shape.beamFromHz = 300.f;  // already to the front at 500 Hz (KB §12.1)
    c.shape.calibFromHz = 64.f;
    c.shape.a0Hz = 100.f;
    c.shape.nasalHz = 660.f;
    // The cellist sits behind the cello (KB §12.1, §12.3d): the torso shades
    // the rear half, a shelf centred at 350 Hz (within 6 dB everywhere below
    // ~300 Hz; front to back over 10 dB near 400-500 Hz, Meyer); the head is
    // behind and above the body, a little to the left of the neck [T: the
    // angles and levels].
    {
      const float head[3] = {-0.65f, 0.15f, 0.74f}, torso[3] = {-0.96f, 0.f, 0.28f};
      for (int i = 0; i < 3; ++i) c.shape.head[i] = head[i], c.shape.torso[i] = torso[i], c.shape.back[i] = torso[i];
    }
    c.shape.headCos = 0.77f;
    c.shape.torsoDb = 0.f;  // the shelf below is the torso's shade, the treble's too
    c.shape.backDb = 16.f;
    c.shape.backHz = 350.f;
    c.shape.backCos = 0.f;
    // The cello's fine pattern changes less from note to note than the
    // violin's (max directivity across tones 1.2-3.2 dB vs 2.2-8.1, KB §12.1):
    // part of each mode's power follows the smooth envelope. 0.4 (spread
    // ~3.5-5 dB, radiation_lab directivity): more narrows the treble's
    // stereo (L/R correlation 0.7-6 kHz 0.49 here, 0.76 at 0.6).
    c.shape.smoothShare = 0.4f;
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
    // Thumb position from the octave up (the thumb across two strings at the
    // octave harmonic): thumb to third finger a fourth [T].
    c.hand.thumbFrom = 11.f;
    c.hand.thumbReach = 5.f;
    // The playability assistant on these strings (tools/calibrate_force.py
    // --instrument cello, Helmholtz by the stick test: clean at Pressure 0 on
    // every note and dynamic, mean margin 0.62 as the violin's):
    // - force across strings grows as Z0 (string_lab sweep: the window's
    //   centre does; Beigi & Conneely's F_min is ~Z^1 too, KB §7.1), not
    //   Z0^1.39 (2x too much on the C);
    // - the body-force term only near real resonances, and gently (25 counted
    //   almost every cello note, Z0 |Y| ~0.04 is common on these strings; 8
    //   pressed 2.8x harder on the wolf note and hid the G string's wolf);
    // - the contact point between per-string limits from the coupled
    //   instrument's windows (tools/window_map.py): a floor that rises with
    //   bow speed (a fast bow near the bridge can't hold Helmholtz motion on
    //   a heavy string at any force, F_min ~ Z0^2 v_b / beta^2), and a
    //   ceiling where the windows break up (beta >~ 0.15; the C's from
    //   0.09). The D and A floors also keep ff off the bridge, so pp -> ff
    //   spans about what the Iowa recordings do (~10 dB on those strings).
    //   Cellists in repertoire average beta 0.15-0.22 (cello doc §6, Chudy);
    //   this model's windows break up there, so the law stays nearer the
    //   bridge.
    c.forceZExp = 1.0f;
    c.bodyForceK = 4.f;
    // The bow's speed: 5 cm/s at pp (Galluzzo's measuring speed), ~0.33 m/s
    // at ff (cellists average 13-35 cm/s in repertoire, cello doc §6).
    c.vbPP = 0.05f;
    c.vbGrowth = 2.1f;
    // ...except on the C and G, whose windows at 5 cm/s are one Pressure step
    // wide (tools/window_map.py --vb 0.05): pp there at the violin's 0.10 m/s.
    // The C's pp then sits at its contact ceiling, which costs its dynamic
    // range: pp -> ff 4 dB, against 13 in the Iowa recordings (the G 10 vs
    // 13, the D 11 vs 10, the A 11 vs 9). Its narrow windows are the
    // model's limit (MODEL.md "The cello").
    c.vbPPScale[0] = c.vbPPScale[1] = 2.f;
    c.limitBeta = true;
    const float floorA[4] = {0.04f, 0.05f, 0.05f, 0.065f}, floorB[4] = {0.2f, 0.1f, 0.12f, 0.1f};
    const float ceil[4] = {0.09f, 0.13f, 0.15f, 0.15f};
    for (int s = 0; s < 4; ++s) {
      c.betaFloorA[s] = floorA[s];
      c.betaFloorB[s] = floorB[s];
      c.betaCeil[s] = ceil[s];
    }
    // Pizzicato's second (vertical) polarisation: the violin's (InstrumentDef
    // defaults, 2.5x the string's T60 at the horizontal's level). Against the
    // Iowa pizzicato (tools/pizz_decay.py --iowa, mf/ff, 122 notes) they score
    // 0.69 (the violin vs VSCO 0.98): the first stage slower than the
    // recordings' (-42 vs -68 dB/s median), the second faster (-22 vs -15),
    // the level at 1 s within 1.5 dB. A weaker, longer second polarisation
    // (0.3 at 4x) matches the two stages better (0.64) but pulls every
    // partial's decay off the recordings (partial_q.py, 1-2 kHz Q 181 vs 293):
    // the loss fit leans on it. 8.6 tried a joint refit (PLAN.md): the first
    // stage stays too slow whatever the string's losses; kept as it was.
    c.wolfMode = 2;  // the Wolf Eliminator acts on the 185 Hz mode
    // Attacks from the air (cello doc §4.2-4.3): Galluzzo's switch-on
    // onsets needed ~4x the sustaining minimum force. +100% at first contact
    // (the note's force sits ~2x above its minimum), settling over ~50 ms,
    // +-20% from note to note. instrument_lab attacks (with the swell below):
    // within 10 periods of Helmholtz motion 3 -> 83 of 240, median 21 -> 13
    // periods; the first slip ~5 ms after the key (the bow catches before it
    // lets go). +150% did a little better on attacks but left the open G's
    // pp window off Pressure 0 (calibrate_force.py) [T: the numbers].
    // Galluzzo's slow-start stiction (a <~ 0.6 m/s^2) never applies: the
    // performer's attacks accelerate at 2.5 m/s^2 or more.
    c.attackLift = 1.f;
    c.attackLiftTau = 0.05f;
    c.attackVary = 0.2f;
    // The attack swell, fitted to the Iowa arco's onsets (pp/mf/ff at
    // velocities 45/80/110: within 3 dB of the held level after 138 / 218 /
    // 55 ms, the first 60 ms 17 / 22 / 11 dB under; synth 148 / 210 / 50 ms,
    // 16 / 19 / 10 dB; MODEL.md "The cello"). The cellist starts mf and f
    // notes much softer than the violinist the violin's table came from (the
    // Philharmonia cellist, measured only: f 195 ms and 19 dB, ff 75 ms and
    // 11 dB).
    // After the recorded onsets the owner heard the attacks as still soft:
    // the depths are scaled by 0.35 / 0.75 (Attack Swell 0.35 of the old
    // table at the default 0.75), which puts the first 60 ms between the two
    // cellists (6-7 dB over the Philharmonia's, 2-3 under bigcat's) and takes
    // away the treble's lag (EVALUATION.md "Attacks and sustains by
    // register") [T: by ear].
    {
      const float db[6] = {18.5f, 18.f, 22.f, 28.f, 20.f, 14.f}, t[6] = {0.36f, 0.3f, 0.34f, 0.4f, 0.18f, 0.1f};
      for (int i = 0; i < 6; ++i) c.swellDb[i] = db[i] * (0.35f / 0.75f), c.swellT[i] = t[i];
    }
    // Recorded onsets (MODEL.md "Recorded onsets"): bigcat's, at a level
    // fitted with tools/attack_sustain.py [T].
    c.onsets = &celloOnsets();
    c.onsetGain = 0.19f;
    {
      const float trim[4] = {0.f, 0.f, 0.f, 0.f};
      for (int i = 0; i < 4; ++i) c.onsetTrimDb[i] = trim[i];
    }
    return c;
  }();
  return d;
}

}  // namespace sm
