// The gesture layer (KB §7.3, §11): turns keys and controllers into bow and
// finger motion for the physics.
//
// The instrument is polyphonic: every note is a voice (Player) with its own
// string, bow and left hand, and all of them stand on one bridge (Instrument).
// The Performer hears the keys and gives them to the voices, in one of two
// modes:
//
// - Polyphonic (Legato off, the default): each key is a note of its own, bowed
//   from the air (or plucked), up to kMaxHeld at once. A key pressed again
//   while its note sounds restrikes that voice; letting go of a key ends its
//   note the chosen way (Release). A new note on a string still ringing from
//   a note let go plays that string, as on the instrument (a fast passage
//   re-bows a moving string); notes held together get strings of their own.
//   When every voice is busy, the quietest released one is let go (damped)
//   to make room, else the oldest held.
// - Legato (mono): one player, last-note priority, as a violinist plays.
//   Overlapping keys slur (same bow, finger change) when the interval is
//   within the slur range; a new key after all keys are up is a new stroke
//   in the other bow direction; a slur to another string carries the bow
//   over (the old string rings on, its finger lifting a moment later).
//   Letting go of the sounding key returns to the newest key still down (up
//   to kMaxHeld are remembered). One voice per string: going back to a string
//   still ringing plays it again.
//
// The synth feeds it through Keys.h, which merges channels and doubled
// inputs: one noteOn per press of a key, one noteOff when it's really up. On
// its own it still ignores a held key pressed again within kDoubleOn,
// restrikes one pressed again later, and ignores a noteOff for a key it
// doesn't hold (tests/key_tests.cpp).
//
// Each voice (Player):
// - The left hand has a position (KB §11.2, player doc §3.3): in legato, a
//   slurred note within its reach is a finger change (hammer-on / lift-off:
//   the finger meets the string as a light touch that firms up over 20-25 ms,
//   with a small tap or pluck); one out of reach is a shift: the old finger
//   slides with the hand, then the new finger lands (a portamento glides on
//   into the note). Slides speed up into the note, the vibrato pauses through
//   them and the bow eases off.
// - Playability assistant (KB §11.5): a dynamics value picks v_b/beta from the
//   measured level law, splits it into v_b and beta, and sets the force on the
//   players' line through the Schelleng diagram, F ∝ v_b * beta^-1.39 * Z^1.39.
//   Pressure moves along the force axis; contact point moves beta. High
//   positions get more force (the body loads short, high strings: F_min rises).
// - Attacks: the bow lands (force rises over ~8 ms) and accelerates at
//   a = k * F (Guettler plane, KB §7.2); velocity raises k and adds an accent.
// - Bow changes (legato, a new note without a slur) reverse v_b at a limited
//   acceleration with a force dip. The bow never runs out.
// - Vibrato on stopped, bowed strings, delayed and ramped in, tied to dynamics.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Bridge.h"
#include "InstrumentDef.h"

namespace sm {

struct PerformerControls {
  float dynamics = 0.55f;     // 0..1
  float velSens = 0.7f;       // velocity -> dynamics
  float pressure = 0.f;       // -1..1 (log2 force offset / 1.5)
  float contact = 0.f;        // -1 tasto .. +1 ponticello
  float swell = 0.75f;        // 0..1 Attack Swell: notes grow into their level (1 = the Philharmonia player)
  bool legato = false;        // on: one player, slurs (the mono mode); off: polyphonic
  float slurRange = 12.f;     // semitones: overlapping keys further apart take a new bow (legato)
  float vibDepth = 26.f;      // cents, peak-to-peak at mf
  float vibRate = 5.5f;       // Hz
  float vibDelay = 0.1f;      // s
  float shiftTime = 0.04f;    // s (legato shifts)
  float bend = 0.f;           // semitones
  float a4 = 440.f;
  float changeFloor = 0.2f;   // force at zero bow speed during a reversal, x target [T]
  float changeWidth = 1.f;    // force is back to full at this fraction of the target speed [T]
  float minAccel = 2.5f;      // m/s^2, floor on attack acceleration: below ~2 soft notes take 0.25 s to speak [T]
  float fingerLift = 0.25f;   // s after the bow leaves a string (legato crossing) before the finger lifts [T]
  // How a note ends when its key goes up (KB §11.2 "Note release"):
  // 0 Auto (by key-up speed: quick = stop, gentle = lift), 1 Lift (the bow
  // leaves; the string rings), 2 Stop (the bow stops on the string and damps
  // it), 3 Finger Off (the bow leaves, then the finger: the note falls to the
  // open string).
  int releaseMode = 0;
  int forcedString = -1;      // -1 auto, else 0..3: play on this string when it reaches the note
  // Articulation for new notes (KB §11.2, §11.5): 0 Arco, 1 Martelé,
  // 2 Spiccato, 3 Tremolo, 4 Pizzicato, 5 Harmonics, 6 Sautillé.
  int articulation = 0;
  float tremoloRate = 12.2f;
  // Player habits (instrument-research player_technique_and_responsiveness.md).
  float intonStyle = 0.7f;   // 0 equal temperament .. 1 expressive (§3.2)
  float vibShape = 0.7f;     // 0 the same vibrato on every note .. 1 by context (§3.1)
  float portamento = 0.3f;   // 0..1: audible slides on slow slurred leaps (§3.3; legato)
  bool avoidOpen = true;     // notes that would be open strings are stopped on the string below when they can take vibrato
};

enum Articulation { kArco = 0, kMartele, kSpiccato, kTremolo, kPizzicato, kHarmonics, kSautille };

struct StringTarget {
  float hz = 440.f;        // sounding pitch incl. vibrato and bend
  float noteHz = 440.f;    // without vibrato (sets bridge-side length)
  bool stopped = false;
  float force = 0.f;       // N
  float damp = 0.f;        // 0..1: the bow resting on the string as a damper (a "stop" release)
  float slide = 0.f;       // m/s: the stopping finger's speed along the string (shifts)
  float touchQ = 0.f;      // harmonics: touch point, fraction of the vibrating length from the finger end (0 = none)
  float touchR = 0.f;      // touch resistance / Z0
  int landId = 0;          // changes when a finger lands on (or lifts off) this sounding string
  bool landUp = true;      // a hammer-on (true) or a lift-off
  float landT = 0.f;       // s: how long the finger takes to stop (or free) the string
};

// A stroke's start, for the recorded onsets (Onsets.h): read by the
// Instrument when `id` changes.
struct OnsetEvent {
  int id = 0;
  int string = 0;
  float vel = 0.7f;      // the key's velocity, 0..1
  float levelRatio = 1.f;  // the stroke's v_b / beta before the swell (the level law)
  bool down = true;      // down-bow (frog to tip)
  bool rebow = false;    // a bow change on the string, not from the air
  int art = 0;           // Articulation
};

// The labs only: pin the bow's contact point (beta), speed (m/s) and force on
// the string (N) after the dynamics law; 0 = the law's.
struct LabPins {
  float beta = 0.f, vb = 0.f, force = 0.f;
  float pinAfter = 0.f;  // s into the note before the pins apply (after the performer's own attack)
  float attackLift = -1.f, attackTau = -1.f, attackVary = -1.f;  // >= 0: override InstrumentDef's
};

// What the voices share: the instrument, the clock, the smoothed controls.
struct PerformerShared {
  const InstrumentDef* def = nullptr;
  const Bridge* bridge = nullptr;
  float fs = 48000.f;
  float now = 0.f;  // s
  float dynIn = 0.55f, pressureIn = 0.f, contactIn = 0.f;  // smoothed controls
  LabPins lab;
};

// A note to play, as the Performer chose it.
struct NoteStart {
  int note = 69;         // the key (MIDI)
  float vel = 0.7f;      // 0..1
  int string = 0;        // which string (0 lowest)
  float stop = 69.f;     // the finger's note (MIDI, with the tuning)
  float touchQ = 0.f;    // harmonics: the touch (0 none)
  bool open = false;     // an open string
  float gap = 1.f;       // s since the previous key press
  float noteGap = 0.1f;  // the playing's note rate (s, smoothed): sautillé, spiccato
  float vibReg = 1.f;    // vibrato by register (shapeVibrato)
  float vibFast = 0.f;   // vibrato narrowing in fast passages
};

// One voice: a string, the bow on it, and the finger stopping it.
class Player {
 public:
  void prepare(const PerformerShared* sh, uint32_t seed) {
    sh_ = sh;
    seed_ = seed;
    landRate_ = 1.f - std::exp(-1.f / (0.008f * sh_->fs));
    liftRate_ = 1.f - std::exp(-1.f / (0.025f * sh_->fs));
    accentDecay_ = std::exp(-1.f / (0.07f * sh_->fs));
    reset();
  }

  void reset() {
    key_ = -1;
    note_ = -1;
    s_ = 2;
    bowing_ = false;
    changing_ = false;
    vb_ = 0.f;
    dir_ = -1.f;
    bowPos_ = 0.15f;
    hand_ = 2.f;
    bright_ = 0.f;
    art_ = kArco;
    tremPhase_ = 0.f;
    vibPhase_ = 0.f;
    vibDrift_ = 0.f;
    noteAge_ = 0.f;
    accent_ = 0.f;
    openAllow_ = 0.f;
    rng_ = seed_;
    landId_ = 0;
    claim();
    placeFinger(static_cast<float>(sh_->def->strings[s_].openNote));
    stopped_ = false;
  }

  // A fresh voice for a new note (the bow's place and direction carry over).
  void claim() {
    key_ = -1;
    bowing_ = false;
    changing_ = false;
    vb_ = 0.f;
    force_ = 0.f;
    stopping_ = false;
    muting_ = false;
    dying_ = false;
    damp_ = 0.f;
    fingerOffAt_ = 0.f;
    pluckStart_ = -1.f;
    pluckNow_ = 0.f;
    tapStart_ = -1.f;
    tapNow_ = 0.f;
    tapPending_ = 0.f;
    tapHalf_ = 0.f;
    touchQ_ = 0.f;
    landAfterSlide_ = false;
    slideFrom_ = slideTo_ = -1.f;
    startLift_ = 0.f;
    shiftT_ = dropT_ = 1.f;
    ++claimId_;
    claimOnset_ = onset_.id;
  }

