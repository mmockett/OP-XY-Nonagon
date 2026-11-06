#pragma once
// The track's stock amp envelope, followed on the screen (SDK 1.10). One
// helper for every Nonagon synth's screen: each machine folder holds a
// byte-identical copy of tools/amp.h, and of tools/amp_curve.h beside it,
// since a machine builds from its own folder alone. Edit tools/ and copy
// both over (AGENTS.md).
//
// A synth voice hears its note start but not its release: its own life (a
// spin, a flyby, a drop) runs on under the OP-XY's amp envelope, which ends
// the sound. The screen gets the envelope's four controls (input.amp) and
// the track's note activity, and runs the same envelope here, so a note's
// motion swells with the attack, holds at the sustain level while notes are
// held and fades over the release after the last note-off: the motion lasts
// as long as the sound.
//
// The times and shapes are stock OS 1.2.1's, from amp_curve.h. A larger
// attack or decay is a longer stage, but a larger release is a shorter one
// (a release of 100 is stock's shortest, about 6 ms). The attack rises
// concavely, fast at first and slower at the top, from the current level;
// the decay falls exponentially and stops at the sustain level plus 1%; the
// sustain is a linear level; and the release fades exponentially from
// wherever it starts to -66 dB, where the motion ends.
//
// When the envelope is unknown (an older OS, the builder's preview) or the
// note activity is, follow() returns false and the screen keeps its own
// timing, exactly as before.
//
// Every function is static: each machine's copy stays local to its screen,
// so machines built into one firmware never share (or clash over) a symbol.
// 32-bit arithmetic only, so the firmware needs no arithmetic runtime.
#include <opxy/draw.hpp>

namespace nonagon_amp {
#include "amp_curve.h"
typedef opxy::i32 i32;
typedef opxy::u32 u32;

// The envelope's level runs 0..full (Q20); level() gives it as Q15.
static const i32 full = 1 << 20;
static const i32 peak = attack_peak_q15 << 5;   // 0.99: the attack's end
static const i32 cut = 524;                      // 0.0005, -66 dB: the release's end
enum Stage : u32 { idle = 0, attack = 1, decay = 2, release = 3, sustain = 4 };

// A screen's copy of the envelope: zero it, or reset() it, in display_reset.
// 16 bytes in the declared stateBytes.
struct Follower {
    i32 level;   // Q20, 0..full
    u32 spare;   // keeps the declared 16 bytes
    u32 stage;   // Stage
    u32 known;   // 1 while the last frame followed the envelope
};

static inline void reset(Follower& f) { f.level = 0; f.spare = 0; f.stage = idle; f.known = 0; }

// A Q20 level (0 .. 2 full) times a Q15 factor.
static inline i32 scaled(i32 level, u32 factor) {
    const u32 l = static_cast<u32>(level);
    return static_cast<i32>((l >> 15) * factor + (((l & 0x7fffu) * factor) >> 15));
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
    const u32 us = static_cast<u32>(opxy::draw::clamp(input.elapsed, 0, 256)) * 15625u >> 4;   // Q10 s to us
    const i32 top = sustain_q15(input.amp.sustain) << 5;   // Q20
    // A new note, or keys held while the envelope has let go, starts the
    // attack from where the level is.
    if (started || (held && (f.stage == idle || f.stage == release))) f.stage = attack;
    if (f.stage == attack) {
        // L(u) = 2 (1 - 0.505^u): what is left below 2 shrinks by the attack's factor.
        f.level = 2 * full - scaled(2 * full - f.level, attack_frame_q15(input.amp.attack, us));
        if (f.level >= peak) {
            f.level = full;
            f.stage = decay;
        }
    } else if (f.stage == decay) {
        f.level = scaled(f.level, decay_frame_q15(input.amp.decay, us));
        if (f.level <= top + full / 100) {
            f.level = top;
            f.stage = sustain;
        }
    } else if (f.stage == sustain) {
        f.level = top;
    } else if (f.stage == release) {
        f.level = scaled(f.level, release_frame_q15(input.amp.release, us));
        if (f.level < cut) {
            f.level = 0;
            f.stage = idle;
        }
    }
    // The last key up (or a note too short to see held) lets go: the
    // release runs from this level.
    if (!held && (f.stage == attack || f.stage == decay || f.stage == sustain)) {
        f.stage = release;
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
