#pragma once
// The track's stock amp envelope, followed on the screen (SDK 1.10). One
// helper for every Nonagon synth's screen: each machine folder holds a
// byte-identical copy of tools/amp.h, since a machine builds from its own
// folder alone. Edit tools/amp.h and copy it over (AGENTS.md).
//
// A synth voice hears its note start but not its release: its own life (a
// spin, a flyby, a drop) runs on under the OP-XY's amp envelope, which ends
// the sound. The screen gets the envelope's four controls (input.amp) and
// the track's note activity, and runs the same envelope here, so a note's
// motion swells with the attack, holds at the sustain level while notes are
// held and fades over the release after the last note-off: the motion lasts
// as long as the sound.
//
// Stock maps its envelope controls to times along curves it doesn't
// publish. This helper assumes one curve for attack, decay and release: about
// 2 ms at 0, rising with the cube of the control to about 10 s at full
// (a quarter of the way is about 0.16 s, half about 1.25 s, three quarters
// about 4.2 s). Sustain is a level, linear in its control. The stages here
// are straight ramps: attack from the current level up to full over the
// attack time, decay from full to the sustain level over the decay time, and
// release from wherever it starts to nothing over the release time, so the
// motion ends when the release does. Measure stock and change only
// stage_time() if the curve turns out otherwise.
//
// When the envelope is unknown (an older OS, the builder's preview) or the
// note activity is, follow() returns false and the screen keeps its own
// timing, exactly as before.
//
// Every function is static: each machine's copy stays local to its screen,
// so machines built into one firmware never share (or clash over) a symbol.
#include <opxy/draw.hpp>

namespace nonagon_amp {
typedef opxy::i32 i32;
typedef opxy::u32 u32;

// The envelope's level runs 0..full (Q20); level() gives it as Q15.
static const i32 full = 1 << 20;
enum Stage : u32 { idle = 0, attack = 1, decay = 2, release = 3 };

// A screen's copy of the envelope: zero it, or reset() it, in display_reset.
// 16 bytes in the declared stateBytes.
struct Follower {
    i32 level;   // Q20, 0..full
    i32 from;    // the level the release started from, Q20
    u32 stage;   // Stage
    u32 known;   // 1 while the last frame followed the envelope
};

static inline void reset(Follower& f) { f.level = f.from = 0; f.stage = idle; f.known = 0; }

// A stage's time from its control (a Q31 position, as input.amp holds it),
// in Q10 seconds: about 2 ms + 10 s x position^3 (the assumption above).
static inline i32 stage_time(i32 position) {
    const i32 p = (position < 0 ? 0 : position) >> 16;  // Q15
    const i32 cube = ((p * p >> 15) * p) >> 15;         // Q15
    return 2 + (cube * 10240 >> 15);                    // Q10 s: 10 s is 10240
}

// The share of a stage of `time` (Q10 s) that dt (Q10 s) covers, times `span` (Q20).
static inline i32 ramp_step(i32 span, i32 dt, i32 time) {
    if (dt >= time) return span;
    return static_cast<i32>((static_cast<u32>(span >> 4) * static_cast<u32>(dt)) / static_cast<u32>(time)) << 4;
}

// Runs one frame of the envelope. `started` is true when a note started
// since the last frame (the machine's own note_serial check). Returns true
// when the envelope and the note activity are both known and the frame
// followed them; false leaves the follower idle and the screen on its own
// timing.
static inline bool follow(Follower& f, const opxy::draw::Input& input, bool started) {
    if (!opxy::draw::envelope_known(input.amp) || input.held_notes > 128) {
        reset(f);
        return false;
    }
    f.known = 1;
    const bool held = input.held_notes != 0;
    const i32 dt = opxy::draw::clamp(input.elapsed, 0, 256);
    // A new note, or keys held while the envelope has let go, starts the
    // attack from where the level is.
    if (started || (held && (f.stage == idle || f.stage == release))) f.stage = attack;
    if (f.stage == attack) {
        f.level += ramp_step(full, dt, stage_time(input.amp.attack));
        if (f.level >= full) {
            f.level = full;
            f.stage = decay;
        }
    } else if (f.stage == decay) {
        const i32 sustain = input.amp.sustain >> 11;  // Q20
        const i32 step = ramp_step(full - sustain, dt, stage_time(input.amp.decay));
        f.level = f.level - step < sustain ? sustain : f.level - step;
    } else if (f.stage == release) {
        f.level -= ramp_step(f.from, dt, stage_time(input.amp.release));
        if (f.level <= 0) {
            f.level = 0;
            f.stage = idle;
        }
    }
    // The last key up (or a note too short to see held) lets go: the
    // release runs from this level.
    if (!held && (f.stage == attack || f.stage == decay)) {
        f.stage = release;
        f.from = f.level;
        if (!f.level) f.stage = idle;
    }
    return true;
}

// The level, Q15 (0..32768).
static inline i32 level(const Follower& f) { return f.level >> 5; }
// The level in `steps` even steps (0..steps), rounding up so a sounding note
// never reads as 0: a screen redraws a fade only as it crosses a step.
static inline i32 level_steps(const Follower& f, i32 steps) {
    return f.level <= 0 ? 0 : static_cast<i32>((static_cast<u32>(f.level) * static_cast<u32>(steps) + (full - 1)) >> 20);
}
// True while the envelope sounds: from a note's start to its release's end.
static inline bool sounding(const Follower& f) { return f.stage != idle; }
// True while the last frame followed a known envelope.
static inline bool known(const Follower& f) { return f.known != 0; }
}