  // Legato: this voice takes the bow from another (a slur or bow change onto
  // its string), and the left hand with it.
  void takeBow(const Player& o) {
    bowing_ = o.bowing_;
    changing_ = o.changing_;
    vb_ = o.vb_;
    dir_ = o.dir_;
    bowPos_ = o.bowPos_;
    vbT_ = o.vbT_;
    betaT_ = o.betaT_;
    fT_ = o.fT_;
    dyn_ = o.dyn_;
    levelRatio_ = o.levelRatio_;
    accel_ = o.accel_;
    accelChange_ = o.accelChange_;
    attackK_ = o.attackK_;
    accent_ = o.accent_;
    swellDb_ = o.swellDb_;
    swellT_ = o.swellT_;
    startLift_ = o.startLift_;
    startLiftDecay_ = o.startLiftDecay_;
    strokeForce_ = o.strokeForce_;
    tremPhase_ = o.tremPhase_;
    tremRateJit_ = o.tremRateJit_;
    tremForceJit_ = o.tremForceJit_;
    noteAge_ = o.noteAge_;
    art_ = o.art_;
    bright_ = o.bright_;
    // The same stroke goes on: no new onset (Instrument::startOnset).
    onset_ = o.onset_;
    claimOnset_ = onset_.id;
    takePlace(o);
  }
  // The bow's place and direction, and the hand's (a fresh stroke follows on).
  void takePlace(const Player& o) {
    dir_ = o.dir_;
    bowPos_ = o.bowPos_;
    hand_ = o.hand_;
    vibPhase_ = o.vibPhase_;
    vibDrift_ = o.vibDrift_;
  }

  void setArticulation(int art) { art_ = art; }

  // Play a note on this voice's string. slur: the bow goes on (legato), else a
  // new stroke (a bow change if the bow is moving, else from the air).
  // sounding: the string is sounding under this bow, so a slur is a finger
  // change or shift; otherwise the finger is placed silently.
  void play(const NoteStart& ns, const PerformerControls& c, bool slur, bool sounding) {
    const float prevNote = note_ >= 0 ? static_cast<float>(note_) : static_cast<float>(ns.note);
    vel_now_ = ns.vel;
    note_ = ns.note;
    key_ = ns.note;
    s_ = ns.string;
    gapRaw_ = ns.gap;
    noteGap_ = ns.noteGap;
    vibReg_ = ns.vibReg;
    vibFast_ = ns.vibFast;
    keyAge_ = 0.f;
    touchQ_ = ns.touchQ;
    const float n = ns.stop;
    const bool open = ns.open;
    // Finger (KB §11.2, player doc §3.3). Slurred on a sounding string:
    // within the hand's reach (or to/from the open string) a finger lands or
    // lifts; out of reach the hand shifts: the old finger slides with it,
    // then the new finger lands (Flesch's L-type; the same finger if the
    // slide reaches the note). Otherwise the finger is placed silently.
    const float openN = static_cast<float>(sh_->def->strings[s_].openNote);
    const float k = n - openN;
    if (sounding && slur) {
      const float from = currentFinger();
      const float kFrom = from - openN;
      const bool up = k > kFrom;
      fingerFrom_ = fingerVia_ = from;
      fingerTo_ = n;
      shiftT_ = 1.f;
      dropT_ = 0.f;
      dropDur_ = up ? 0.002f : 0.003f;
      tapPending_ = tapFor(up, ns.vel);
      if (open || kFrom < 0.5f || (inReach(k) && inReach(kFrom))) {
        // A finger change. A stopped note out of reach after the open string:
        // the hand moved while the open string sounded.
        if (!open && !inReach(k)) hand_ = handFor(k, up);
      } else {
        const float newHand = handFor(k, up);
        float via = from + (newHand - hand_);
        via = up ? std::min(via, n) : std::max(via, n);  // never past the note
        // Portamento glides into the note: the new finger takes over part-way
        // and completes the slide (the "Russian" way, from underneath),
        // rather than the old finger stopping at the intermediary note and
        // the new one dropping on (Flesch's B-portamento, as quick shifts
        // still do). Chosen by ear over the B-type (PLAN.md "Portamento").
        const bool glideIn = kArriveGlide && c.portamento > 0.f && gapRaw_ >= 0.3f;
        if (glideIn) via = n;
        hand_ = newHand;
        fingerVia_ = via;
        const float slideSt = std::fabs(via - from);
        const float interval = std::fabs(n - prevNote);
        // A gentle key slides slowly and expressively, a hard one shifts quickly
        // (x1.4 at velocity 0.2, x1 at 0.5, x0.4 at 1).
        const float speed = std::clamp(1.6f - 1.2f * ns.vel, 0.4f, 1.4f);
        shiftDur_ = 0.008f + c.shiftTime * std::min(1.f, slideSt / 5.f) * speed;
        // Portamento (player doc §3.3): on slow slurred leaps of a third or
        // more, an audible slide, up to x7 longer: none in fast passages [T].
        const float slow = std::clamp((gapRaw_ - 0.3f) / 0.5f, 0.f, 1.f);
        const float leap = std::clamp((interval - 2.f) / 5.f, 0.f, 1.f);
        shiftDur_ *= 1.f + 6.f * c.portamento * slow * leap;
        shiftT_ = slideSt > 0.01f ? 0.f : 1.f;
        if (slideSt > 0.01f) {
          slideFrom_ = sh_->now;
          slideTo_ = sh_->now + shiftDur_;
        }
        if (std::fabs(n - via) < 0.01f) {
          dropT_ = 1.f;  // the same finger: it just arrives
          // (gliding in: the new finger lands half-way through the slide)
          tapHalf_ = glideIn && tapPending_ != 0.f ? tapPending_ : 0.f;
          tapPending_ = 0.f;
        }
      }
      // A finger change with no slide lands at once: softly (the string
      // takes the landing, BowedString::land), so the pitch target moves now.
      if (kSoftLanding && shiftT_ >= 1.f && dropT_ < 1.f) {
        requestLanding(fingerTo_ > fingerVia_);
        dropT_ = 1.f;
      } else if (kSoftLanding && shiftT_ < 1.f && dropT_ < 1.f) {
        landAfterSlide_ = true;
      }
      if (shiftT_ >= 1.f && tapPending_ != 0.f) {
        tapStart_ = sh_->now;
        tapAmp_ = tapPending_;
        tapPending_ = 0.f;
      }
    } else {
      placeFinger(n);
      if (!open && !inReach(k)) hand_ = handFor(k, k > hand_);
    }
    stopped_ = !open;
    fingerOffAt_ = 0.f;
    updateForceFactors();
    if (!slur) startStroke(ns.vel, c, bowing_);
    else computeTargets(c);
  }

  // Pluck a note on this voice's string (pizzicato): the finger stops it, the
  // bow is away.
  void pluck(const NoteStart& ns) {
    vel_now_ = ns.vel;
    note_ = ns.note;
    key_ = ns.note;
    s_ = ns.string;
    art_ = kPizzicato;
    stopped_ = !ns.open;
    placeFinger(ns.stop);
    touchQ_ = 0.f;
    noteAge_ = 0.f;
    fingerOffAt_ = 0.f;
    lift();
    startPluck(ns.vel, 0.f);
    ++onset_.id;  // (no onset for plucks; the id marks the note's start)
    onset_.string = s_;
    onset_.vel = ns.vel;
    onset_.art = kPizzicato;
    onset_.rebow = false;
  }

  // Its key went up: end the note the chosen way.
  void release(float relVel, const PerformerControls& c) {
    key_ = -1;
    int mode = c.releaseMode;
    if (mode == 0) mode = relVel > 0.7f ? 2 : 1;  // a quick key-up stops the bow on the string
    if (art_ == kPizzicato) {
      // A plucked note rings on; "stop" damps it with the hand.
      if (mode == 2) {
        muting_ = true;
        muteStart_ = sh_->now;
      }
      if (mode == 3 && stopped_) fingerOffAt_ = sh_->now + 0.04f;
      return;
    }
    if (mode == 2 && bowing_) {
      stopping_ = true;
      stopStart_ = sh_->now;
      return;
    }
    lift();
    if (mode == 3 && stopped_) fingerOffAt_ = sh_->now + 0.04f;  // the finger leaves just after the bow
  }

  // Legato: the bow has left for another string; this one rings on, and its
  // finger lifts a moment later (so it rings open).
  void leave(const PerformerControls& c) {
    key_ = -1;
    stopping_ = false;
    lift();
    if (stopped_) fingerOffAt_ = sh_->now + c.fingerLift;
  }

  // All notes off: the bow lifts, the strings ring on.
  void allOff() {
    key_ = -1;
    stopping_ = false;
    damp_ = 0.f;
    lift();
  }

  // Make room: the voice is damped out (the Instrument frees it when quiet).
  void steal() {
    key_ = -1;
    stopping_ = false;
    lift();
    dying_ = true;
  }

  // A new note on this voice in legato: a "stop" release or a damping hand
  // still under way ends.
  void clearStop() {
    stopping_ = false;
    damp_ = 0.f;
    muting_ = false;
    fingerOffAt_ = 0.f;
  }

  // Per sample (after the shared clock moved): bow kinematics and forces.
  void tick(const PerformerControls& c, float dt) {
    const float now = sh_->now;
    // Bow velocity: approach dir * vbTarget at a limited acceleration. A
    // "stop" release brings the bow to rest on the string in ~40 ms, then
    // lifts it.
    float want = bowing_ ? dir_ * vbT_ : vb_ * 0.999f;
    float accel = bowing_ ? accel_ : 0.5f;
    if (dying_) {
      damp_ = std::min(1.f, damp_ + dt / 0.01f);
    } else if (stopping_) {
      want = 0.f;
      accel = std::max(vbT_, 0.05f) / 0.04f;
      // Once the bow has (nearly) stopped, it rests on the string as a damper
      // (over ~15 ms), then lifts after 150 ms.
      if (std::fabs(vb_) < 0.1f * std::max(vbT_, 0.05f)) damp_ = std::min(1.f, damp_ + dt / 0.015f);
      if (std::fabs(vb_) < 1e-3f && now - stopStart_ > 0.15f) {
        stopping_ = false;
        lift();
      }
    } else if (muting_) {
      // The hand damps a plucked string, then lets go after 200 ms.
      damp_ = std::min(1.f, damp_ + dt / 0.015f);
      if (now - muteStart_ > 0.2f) muting_ = false;
    } else if (damp_ > 0.f) {
      damp_ = std::max(0.f, damp_ - dt / 0.02f);
    }
    // Bow-stroke templates (Demoucron, KB §11.5) set the bow's speed directly.
    templateForce_ = -1.f;
    if (bowing_ && (art_ == kMartele || art_ == kSpiccato || art_ == kTremolo || art_ == kSautille)) strokeTemplate(c, dt, want);
    if (templateForce_ >= 0.f) {
      vb_ = want;  // the template's speed is already smooth
    } else {
      const float dv = want - vb_;
      const float step = accel * dt;
      vb_ += std::clamp(dv, -step, step);
    }
    // Bow position along the hair (it never runs out: no bow changes by
    // themselves).
    bowPos_ = std::clamp(bowPos_ + vb_ * dt / sh_->def->bowLength, 0.1f, 0.9f);
    // Force: landing ramp, change dip, accent; lift ramps to 0.
    const float speedFrac = vbT_ > 1e-4f ? std::min(1.f, std::fabs(vb_) / vbT_) : 1.f;
    // Force dips through a reversal: players lighten the bow, which shortens the
    // gap and cleans the re-attack (KB §11.5: dip 0.6 -> 0.1 N, gap 28 -> 16 ms).
    const float sf = std::min(1.f, speedFrac / c.changeWidth);
    // (A stop holds its force: the stationary bow is what damps the string.)
    const float dip = stopping_ ? 1.f : c.changeFloor + (1.f - c.changeFloor) * sf * sf;
    accent_ *= accentDecay_;
    const float weight = 1.f + 0.12f * (0.5f - bowPos_);  // +6% at the frog, -6% at the tip
    float fTarget = fT_ * dip * (1.f + accent_) * strokeForce_ * weight;
    if (startLift_ > 0.f) {
      fTarget *= 1.f + startLift_;
      startLift_ *= startLiftDecay_;
      if (startLift_ < 1e-3f) startLift_ = 0.f;
    }
    {
      const float base = templateForce_ >= 0.f ? templateForce_ : fTarget;
      const float goal = bowing_ ? std::min(base * fFactor_, fMax_) : 0.f;
      const float rate = goal > force_ ? landRate_ : liftRate_;
      force_ += (goal - force_) * rate;
      if (force_ < 1e-4f && goal == 0.f) force_ = 0.f;
    }
    noteAge_ += dt;
    keyAge_ += dt;
    vibPhase_ += vibRateNow_ * dt;
    if (vibPhase_ >= 1.f) {
      vibPhase_ -= 1.f;
      // Slow random drift of rate and depth per cycle (KB §11.2).
      vibRateJit_ = 1.f + 0.05f * bipolar();
      vibDepthJit_ = 1.f + 0.15f * bipolar();
    }
    // The vibrato's centre wanders (the finger rolls about a centre that
    // itself drifts): measured 2.2 cents (std over ~0.2 s and up) on the
    // Philharmonia violin, 3.4 on VSCO's, against 0.6 for a fixed centre (the
    // tracker's noise). With a fixed centre each partial keeps a steady line
    // at its centre pitch ~14 dB over the vibrato's smear (recordings ~7):
    // heard as a ring, like beating. An Ornstein-Uhlenbeck drift, kVibDriftTau,
    // kVibDriftCents (std) [T].
    vibDrift_ += (-vibDrift_ / kVibDriftTau) * dt + kVibDriftCents * std::sqrt(6.f * dt / kVibDriftTau) * bipolar();
    if (shiftT_ < 1.f) {
      shiftT_ = std::min(1.f, shiftT_ + dt / shiftDur_);
      if (tapHalf_ != 0.f && shiftT_ >= 0.5f) {
        tapStart_ = now;
        tapAmp_ = tapHalf_;
        tapHalf_ = 0.f;
      }
    } else if (dropT_ < 1.f) {
      // The slide has arrived: the new finger lands (or lifts) now.
      if (landAfterSlide_) {
        landAfterSlide_ = false;
        requestLanding(fingerTo_ > fingerVia_);
        dropT_ = 1.f;
      }
      if (tapPending_ != 0.f) {
        tapStart_ = now;
        tapAmp_ = tapPending_;
        tapPending_ = 0.f;
      }
      dropT_ = std::min(1.f, dropT_ + dt / dropDur_);
    }
    // Finger taps: a raised-cosine velocity pulse at the finger.
    tapNow_ = 0.f;
    if (tapStart_ >= 0.f) {
      const float t = now - tapStart_;
      if (t < kTapDur) {
        const float x = std::sin(3.14159265f * t / kTapDur);
        tapNow_ = tapAmp_ * x * x;
      } else {
        tapStart_ = -1.f;
      }
    }
    // Plucks: the finger loads the string (force rises over 10-25 ms), then
    // slips off (falls in 1-2.5 ms) (KB §11.2 "Pizzicato").
    pluckNow_ = 0.f;
    if (pluckStart_ >= 0.f && now >= pluckStart_) {
      const float t = now - pluckStart_;
      if (t < pluckRise_) {
        const float x = std::sin(1.5707963f * t / pluckRise_);
        pluckNow_ = pluckF_ * x * x;
      } else if (t < pluckRise_ + pluckRel_) {
        const float x = std::cos(1.5707963f * (t - pluckRise_) / pluckRel_);
        pluckNow_ = pluckF_ * x * x;
      } else {
        pluckStart_ = -1.f;
      }
    }
  }

  // At control rate: smoothing, targets, vibrato envelope, finger lift.
  void controlTick(const PerformerControls& c, float dtControl) {
    // Brightness drive: the bow force on the players' line, in octaves re mf
    // (dynamics 0.557, the middle of the p/f pair the radiation was fitted on).
    // f ~ e^(3.62 d) x 2^(1.5 pressure) x 2^(2.09 contact) (computeTargets:
    // vb ~ e^(1.4 d), vb/beta ~ e^(3 d), f ~ vb^-0.39 (vb/beta)^1.39; the
    // contact moves beta by 2^(-1.5 c)). Not bowing: neutral. ~20 ms glide.
    {
      const bool bowed = bowing_ && art_ != kPizzicato;
      const float want = bowed ? std::clamp(5.23f * (dyn_ - 0.557f) + 1.5f * sh_->pressureIn + 2.09f * sh_->contactIn, -3.f, 3.f) : 0.f;
      bright_ += (want - bright_) * (1.f - std::exp(-dtControl / 0.02f));
    }
    // Open-string bow allowance: eases in and out with the bow (~30 ms).
    {
      const float ka = 1.f - std::exp(-dtControl / 0.03f);
      const float want = bowing_ && art_ != kPizzicato ? 1.f : 0.f;
      openAllow_ += (want - openAllow_) * ka;
    }
    computeTargets(c);
    const float d = dyn_;
    const float age = noteAge_ - c.vibDelay;
    const float ramp = age <= 0.f ? 0.f : std::min(1.f, age / 0.35f);
    // Peak-to-peak depth given; amplitude is half. Louder = wider, faster.
    // Shaped by context (vibReg_, vibFast_), and on long strokes it keeps
    // growing a little for ~1.5 s, as players' vibrato does [T].
    const float grow = 1.f - c.vibShape * 0.25f * (1.f - std::min(1.f, std::max(0.f, age) / 1.5f));
    const float fast = vibFast_ * std::max(0.f, 1.f - keyAge_ / 0.5f);
    const float shapeK = 1.f + c.vibShape * (vibReg_ * (1.f - 0.7f * fast) - 1.f);
    vibCents_ = 0.5f * c.vibDepth * (0.6f + 0.8f * d) * ramp * vibDepthJit_ * shapeK * grow * vibAfterShift();
    vibRateNow_ = c.vibRate * (0.9f + 0.2f * d) * vibRateJit_;
    // A finger due off (Finger Off release, or the bow gone to another string
    // in legato) leaves the string, so it rings open.
    if (fingerOffAt_ > 0.f && sh_->now >= fingerOffAt_ && !bowing_) {
      fingerOffAt_ = 0.f;
      if (stopped_) {
        stopped_ = false;
        placeFinger(static_cast<float>(sh_->def->strings[s_].openNote));
      }
    }
  }

  // At control rate: the string's pitch, vibrato and force.
  void target(StringTarget& out, const PerformerControls& c) const {
    const InstrumentDef& def = *sh_->def;
    const bool lead = bowing_;
    float n = currentFinger();
    if (lead && note_ >= 0) n += c.bend;
    const bool stopped = stopped_ || (lead && std::fabs(c.bend) > 0.01f);
    // Open strings are tuned a little sharp at rest (InstrumentDef): the bow
    // pulls them down. Plucked, there is no bow, so they sound at pitch.
    // The bow flattens a string it's on; an open string can't be corrected
    // by the finger, so it carries an allowance while (and only while) the
    // bow is on it (openAllow_). Unbowed open strings sit at true pitch.
    // Harmonics are bowed much lighter (updateForceFactors), so they flatten
    // less: about half the allowance (natural harmonics within ±3 cents) [T].
    // Above mf the faster bow, its force capped (forceMax), flattens the
    // string less: the allowance falls to 70% at ff (tools/open_strings.py) [T].
    const float dt = std::clamp((dyn_ - 0.5f) / 0.35f, 0.f, 1.f);
    const float allow = (touchQ_ > 0.f ? 0.55f : 1.f) * (1.f - 0.3f * dt * dt * (3.f - 2.f * dt));
    float tune = stopped ? 0.f : def.openTuneCents[s_] * 0.01f * openAllow_ * allow;
    // Open strings in pure fifths around A (G -3.9, D -2.0, E +2.0 cents).
    if (!stopped) tune += 0.01f * c.intonStyle * openPureCents(def, s_);
    // Stopped notes: the finger corrects the bow's flattening (the
    // instrument's stoppedTuneCents), only while bowed; harmonics, bowed
    // lighter, about half.
    if (stopped && lead && art_ != kPizzicato) tune += def.stoppedTuneCents[s_] * 0.01f * (touchQ_ > 0.f ? 0.55f : 1.f);
    const float noteHz = mtof(n + tune, c.a4);
    float vib = 0.f;
    if (stopped) {
      // The centre drift goes with the vibrato (a straight note stays put).
      const float withVib = std::min(1.f, vibCents_ / 10.f);
      vib = vibCents_ * std::sin(6.2831853f * vibPhase_) + withVib * vibDrift_;
    }
    out.noteHz = noteHz;
    out.hz = noteHz * std::pow(2.f, vib / 1200.f);
    out.stopped = stopped;
    out.force = force_;
    out.damp = damp_;
    out.touchQ = touchQ_;
    out.touchR = touchQ_ > 0.f ? kTouchR : 0.f;
    out.landId = landId_;
    out.landUp = landUp_;
    out.landT = landUp_ ? kLandOn : kLandOff;
    // Finger speed along the string during a shift: x = L (1 - 2^(-p/12)),
    // p semitones above the open string; dn/dt from the S-curve.
    out.slide = 0.f;
    if (shiftT_ < 1.f && stopped) {
      const float t = shiftT_;
      const float dndt = (fingerVia_ - fingerFrom_) * slideRate(t) / shiftDur_;
      const float pos = n - static_cast<float>(def.strings[s_].openNote);
      const float dxdn = def.scaleLength * 0.0577623f * std::pow(2.f, -pos / 12.f);  // L ln2/12 2^(-p/12)
      out.slide = std::fabs(dxdn * dndt);
    }
  }

  // Per sample: the plucking finger's force on the string (N).
  // The pluck splits between the polarisations by its angle.
  float pluckForce() const { return pluckNow_ * pluckCos_; }
  float pluckForceVertical() const { return pluckNow_ * pluckSin_; }
  // Per sample: a landing or lifting finger's pulse (m/s, added at the finger end).
  float fingerTap() const { return tapNow_; }
  float fingerNote() const { return fingerTo_; }  // the note (MIDI) the string is fingered for
  float handPlace() const { return hand_; }        // the hand's place, semitones above the open string
  float beta() const { return betaT_; }
  float force() const { return force_; }
  // Bow force re mf on the players' line, in octaves (smoothed; 0 unbowed).
  float brightness() const { return bright_; }
  float bowVelocity() const { return vb_; }
  float bowPosition() const { return bowPos_; }
  int string() const { return s_; }
  int key() const { return key_; }        // the key holding this voice (-1: none, the note rings on)
  int note() const { return note_; }      // the note last played (-1 before any)
  int articulation() const { return art_; }
  bool bowing() const { return bowing_; }
  bool stopping() const { return stopping_; }
  float stopTime() const { return sh_->now - stopStart_; }
  bool dying() const { return dying_; }
  bool stopped() const { return stopped_; }
  float damp() const { return damp_; }
  int claimId() const { return claimId_; }
  int claimOnset() const { return claimOnset_; }  // the stroke count when the voice was claimed (or took the bow)
  const OnsetEvent& onset() const { return onset_; }
  // Anything still to happen that the Instrument must wait for before the
  // voice can be freed (a ringing string is the Instrument's to judge).
  bool busy() const {
    return key_ >= 0 || bowing_ || stopping_ || muting_ || force_ > 0.f || pluckStart_ >= 0.f || tapStart_ >= 0.f ||
           (fingerOffAt_ > 0.f && !dying_) || shiftT_ < 1.f || dropT_ < 1.f;
  }

  static float mtof(float n, float a4) { return a4 * std::pow(2.f, (n - 69.f) / 12.f); }
  static int chainOf(int pc) {  // fifths above A, 0..11
    int k = (7 * (pc - 9)) % 12;
    return k < 0 ? k + 12 : k;
  }
  // Open strings in pure fifths around A: cents from equal temperament.
  static float openPureCents(const InstrumentDef& def, int s) {
    int k = chainOf(def.strings[s].openNote % 12);
    if (k > 5) k -= 12;
    return 1.955f * static_cast<float>(k);
  }

 private:
  void lift() {
    bowing_ = false;  // the bow leaves the string; it rings on
  }

  // The swell's depth (dB, at Attack Swell 1) and length by dynamic
  // (InstrumentDef::swellDb/swellT). The violin's fitted to the Philharmonia
  // rise times per dynamic (pp..ff at velocities 20..120, i.e. dynamics
  // 0.24..0.79 at the default Velocity -> Dynamics): within 3 dB of the
  // sustain after ~150 / 160 / 170+ / 75 / 50 / 35 ms [T].
  static float swellTable(float d, const float* v) {
    static constexpr float kD[6] = {0.24f, 0.35f, 0.46f, 0.571f, 0.681f, 0.792f};
    if (d <= kD[0]) return v[0];
    for (int i = 1; i < 6; ++i)
      if (d < kD[i]) return v[i - 1] + (v[i] - v[i - 1]) * (d - kD[i - 1]) / (kD[i] - kD[i - 1]);
    return v[5];
  }
  float swellDepth(float d) const { return swellTable(d, sh_->def->swellDb); }
  float swellLength(float d) const { return swellTable(d, sh_->def->swellT); }  // s

  // A new stroke: a bow change if the bow is moving, else from the air.
  void startStroke(float vel, const PerformerControls& c, bool change) {
    const bool rebow = change && bowing_;
    noteAge_ = 0.f;
    strokeForce_ = 1.f;
    // Guettler plane: acceleration proportional to force, steeper with
    // velocity, and an accent on loud notes [T].
    attackK_ = (6.f + 8.f * vel) * kAttackBite;
    accent_ = std::max(0.f, vel - 0.55f) * kAccent;
    // Attack swell (measured on the Philharmonia violin, docs/EVALUATION.md):
    // a bowed note grows into its level. Its first 60 ms sit 12-13 dB under
    // the peak at pp-mp, ~7 dB at mf, 3-5 dB at f-ff; within 3 dB after
    // 170-250 / 70-200 / 58-77 ms. Fitted [T]: the deficit D0 (1 - t/T)^2 with
    // D0 15 dB (soft) .. 4 dB (loud), T 0.36 .. 0.26 s. From rest only in
    // full: a new bow mid-phrase (detache) gets 40% of the depth over half the
    // time, and in fast passages less still (notes under ~0.3 s apart).
    computeDyn(c);
    swellDb_ = c.swell * swellDepth(dyn_);
    swellT_ = swellLength(dyn_);
    if (rebow) {
      swellDb_ *= 0.4f * std::clamp((gapRaw_ - 0.12f) / 0.25f, 0.f, 1.f);
      swellT_ *= 0.5f;
      bowChange();
    } else {
      // From the air: the starting force (InstrumentDef::attackLift).
      const LabPins& lab = sh_->lab;
      const float lift = lab.attackLift >= 0.f ? lab.attackLift : sh_->def->attackLift;
      if (lift > 0.f) {
        const float tau = lab.attackTau >= 0.f ? lab.attackTau : sh_->def->attackLiftTau;
        const float vary = lab.attackVary >= 0.f ? lab.attackVary : sh_->def->attackVary;
        startLift_ = lift * (1.f + vary * bipolar());
        startLiftDecay_ = std::exp(-1.f / (std::max(tau, 1e-3f) * sh_->fs));
      }
      // From the air: alternate, unless the bow is nearly at one end.
      dir_ = bowPos_ > 0.8f ? -1.f : (bowPos_ < 0.2f ? 1.f : -dir_);
      vb_ = 0.f;
      bowing_ = true;
    }
    // The hand balances the bow's weight, imperfectly (player doc §1.4: frog
    // heavier, tip lighter; residual error a few % per stroke) [T].
    strokeForce_ = 1.f + 0.03f * bipolar();
    computeTargets(c);
    ++onset_.id;
    onset_.string = s_;
    onset_.vel = vel;
    onset_.levelRatio = levelRatio_;
    onset_.down = dir_ > 0.f;
    onset_.rebow = rebow;
    onset_.art = art_;
  }

  // Force scale on this string, relative to the assistant's base force:
  // string impedance (F ∝ Z^1.39 on the players' line), more force above a
  // fifth up the string (short, high strings: the body loads them, F_min
  // rises) [T]. Notes whose fundamental sits on a body resonance need more
  // force too (the body absorbs, F_min rises, KB §5.4): log2 F += 1.5 * 25 Z0
  // |Y(f0)|, fitted with tools/calibrate_force.py (C#5 on the 550 Hz mode) [T].
  void updateForceFactors() {
    const InstrumentDef& def = *sh_->def;
    const float z = def.strings[s_].z0;
    const float ratio = std::pow(2.f, (fingerTo_ - static_cast<float>(def.strings[s_].openNote)) / 12.f);
    const float pos = std::pow(std::max(1.f, ratio / 1.5f), 0.7f);
    float body = 1.f;
    if (sh_->bridge) {
      const float hz = mtof(fingerTo_, 440.f);
      const float load = std::min(1.f, def.bodyForceK * z * sh_->bridge->magnitude(hz, s_));
      body = std::pow(2.f, 1.5f * load);
    }
    fFactor_ = std::pow(z / def.zRef, def.forceZExp) * pos * body * def.forceTrim[s_];
    fMax_ = def.forceMax * pos;
    // Harmonics: a much lighter bow; quarter touches lighter still. Through
    // the synth: octaves within ~5 cents, thirds/quarters within ~15 [T].
    if (touchQ_ > 0.f) fFactor_ *= touchQ_ < 0.3f ? 0.15f : 0.3f;
  }

  // The slide follows a minimum-jerk curve, the usual model of a human
  // point-to-point reach (Flash & Hogan 1985): it starts and lands more
  // gently than a cubic S-curve [T]. A finger landing or lifting is a fast
  // S-curve (KB §11.2: 1-3 ms).
  // Players' slides are not symmetric: fitted with a logistic curve, a
  // violinist's slide turns fastest after its midpoint (Yang & Chew 2015/16:
  // the normalised inflection time sits past 0.5 for the violin, before it
  // for the erhu): the finger leaves gently and speeds into the note. Time
  // is warped (u = t^p) so the minimum-jerk curve's fastest point lands at
  // kSlideInflect of the slide [T: the value].
  static float slideWarpP() { return std::log(0.5f) / std::log(kSlideInflect); }
  static float slideCurve(float t) {
    const float u = kSlideInflect == 0.5f ? t : std::pow(t, slideWarpP());
    return u * u * u * (10.f + u * (6.f * u - 15.f));
  }
  // d(slideCurve)/dt.
  static float slideRate(float t) {
    const float p = kSlideInflect == 0.5f ? 1.f : slideWarpP();
    const float u = std::pow(t, p);
    const float du = p == 1.f ? 1.f : p * std::pow(std::max(t, 1e-6f), p - 1.f);
    return 30.f * u * u * (1.f - u) * (1.f - u) * du;
  }
  float currentFinger() const {
    if (shiftT_ < 1.f) return fingerFrom_ + (fingerVia_ - fingerFrom_) * slideCurve(shiftT_);
    if (dropT_ < 1.f) {
      const float t = dropT_;
      return fingerVia_ + (fingerTo_ - fingerVia_) * t * t * (3.f - 2.f * t);
    }
    return fingerTo_;
  }

  void placeFinger(float n) {
    fingerFrom_ = fingerVia_ = fingerTo_ = n;
    shiftT_ = dropT_ = 1.f;
    tapPending_ = 0.f;
  }

  // The hand's reach (KB §11.2; player doc §3.3; HandDef): violin, from a
  // low first finger (one below the hand's place) to an extended fourth
  // (five above). k: semitones above the open string. Positions are the
  // same on every string.
  bool inReach(float k) const {
    const HandDef& h = sh_->def->hand;
    const float r = std::round(k);
    const bool thumb = hand_ >= h.thumbFrom;
    const float below = thumb ? h.thumbBelow : h.reachBelow, above = thumb ? h.thumbReach : h.reachAbove;
    return r <= 0.f || (r >= hand_ - below && r <= hand_ + above);
  }
  // Where the hand goes for a note out of reach [T]: going up, the note
  // lands on the third finger, so the old (lower) finger slides and a higher
  // one drops (Flesch's L-type; from the third or fourth finger it is a
  // same-finger slide); going down, on the second, so the old finger slides
  // and lifts off onto the note. (HandDef: how far above the hand's place
  // those fingers stop.)
  float handFor(float k, bool up) const {
    const HandDef& h = sh_->def->hand;
    if (!up && k - h.thumbDown >= h.thumbFrom) return std::round(k - h.thumbDown);
    return std::max(2.f, std::round(up ? k - h.upFinger : k - h.downFinger));
  }

  // A finger lands (hammer-on) or lifts sideways (lift-off, a small pluck) on
  // a bowed string: a velocity pulse at the finger (KB §11.2) [T level]. The
  // hammer-on mostly pushes the string toward the fingerboard (the other
  // polarisation), so it is the weaker of the two here.
  float tapFor(bool down, float vel) { return (down ? kTapHammer : kTapLift) * (0.5f + vel) * (bipolar() < 0.f ? -1.f : 1.f); }

  // Bow-stroke templates, from Demoucron's measured strokes (KB §11.5).
  // Sets `want` (bow velocity) and templateForce_ (base force before the
  // string's factor); t is the time since the stroke began.
  void strokeTemplate(const PerformerControls& c, float dt, float& want) {
    const float t = noteAge_, d = dyn_;
    auto sin2 = [](float x) {
      const float v = std::sin(1.5707963f * x);
      return v * v;
    };
    if (art_ == kMartele) {
      // Pinch with the bow still, then a fast stroke while the force drops
      // over 0.2 s, with the bow's ~13 Hz ripple; the bow stays on the string.
      const float fp = 0.8f + 1.9f * d, tp = 0.06f * d, vm = 0.5f + 0.7f * d, T = 0.25f + 0.08f * d, A = 0.42f;
      const float fmin = 0.25f + 0.2f * d;
      if (t < tp) {
        want = 0.f;
        templateForce_ = fp;
        return;
      }
      const float u = t - tp;
      float v = 0.f;
      if (u < A * T) v = vm * sin2(u / (A * T));
      else if (u < T) v = vm * (1.f - sin2((u - A * T) / ((1.f - A) * T)));
      else damp_ = std::min(1.f, damp_ + 2.f * dt / 0.015f);  // at rest on the string: it damps
      want = dir_ * v;
      const float drop = u < 0.2f ? 0.5f * (fp - fmin) * (1.f + std::cos(3.14159265f * u / 0.2f)) : 0.f;
      const float ripple = 0.15f * fp * std::exp(-u / 0.08f) * std::cos(6.2831853f * 13.f * u);
      templateForce_ = std::max(0.f, fmin + drop + ripple);
    } else if (art_ == kSautille) {
      // The bow bounces by itself (Guettler & Askenfelt 1997, KB §11.5): the
      // speed is a sine at half the note rate (one note per half cycle,
      // starting at the reversal) and the force a cosine at the note rate,
      // clipped at 0 (briefly in the air), peaking 53 degrees of its cycle
      // after the reversal: their "perfect" phase. The cycle is the time
      // between key presses (60-200 ms). Peak force as spiccato (x0.4 of
      // Demoucron's 0.5-3 N).
      const float T = std::clamp(noteGap_, 0.06f, 0.2f);
      const float fm = 0.4f * (0.5f + 2.5f * std::pow(d, 1.5f));
      const float vm = 0.08f + 0.6f * std::pow(d, 1.2f);
      if (t < T) {
        const float ph = 6.2831853f * t / T - 0.925f;  // 53 degrees
        templateForce_ = std::max(0.f, fm * (0.35f + 0.65f * std::cos(ph)));
        want = dir_ * vm * std::sin(3.14159265f * t / T);
      } else {
        templateForce_ = 0.f;  // no next note: the bow leaves the string
        want = 0.f;
      }
    } else if (art_ == kSpiccato) {
      // Thrown: a ~75 ms force bell (rise a little faster than the fall) with a
      // half-sine stroke, then in the air. Demoucron measured 0.5 N (p) to 3 N
      // (f) peaks; x0.4 maps them onto this string's force scale (at x1 the
      // notes went 20-40 cents flat, a sign of crushing) [T].
      // Slower spiccato stays longer on the string, faster shorter (the note
      // rate, from the time between keys: x0.7 at 9/s, x1.5 at 3/s) [T].
      const float len = std::clamp(noteGap_ / 0.15f, 0.7f, 1.5f);
      const float tc = 0.075f * len, A = 0.45f, fm = 0.4f * (0.5f + 2.5f * std::pow(d, 1.5f));
      const float vm = 0.08f + 0.7f * std::pow(d, 1.2f), tb = 0.14f * len;
      float f = 0.f;
      if (t < A * tc) f = fm * sin2(t / (A * tc));
      else if (t < tc) f = fm * (1.f - sin2((t - A * tc) / ((1.f - A) * tc)));
      want = t < tb ? dir_ * vm * std::sin(3.14159265f * t / tb) : 0.f;
      templateForce_ = f;
    } else {
      // Tremolo: ~12 reversals/s, near-sinusoidal speed; down-bows lighter
      // than up-bows (0.4 vs 0.6 N measured); timing and force jitter per stroke.
      const float v0 = 0.15f + 0.35f * d;
      const float before = tremPhase_;
      tremPhase_ += 0.5f * c.tremoloRate * tremRateJit_ * dt;
      if ((before < 0.5f && tremPhase_ >= 0.5f) || tremPhase_ >= 1.f) {
        if (tremPhase_ >= 1.f) tremPhase_ -= 1.f;
        tremRateJit_ = 1.f + 0.12f * bipolar();
        tremForceJit_ = 1.f + 0.15f * bipolar();
      }
      const float v = v0 * std::sin(6.2831853f * tremPhase_);
      want = dir_ * v;
      templateForce_ = fT_ * (v * dir_ > 0.f ? 0.8f : 1.2f) * tremForceJit_;
    }
  }

  // Each pluck varies a little (KB §11.2: the same pluck every time sounds
  // mechanical): force, rise, release and the pluck point [T].
  void startPluck(float vel, float delay) {
    const InstrumentDef& def = *sh_->def;
    pluckStart_ = sh_->now + delay;
    pluckF_ = (0.6f + 3.0f * std::pow(vel, 1.3f)) * (1.f + 0.1f * bipolar());
    pluckRise_ = 0.012f + 0.006f * (1.f + bipolar());
    // The release is a ramp: one as long as the period would cancel the
    // fundamental (a sinc null), which made open E plucks weak and uneven. Keep
    // it under ~0.35 period: 1-2.5 ms on the G string, ~0.5 ms on the E [T].
    const float period = 1.f / mtof(fingerTo_, 440.f);
    pluckRel_ = std::min(0.001f + 0.00075f * (1.f + bipolar()), (0.25f + 0.05f * (1.f + bipolar())) * period);
    const float ang = def.pluckAngleMin + (def.pluckAngleMax - def.pluckAngleMin) * 0.5f * (1.f + bipolar());
    pluckCos_ = std::cos(ang);
    pluckSin_ = std::sin(ang);
    pluckBeta_ = 0.2f + 0.05f * bipolar();
  }

  // A bow change: reverse at the bow-change acceleration, so the reversal
  // takes about kBowChange.
  void bowChange() {
    dir_ = -dir_;
    strokeForce_ = 1.f;
    accelChange_ = 2.f * std::max(vbT_, 0.05f) / kBowChange;
    changing_ = true;
  }

  void requestLanding(bool up) {
    ++landId_;
    landUp_ = up;
  }

  void computeDyn(const PerformerControls& c) { dyn_ = std::clamp(sh_->dynIn + c.velSens * (vel_now_ - 0.6f), 0.f, 1.f); }

  void computeTargets(const PerformerControls& c) {
    const InstrumentDef& def = *sh_->def;
    const LabPins& lab = sh_->lab;
    computeDyn(c);
    const float d = dyn_;
    const int s = s_;
    // Level law: L ≈ 7.7 ln(vb/beta) dB; pp..ff spans ~23 dB here.
    const float ratio = 0.6f * std::exp(3.0f * d);
    const float vbAttack = std::max(def.vbPP * std::pow(def.vbPPScale[s], 1.f - d) * std::exp(def.vbGrowth * d), ratio * 0.02f);  // beta can't go below 0.02
    float beta = vbAttack / ratio;
    // Heavy strings (InstrumentDef::betaFloor/betaCeil): a fast bow near the
    // bridge can't hold Helmholtz motion at any force (F_min ~ Z0^2 v_b /
    // beta^2), so the contact point stays above a floor that rises with the
    // bow's speed, and below a ceiling where the windows break up
    // (tools/window_map.py). The violin's law is Demoucron's, unlimited.
    if (def.limitBeta) {
      const float lo = def.betaFloorA[s] + def.betaFloorB[s] * vbAttack;
      beta = std::clamp(beta, lo, std::max(lo, def.betaCeil[s]));  // a fast bow's floor wins over the ceiling
    }
    // Long notes save bow (KB §7.3 step 2; Demoucron: 12 cm/s on whole notes,
    // 22 on halves, 54 on quarters): after the attack the bow slows to no
    // more than kLongVb, keeping its contact point (Demoucron: 25-40 mm), so
    // loud long notes settle a little softer. (Moving toward the bridge to
    // keep the level brightened soft notes most and erased the p -> f
    // brightening; a fast ff bow held on overheats the rosin and turns raucous.)
    const float age = std::clamp((noteAge_ - 0.25f) / 0.6f, 0.f, 1.f);
    const float ease = age * age * (3.f - 2.f * age);
    float vb = vbAttack + (std::min(kLongVb, vbAttack) - vbAttack) * ease;
    if (art_ == kSpiccato || art_ == kSautille) beta = 0.18f - 0.1f * d;  // Demoucron: 58 / 40 / 29 mm at p / mf / f
    if (art_ == kPizzicato) beta = pluckBeta_;                           // the pluck point
    levelRatio_ = vbAttack / std::clamp(beta * std::pow(2.f, -1.5f * sh_->contactIn), 0.015f, 0.3f);
    // Attack swell (startStroke): the level deficit, 40% from a slower bow
    // (the bow accelerates through the stroke, KB §11.5) and 60% from further
    // from the bridge, on the level law L = 7.7 ln(vb/beta); the force below
    // follows the players' line for both, so it stays in the window.
    float swellNow = 0.f;  // dB under the note's level now
    if (art_ == kArco && swellDb_ > 0.f && noteAge_ < swellT_) {
      const float u = 1.f - noteAge_ / swellT_;
      swellNow = swellDb_ * u * u;
      vb *= std::exp(-kSwellSpeed * swellNow / 7.7f);
      beta *= std::exp((1.f - kSwellSpeed) * swellNow / 7.7f);
    }
    // Through a slide the bow eases off (players slide "with a slight
    // diminuendo"; a lighter left hand too, KB §11.2): a dip of kSlideDipDb
    // (bell-shaped over the slide and the landing), on the same level law as
    // the swell; short shifts barely [T].
    const float now = sh_->now;
    if (kSlideDipDb > 0.f && art_ == kArco && slideFrom_ >= 0.f && now >= slideFrom_ && now < slideTo_ + kSlideDipTail) {
      const float len = slideTo_ + kSlideDipTail - slideFrom_;
      const float x = (now - slideFrom_) / len;
      const float dip = kSlideDipDb * std::min(1.f, (slideTo_ - slideFrom_) / 0.15f) * std::sin(3.14159265f * x);
      vb *= std::exp(-kSwellSpeed * dip / 7.7f);
      beta *= std::exp((1.f - kSwellSpeed) * dip / 7.7f);
      swellNow += dip;
    }
    // A touched string vibrates in k segments, like a string 1/k as long: the
    // bow goes closer to the bridge, by sqrt(k) (relative to the whole string;
    // by k it sat as far along the segment as a stopped note's bow, and the
    // harmonic kept a stopped note's spectrum), with less force
    // (updateForceFactors) [T: Philharmonia harmonics].
    if (art_ == kHarmonics && touchQ_ > 0.f) beta /= std::sqrt(std::round(1.f / touchQ_));
    beta *= std::pow(2.f, -1.5f * sh_->contactIn);
    beta = std::clamp(beta, 0.015f, 0.3f);
    const bool pinned = noteAge_ >= lab.pinAfter;
    if (lab.beta > 0.f && pinned) beta = lab.beta;
    if (lab.vb > 0.f && pinned) vb = lab.vb;
    // Base force on the players' line at the reference impedance; the
    // string's factor (impedance, position) is fFactor_.
    float f = def.forceRef * (vb / def.vbRef) * std::pow(beta / def.betaRef, -1.39f);
    // Soft playing sits near the bottom of the Helmholtz window in this model
    // (calibrate_force.py): lift it, most at pp; ff close to the bridge on
    // the lower strings is near the bottom too: lift it a little (ffLift) [T].
    // During an attack swell, by the dynamic the note is at now (the level
    // law: ~23 dB per unit of d), so the quiet start is lifted like pp.
    const float dNow = std::max(0.f, d - swellNow / 23.1f);
    const float margin = 0.1f + (0.8f + def.ppLift[s]) * (1.f - dNow) * (1.f - dNow) + def.ffLift[s] * dNow * dNow;
    f *= std::pow(2.f, 1.5f * (sh_->pressureIn + margin));
    if (lab.force > 0.f && pinned) f = lab.force / fFactor_;
    vbT_ = vb;
    betaT_ = beta;
    fT_ = f;
    // Acceleration: attack (k * F) until near target speed, then gentle;
    // reversals use the bow-change acceleration.
    const float fLead = std::min(f * fFactor_, fMax_);
    const float attackA = std::clamp(attackK_ * fLead, c.minAccel, 30.f);
    if (changing_) {
      accel_ = accelChange_;
      if (vb_ * dir_ > 0.9f * vbT_) changing_ = false;
    } else {
      accel_ = std::fabs(vb_) < 0.9f * vbT_ ? attackA : 2.f;
    }
  }

  float bipolar() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) * (2.f / 16777216.f) - 1.f;
  }

  // The vibrato through a shift (KB §11.2): the hand that slides can't also
  // vibrate. It stops as the slide starts (kVibStopT), and comes back after
  // arrival: kVibAfterShift later, ramping in over kVibRampShift [T, from the
  // KB's onset 100-300 ms and ramp 150-400 ms for a new note].
  float vibAfterShift() const {
    const float now = sh_->now;
    if (!kVibStopsInShift || slideFrom_ < 0.f || now < slideFrom_) return 1.f;
    if (now < slideTo_) return std::max(0.f, 1.f - (now - slideFrom_) / kVibStopT);
    const float g = std::clamp((now - slideTo_ - kVibAfterShift) / kVibRampShift, 0.f, 1.f);
    const float gone = std::max(0.f, 1.f - (slideTo_ - slideFrom_) / kVibStopT);  // a quick shift barely stopped it
    return gone + (1.f - gone) * g * g * (3.f - 2.f * g);
  }

  // The attack's steepness and accent on loud notes, as tuned with the old
  // Attack Bite control at its default (0.3): the control made no difference
  // larger than playing the same passage twice, so it went [T].
  static constexpr float kAttackBite = 1.45f, kAccent = 0.96f;
  static constexpr float kBowChange = 0.02f;  // s: a bow change's reversal, full speed one way to the other [T]
  static constexpr float kLongVb = 0.22f;     // m/s: the fastest long-note bow (Demoucron: half notes) [T]
  static constexpr float kSwellSpeed = 0.4f;  // share of the swell taken by bow speed (the rest by distance)
  // A finger's landing on the vibrating string, soft: first a light touch
  // (as on a harmonic: the fundamental dies, partials with a node near the
  // finger lead to the new note), firm after kLandOn; a lift-off frees it
  // over kLandOff (BowedString::land). The Philharmonia cellist's legato steps
  // take a median 34 ms from pitch to pitch (17-65 ms); a 2-3 ms switch made
  // same-string slurs sound artificial (the owner, by ear) [T: the values;
  // the spread didn't follow the register, so no dependence on the string's
  // height over the fingerboard].
  static constexpr bool kSoftLanding = true;
  static constexpr float kLandOn = 0.025f, kLandOff = 0.02f;  // s
  static constexpr float kSlideInflect = 0.65f;  // where a slide is fastest, of its length (0.5: symmetric)
  static constexpr bool kVibStopsInShift = true;
  static constexpr bool kArriveGlide = true;
  static constexpr float kSlideDipDb = 3.f, kSlideDipTail = 0.06f;  // dB, s
  static constexpr float kVibStopT = 0.04f, kVibAfterShift = 0.1f, kVibRampShift = 0.3f;  // s
  static constexpr float kVibDriftTau = 0.25f, kVibDriftCents = 2.5f;
  static constexpr float kTapDur = 0.0015f;   // s
  static constexpr float kTapHammer = 0.05f;  // m/s at velocity 0.5 [T]
  static constexpr float kTapLift = 0.09f;    // m/s at velocity 0.5 [T]
  static constexpr float kTouchR = 4.f;       // a light touch: R = 4 Z (KB: 1-10 Z) [T]

  const PerformerShared* sh_ = nullptr;
  uint32_t seed_ = 0x12345678u, rng_ = 1;
  int key_ = -1, note_ = -1, s_ = 2;
  int claimId_ = 0, claimOnset_ = 0;
  float vel_now_ = 0.7f;
  float bright_ = 0.f;  // brightness drive, octaves of force re mf
  bool bowing_ = false, changing_ = false;
  float strokeForce_ = 1.f;
  float vb_ = 0.f, dir_ = -1.f, bowPos_ = 0.15f;
  float vbT_ = 0.2f, betaT_ = 0.08f, fT_ = 0.4f, dyn_ = 0.5f;
  float levelRatio_ = 2.5f;  // v_b / beta before the swell (computeTargets)
  OnsetEvent onset_;
  float accel_ = 5.f, accelChange_ = 5.f, attackK_ = 10.f, accent_ = 0.f;
  float swellDb_ = 0.f, swellT_ = 0.3f;  // the attack swell's depth (dB) and length (s)
  float landRate_ = 0.01f, liftRate_ = 0.01f, accentDecay_ = 0.999f;
  float startLift_ = 0.f, startLiftDecay_ = 0.999f;  // the attack's extra force (InstrumentDef::attackLift)
  int landId_ = 0;
  bool landUp_ = true, landAfterSlide_ = false;
  float tapHalf_ = 0.f;  // a finger landing half-way through a slide (gliding in)
  float slideFrom_ = -1.f, slideTo_ = -1.f;  // the latest shift's slide: start and arrival (shared clock)
  float force_ = 0.f, fFactor_ = 1.f, fingerOffAt_ = 0.f;
  // Force cap: forceMax in low positions, scaled up high like the force law
  // itself (pos): short strings need more and stay clean (D5 and G5 on the G
  // at ff: Helmholtz from ~5 N to 20 N) [T].
  float fMax_ = 3.f;
  bool stopping_ = false, muting_ = false, dying_ = false;
  float stopStart_ = 0.f, damp_ = 0.f, muteStart_ = 0.f;
  int art_ = kArco;
  float noteGap_ = 0.1f, gapRaw_ = 1.f, vibReg_ = 1.f, vibFast_ = 0.f, keyAge_ = 0.f;
  float openAllow_ = 0.f;  // 0..1: the bow-flattening allowance on an open string
  float templateForce_ = -1.f, tremPhase_ = 0.f, tremRateJit_ = 1.f, tremForceJit_ = 1.f;
  float pluckStart_ = -1.f, pluckF_ = 0.f, pluckRise_ = 0.015f, pluckRel_ = 0.001f;
  float pluckCos_ = 1.f, pluckSin_ = 0.f;
  float pluckNow_ = 0.f, pluckBeta_ = 0.2f;
  float touchQ_ = 0.f;
  bool stopped_ = false;
  // The stopping finger: a shift slides from -> via (shiftT_), then a finger
  // lands or lifts via -> to (dropT_). A plain finger change has via = from.
  float fingerFrom_ = 69.f, fingerVia_ = 69.f, fingerTo_ = 69.f, shiftT_ = 1.f, shiftDur_ = 0.03f;
  float dropT_ = 1.f, dropDur_ = 0.0025f;
  float tapStart_ = -1.f, tapAmp_ = 0.f, tapNow_ = 0.f, tapPending_ = 0.f;
  float hand_ = 2.f;      // the first finger's place, semitones above the open string (2: first position)
  float vibDrift_ = 0.f;  // the vibrato centre's drift, cents
  float noteAge_ = 0.f, vibPhase_ = 0.f, vibCents_ = 0.f, vibRateNow_ = 5.5f, vibRateJit_ = 1.f, vibDepthJit_ = 1.f;
};

class Performer {
 public:
  static constexpr int kMaxHeld = 16;  // keys remembered; notes held at once (polyphonic)
  static constexpr int kVoices = 20;   // voices: kMaxHeld held, and room for released notes to ring and fade

  void prepare(float fs, const InstrumentDef& def) {
    sh_.fs = fs;
    sh_.def = &def;
    for (int v = 0; v < kVoices; ++v) voice_[v].prepare(&sh_, 0x12345678u + 0x9e3779b9u * static_cast<uint32_t>(v));
    reset();
  }
  // The bridge, for the body's load on each note (Player::updateForceFactors).
  void setBridge(const Bridge* b) { sh_.bridge = b; }

  void reset() {
    nHeld_ = 0;
    lead_ = 0;
    sh_.now = 0.f;
    primed_ = false;
    lastOn_ = -1.f;
    noteGap_ = 0.1f;
    for (float& w : pcWeight_) w = 0.f;
    keyCenter_ = 1;
    for (int v = 0; v < kVoices; ++v) {
      voice_[v].reset();
      loaded_[v] = false;
      level_[v] = 0.f;
      startedAt_[v] = 0.f;
    }
  }

  LabPins& lab() { return sh_.lab; }

  // Keys ---------------------------------------------------------------
  void noteOn(int note, float vel, const PerformerControls& c) {
    // A key already held, pressed again within 30 ms: the same key arriving
    // twice (a keyboard seen as two MIDI inputs, or a doubled MIDI route; a
    // real key can't go down twice without coming up). Ignore it: in
    // separate strokes (sautille, spiccato, martele) it restarted the stroke,
    // a doubled attack, and its zero gap read as a very fast passage.
    for (int i = 0; i < nHeld_; ++i)
      if (held_[i] == note && sh_.now - press_[i] < kDoubleOn) return;
    prime(c);
    // Sautillé follows the playing tempo: the time between key presses.
    const float gap = sh_.now - lastOn_;
    lastOn_ = sh_.now;
    if (gap > 0.04f && gap < 0.3f) noteGap_ += 0.5f * (gap - noteGap_);
    gap_ = gap;
    hearKey(note);
    removeHeld(note);
    // Full: forget the oldest key (its note lets go), so the newest is always
    // held and its release is heard.
    if (nHeld_ == kMaxHeld) {
      const int old = held_[0];
      removeHeldAt(0);
      releaseKey(old, 64.f / 127.f, c, -1);
    }
    held_[nHeld_] = note;
    vel_[nHeld_] = vel;
    press_[nHeld_] = sh_.now;
    ++nHeld_;
    if (c.legato) monoOn(note, vel, c);
    else polyOn(note, vel, c);
  }

  // relVel: key-up velocity 0..1 (keyboards without it send 64/127).
  void noteOff(int note, float relVel, const PerformerControls& c) {
    bool wasHeld = false;
    for (int i = 0; i < nHeld_; ++i) wasHeld |= held_[i] == note;
    removeHeld(note);
    if (!wasHeld) return;  // a key it doesn't hold: nothing to do
    if (!c.legato) {
      releaseKey(note, relVel, c, -1);
      return;
    }
    // Legato. Voices other than the player's (left from polyphonic playing)
    // let go of their key.
    releaseKey(note, relVel, c, lead_);
    Player& L = voice_[lead_];
    if (nHeld_ == 0) {
      if (L.key() >= 0 || L.bowing()) L.release(relVel, c);  // the last key up ends the note, however it was reached
      return;
    }
    // Another key goes up: the note being played goes on. Only the key of
    // the note sounding hands over (stress seed 46,
    // tests/key_tests.cpp performer_released_key_stops_under_a_dropped_key).
    if (note != L.key()) return;
    // Back to the newest key still down: slurred if close enough, else a
    // new bow. A plucked note isn't re-sounded (the string just rings).
    if (L.articulation() == kPizzicato) {
      L.release(relVel, c);
      return;
    }
    const int back = held_[nHeld_ - 1];
    const bool slur = L.bowing() && std::abs(back - L.note()) <= static_cast<int>(std::lround(c.slurRange));
    monoPlay(back, vel_[nHeld_ - 1], slur, L.articulation(), c);
  }

  void allNotesOff() {
    nHeld_ = 0;
    for (auto& p : voice_) p.allOff();
  }

  // Per sample: the clock, then every voice on the instrument.
  void tickSample(const PerformerControls& c) {
    const float dt = 1.f / sh_.fs;
    sh_.now += dt;
    for (int v = 0; v < kVoices; ++v)
      if (loaded_[v]) voice_[v].tick(c, dt);
  }

  // At control rate: smoothing, every voice's targets.
  void controlTick(const PerformerControls& c, float dtControl) {
    // Smooth the continuous controls (~15 ms) on this fixed clock.
    prime(c);
    const float k = 1.f - std::exp(-dtControl / 0.015f);
    sh_.dynIn += (c.dynamics - sh_.dynIn) * k;
    sh_.pressureIn += (c.pressure - sh_.pressureIn) * k;
    sh_.contactIn += (c.contact - sh_.contactIn) * k;
    for (int v = 0; v < kVoices; ++v)
      if (loaded_[v]) voice_[v].controlTick(c, dtControl);
  }

  // The voices, for the Instrument ---------------------------------------
  Player& voice(int v) { return voice_[v]; }
  const Player& voice(int v) const { return voice_[v]; }
  bool loaded(int v) const { return loaded_[v]; }  // on the instrument (sounding or ringing)
  // The Instrument: the voice's string is quiet and it has nothing to do: free it.
  void freeVoice(int v) { loaded_[v] = false; }
  // The Instrument, at control rate: how loud each voice's string is (for
  // choosing one to let go).
  void setLevel(int v, float level) { level_[v] = level; }

  // For the labs and tests: the player most recently given a note.
  const Player& lead() const { return voice_[lead_]; }
  int leadVoice() const { return lead_; }
  float bowVelocity() const { return lead().bowVelocity(); }
  float beta() const { return lead().beta(); }
  float bowPosition() const { return lead().bowPosition(); }
  float handPlace() const { return lead().handPlace(); }
  int currentString() const { return lead().string(); }
  int sounding() const { return lead().note(); }  // the note last played, -1 before any
  bool bowing() const { return lead().bowing(); }
  // Voices with the bow on their string, and their force on a string.
  int bowingCount() const {
    int n = 0;
    for (int v = 0; v < kVoices; ++v) n += loaded_[v] && voice_[v].bowing();
    return n;
  }
  float forceOn(int s) const {
    float f = 0.f;
    for (int v = 0; v < kVoices; ++v)
      if (loaded_[v] && voice_[v].string() == s) f += voice_[v].force();
    return f;
  }
  int loadedCount() const {
    int n = 0;
    for (bool l : loaded_) n += l;
    return n;
  }
  // The voice holding a key (-1: none).
  int voiceFor(int note) const {
    for (int v = 0; v < kVoices; ++v)
      if (loaded_[v] && voice_[v].key() == note) return v;
    return -1;
  }
  int heldCount() const { return nHeld_; }        // keys down (tests)
  int heldNote(int i) const { return held_[i]; }  // i < heldCount(), oldest first

  // Rules about the keys and the bows that always hold, however the keys
  // were played (the notes sounding needn't match the keys down: a slur, a
  // ringing string, a released note all differ). Returns the broken rules (0:
  // none) and a note involved. Cheap (no allocation): the tests check it
  // constantly and the synth once per control block.
  enum Audit : int {
    kAuditTooManyBowed = 1,   // more bows on strings than keys down
    kAuditKeyTwice = 2,       // two voices holding one key
    kAuditBowWithoutKey = 4,  // a bow on a string with no key down (and not a "stop" release)
    kAuditStopTooLong = 8,    // a "stop" release still on the string after kAuditStopMax
    kAuditNotHeld = 16,       // a note being bowed whose key is not down
    kAuditHeldTwice = 32,     // a key in the held list twice
  };
  static constexpr float kAuditStopMax = 2.f;  // s: a stop takes ~0.2 s (tremolo up to ~0.5)
  int audit(int& note, bool legato) const {
    int bad = 0;
    note = -1;
    auto held = [&](int n) {
      for (int i = 0; i < nHeld_; ++i)
        if (held_[i] == n) return true;
      return false;
    };
    int bows = 0;
    for (int v = 0; v < kVoices; ++v) {
      if (!loaded_[v]) continue;
      const Player& p = voice_[v];
      if (p.bowing() && !p.stopping()) ++bows;
      if (p.stopping() && p.stopTime() > kAuditStopMax) bad |= kAuditStopTooLong, note = p.note();
      if (p.bowing() && !p.stopping()) {
        if (nHeld_ == 0) bad |= kAuditBowWithoutKey, note = p.note();
        else if (!held(p.key())) bad |= kAuditNotHeld, note = p.note();
      }
      if (p.key() >= 0)
        for (int w = v + 1; w < kVoices; ++w)
          if (loaded_[w] && voice_[w].key() == p.key()) bad |= kAuditKeyTwice, note = p.key();
    }
    // (Legato plays one bow, but notes held from polyphonic playing keep
    // theirs when Legato comes on, until their keys go up.)
    (void)legato;
    if (bows > std::max(nHeld_, 1)) bad |= kAuditTooManyBowed;
    for (int i = 0; i < nHeld_; ++i)
      for (int j = i + 1; j < nHeld_; ++j)
        if (held_[i] == held_[j]) bad |= kAuditHeldTwice, note = held_[i];
    return bad;
  }

 private:
  static constexpr float kDoubleOn = 0.03f;  // s: a held key's repeat within this is the same press, arriving twice
  static constexpr int kSpare = 2;           // voices kept free: below this, the quietest released one is let go

  // The smoothed controls start at their first values (no glide from defaults).
  void prime(const PerformerControls& c) {
    if (primed_) return;
    sh_.dynIn = c.dynamics;
    sh_.pressureIn = c.pressure;
    sh_.contactIn = c.contact;
    primed_ = true;
  }

  void removeHeld(int note) {
    for (int i = nHeld_ - 1; i >= 0; --i)
      if (held_[i] == note) removeHeldAt(i);
  }
  void removeHeldAt(int i) {
    for (int j = i; j + 1 < nHeld_; ++j) {
      held_[j] = held_[j + 1];
      vel_[j] = vel_[j + 1];
      press_[j] = press_[j + 1];
    }
    --nHeld_;
  }

  // Every voice holding `note` lets go (except `except` and `except2`).
  void releaseKey(int note, float relVel, const PerformerControls& c, int except, int except2 = -1) {
    for (int v = 0; v < kVoices; ++v)
      if (v != except && v != except2 && loaded_[v] && voice_[v].key() == note) voice_[v].release(relVel, c);
  }

  // A voice for a new note: a free one, else the quietest released one (cut
  // short), else the oldest held. Then, if few are left free, the quietest
  // released voice starts fading out, so the next note finds one free.
  int allocate() {
    int best = -1;
    for (int v = 0; v < kVoices && best < 0; ++v)
      if (!loaded_[v]) best = v;
    if (best < 0) best = quietestReleased(false);
    if (best < 0) {
      float oldest = 1e30f;
      for (int v = 0; v < kVoices; ++v)
        if (startedAt_[v] < oldest) oldest = startedAt_[v], best = v;
    }
    voice_[best].claim();
    loaded_[best] = true;
    level_[best] = 0.f;
    startedAt_[best] = sh_.now;
    int free = 0;
    for (bool l : loaded_) free += !l;
    if (free < kSpare) {
      const int v = quietestReleased(true, best);
      if (v >= 0) voice_[v].steal();
    }
    return best;
  }
  int quietestReleased(bool skipDying, int skip = -1) const {
    int best = -1;
    float quiet = 1e30f;
    for (int v = 0; v < kVoices; ++v) {
      const Player& p = voice_[v];
      if (v == skip || !loaded_[v] || p.key() >= 0 || (skipDying && p.dying())) continue;
      const float l = p.dying() ? -1.f : level_[v];  // dying voices first
      if (l < quiet) quiet = l, best = v;
    }
    return best;
  }

  // The note's string, finger and tuning.
  NoteStart makeNote(int note, float vel, int s, int art, const PerformerControls& c) const {
    const InstrumentDef& def = *sh_.def;
    NoteStart ns;
    ns.note = note;
    ns.vel = vel;
    ns.string = s;
    ns.gap = gap_;
    ns.noteGap = noteGap_;
    float n = static_cast<float>(std::max(note, def.strings[0].openNote - 7));
    // Harmonics: play the note as a natural or artificial harmonic; the
    // finger stops the fundamental and a light touch picks the partial.
    if (art == kHarmonics) {
      int hs, stop;
      float q;
      if (findHarmonic(note, c.forcedString, hs, stop, q)) {
        ns.string = hs;
        n = static_cast<float>(stop);
        ns.touchQ = q;
      }
    }
    ns.open = static_cast<int>(n) == def.strings[ns.string].openNote;
    if (!ns.open) n += 0.01f * c.intonStyle * melodicCents(note);  // the sounding note's tuning
    ns.stop = n;
    // Vibrato by context (player doc §3.1): wider higher up; narrow in fast
    // passages (the time since the last key: under ~0.12 s, ~30%), opening up
    // over half a second if the note is held [T].
    ns.vibReg = std::clamp(1.f + 0.25f * static_cast<float>(note - def.hand.vibRefNote) / 12.f, 0.8f, 1.35f);
    ns.vibFast = std::clamp((0.28f - gap_) / 0.16f, 0.f, 1.f);
    return ns;
  }

  // Polyphonic: each key its own voice.
  void polyOn(int note, float vel, const PerformerControls& c) {
    const int art = c.articulation;
    const int s = chooseString(note, false, c);
    const NoteStart ns = makeNote(note, vel, s, art, c);
    int v = voiceFor(note);  // pressed again while its note sounds: restrike it
    if (v >= 0 && voice_[v].string() != ns.string) {
      voice_[v].release(64.f / 127.f, c);
      v = -1;
    }
    const bool restrike = v >= 0;
    if (!restrike) {
      // A string still ringing from a note let go is the same string on a
      // real instrument: the new note plays it (a fast passage re-bows a
      // moving string). Notes held together get strings of their own.
      v = releasedOn(ns.string);
      if (v >= 0) voice_[v].clearStop(), startedAt_[v] = sh_.now;
      else v = allocate();
      if (v != lead_) voice_[v].takePlace(voice_[lead_]);  // the bow's direction alternates from note to note
    } else {
      voice_[v].clearStop();
    }
    lead_ = v;
    Player& p = voice_[v];
    if (art == kPizzicato) {
      p.pluck(ns);
      return;
    }
    p.setArticulation(art);
    p.play(ns, c, false, restrike);
  }

  // Legato: one player.
  void monoOn(int note, float vel, const PerformerControls& c) {
    Player& L = voice_[lead_];
    const bool live = loaded_[lead_];
    // A note after a "stop" release is a new stroke from rest.
    const bool wasStopping = live && L.stopping();
    if (live) L.clearStop();
    if (c.articulation == kPizzicato) {
      const int s = chooseString(note, false, c);
      const NoteStart ns = makeNote(note, vel, s, kPizzicato, c);
      const int v = monoVoiceOn(ns.string);
      releaseKey(note, 64.f / 127.f, c, v, lead_);
      if (v != lead_ && live) L.leave(c);
      lead_ = v;
      voice_[v].pluck(ns);
      return;
    }
    // Martelé and spiccato strokes are separate by nature; tremolo and
    // harmonics slur like arco.
    const int art = c.articulation;
    const bool canSlur = art == kArco || art == kTremolo || art == kHarmonics;
    // Wider leaps than the slur range take a new bow.
    const bool close = live && L.note() >= 0 && std::abs(note - L.note()) <= static_cast<int>(std::lround(c.slurRange));
    const bool slur = live && L.bowing() && close && !wasStopping && canSlur && L.articulation() == art;
    monoPlay(note, vel, slur, slur ? L.articulation() : art, c);
  }

  // Legato: play `note` slurred or with a new stroke; on another string the
  // bow moves over.
  void monoPlay(int note, float vel, bool slur, int art, const PerformerControls& c) {
    const int s = chooseString(note, slur, c);
    const NoteStart ns = makeNote(note, vel, s, art, c);
    const int lead = lead_;
    const bool live = loaded_[lead];
    const int v = monoVoiceOn(ns.string);
    // A voice still holding this key from polyphonic playing lets go: the
    // player takes the note.
    releaseKey(note, 64.f / 127.f, c, v, lead);
    Player& p = voice_[v];
    const bool sounding = (v == lead && p.bowing()) || p.force() > 0.f;
    if (v != lead && live) {
      Player& L = voice_[lead];
      p.clearStop();
      if (L.bowing()) p.takeBow(L);
      else p.takePlace(L);
      L.leave(c);
    } else if (v != lead) {
      p.takePlace(voice_[lead]);
    }
    lead_ = v;
    p.setArticulation(art);
    p.play(ns, c, slur && p.bowing(), sounding);
  }

  // The newest voice on string s that no key holds (ringing on), or -1.
  int releasedOn(int s) const {
    int best = -1;
    for (int v = 0; v < kVoices; ++v) {
      const Player& p = voice_[v];
      if (loaded_[v] && p.string() == s && p.key() < 0 && !p.dying() && (best < 0 || startedAt_[v] > startedAt_[best])) best = v;
    }
    return best;
  }

  // Legato: the voice on string s (one per string, as on the instrument).
  int monoVoiceOn(int s) {
    if (loaded_[lead_] && voice_[lead_].string() == s && !voice_[lead_].dying()) return lead_;
    for (int v = 0; v < kVoices; ++v)
      if (loaded_[v] && voice_[v].string() == s && !voice_[v].dying() && voice_[v].key() < 0) return v;
    return allocate();
  }

  int chooseString(int note, bool slur, const PerformerControls& c) const {
    const InstrumentDef& def = *sh_.def;
    // A chosen string (keyswitch / String parameter) plays every note it
    // reaches: from its open note up two octaves.
    const int forced = c.forcedString;
    if (forced >= 0 && forced < 4) {
      const int open = def.strings[forced].openNote;
      if (note >= open && note <= open + 24) return forced;
    }
    const bool slow = gap_ >= 0.25f;
    if (slur) {
      const Player& L = voice_[lead_];
      const int cur = L.string();
      const int open = def.strings[cur].openNote;
      // Portamento (player doc §3.3): a player who slides keeps a slow,
      // slurred leap of a third or more on the string and shifts, from a
      // stopped note, up to an octave above the open string [T].
      if (c.portamento > 0.f && gap_ >= 0.3f && L.note() > open && std::abs(note - L.note()) >= 3 && note > open && note <= open + 12)
        return cur;
      // Otherwise stay on the current string if it reaches within 7 semitones
      // (and the note isn't its open string we'd rather stop on the string below).
      if (note >= open && note <= open + 7 && !(note == open && cur > 0 && wantsStopped(slow, c))) return cur;
    }
    int s = 0;
    for (int i = 0; i < 4; ++i)
      if (def.strings[i].openNote <= note) s = i;
    // An open string can't take vibrato: unless the playing is fast, a player
    // stops the note with the fourth finger on the string below (player doc
    // §3.1; PLAN "Ideas") [T].
    if (s > 0 && note == def.strings[s].openNote && wantsStopped(slow, c)) --s;
    return s;
  }
  bool wantsStopped(bool slow, const PerformerControls& c) const {
    return c.avoidOpen && slow && c.vibDepth > 0.f && (c.articulation == kArco || c.articulation == kMartele);
  }

  // A harmonic for `note`: natural (an open string's 2nd, 3rd or 4th
  // partial, touched at 1/2, 1/3, 1/4) if there is one, else artificial
  // (a stopped note two octaves, a twelfth or an octave below, touched at
  // 1/4, 1/3 or 1/2 of the stopped length), lowest position first.
  bool findHarmonic(int note, int forced, int& s, int& stop, float& q) const {
    static constexpr int kInterval[3] = {12, 19, 24};
    static constexpr float kQ[3] = {0.5f, 1.f / 3.f, 0.25f};
    int bestCost = 1 << 20;
    for (int str = 0; str < 4; ++str) {
      if (forced >= 0 && str != forced) continue;
      const int open = sh_.def->strings[str].openNote;
      for (int k = 0; k < 3; ++k) {
        const int st = note - kInterval[k];
        const int pos = st - open;
        if (pos < 0 || pos > 12) continue;
        // Natural first (cost by partial), then artificial, two-octave first.
        const int cost = pos == 0 ? k : 100 + pos * 4 + (2 - k);
        if (cost < bestCost) bestCost = cost, s = str, stop = st, q = kQ[k];
      }
    }
    return bestCost < (1 << 20);
  }

  // Intonation (player doc §3.2) ---------------------------------------
  // Players tune by fifths from the open strings (Pythagorean-leaning),
  // which raises leading tones in the usual keys and makes a note matching
  // an open string ring it. Each note's place on the chain of fifths depends
  // on the key, estimated from recent notes.
  void hearKey(int note) {
    // Krumhansl-Kessler major profile against a decaying pitch-class count
    // (~7 notes of memory); relative minors share the key signature.
    static constexpr float kProfile[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
    for (float& w : pcWeight_) w *= 0.85f;
    pcWeight_[((note % 12) + 12) % 12] += 1.f;
    int best = 2;
    float bestScore = -1.f;
    for (int t = 0; t < 12; ++t) {
      float sc = 0.f;
      for (int pc = 0; pc < 12; ++pc) sc += pcWeight_[pc] * kProfile[(pc - t + 12) % 12];
      if (sc > bestScore + 1e-4f) bestScore = sc, best = t;
    }
    int k = Player::chainOf(best);
    if (k > 5) k -= 12;
    keyCenter_ = k + 2;  // the middle of the major scale's span on the chain (-1..+5 from the tonic)
  }
  // Cents from equal temperament: Pythagorean, the note spelled nearest the
  // key (from 5 fifths flat to 6 sharp of its centre, so minor-key leading
  // tones are sharps). A Pythagorean fifth is 1.955 cents wide.
  float melodicCents(int note) const {
    int rel = ((Player::chainOf(((note % 12) + 12) % 12) - keyCenter_) % 12 + 12) % 12;
    if (rel > 6) rel -= 12;
    return 1.955f * static_cast<float>(keyCenter_ + rel);
  }

  PerformerShared sh_;
  Player voice_[kVoices];
  bool loaded_[kVoices]{};
  float level_[kVoices]{}, startedAt_[kVoices]{};
  int lead_ = 0;
  int held_[kMaxHeld]{};
  float vel_[kMaxHeld]{}, press_[kMaxHeld]{};
  int nHeld_ = 0;
  bool primed_ = false;
  float lastOn_ = -1.f, noteGap_ = 0.1f, gap_ = 1.f;
  float pcWeight_[12]{};
  int keyCenter_ = 1;  // D major until notes say otherwise
};

}  // namespace sm
