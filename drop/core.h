#pragma once
#include <opxy/audio.hpp>
#include "leidenfrost.h"

namespace drop {
using opxy::i16;
const u32 pipe_length = 2048;   // the longest pipe, 2040 samples: notes under 21.6 Hz play an octave up

// One voice: one drop's life, its beats, its crackle and the pipe its vapour
// sounds, tuned to the key.
struct Voice {
    physics::Drop drop;
    u32 fresh;          // 1 from a note's start to its first block, when the drop lands
    u32 quiet;          // 1 once the drop is gone and the pipe has rung out
    u32 counted;        // 1 while it counts among the voices sounding (Shared)
    i32 share;          // its level for the voices sounding, Q15
    u32 phase;          // the beats' phase, Q32 cycles: a beat at each wrap
    u32 vent;           // the vents' phase within a beat
    u32 vents;          // vents still to open in this beat (one a lobe)
    u32 beats;          // beats so far: alternate ones are a little softer
    u32 accent;         // 1 when the next beat comes as it lands from a hop
    i32 puff;           // this beat's strength, Q15
    i32 jet;            // the vapour jet's envelope, Q30
    i32 spark;          // the crackle's envelope, Q30
    i32 pulse;          // the pulses' envelope, Q15: the tone sounds while the vapour vents
    i32 soft_crack, softer_crack;   // the crackle as heard, low-passed twice
    i32 settled;        // the pipe's mean, Q10: its offset, taken out before the gate
    i32 hold, held;     // the loop's DC blocker: its state (Q10) and its last input
    i32 jet_lp;         // the jet's answer, low-passed at 2.5 times the key
    i32 lowest;         // the pipe's sound under an eighth of the key, Q10: the jet does not blow it
    u32 splash;         // blocks left of the landing's spray
    u32 kick;           // samples left of the landing's push into the pipe
    i32 push;           // the push, Q15
    u32 noise, dice;    // random numbers
    // The pipe: a delay of one period, its fractional part an allpass, and a
    // three-tap loop filter of exactly one sample's delay.
    u32 write;
    i32 ap_in, ap_out, f1, f2;
    i32 e1, e2, e3;     // the jet's low-passes into the pipe, and its low end taken out
    i32 whole;          // the delay's whole samples
    i32 eta;            // the allpass's coefficient, Q15
    i32 period;         // log2 of the delay, samples Q16
    i32 length;         // the delay, Q16 samples, before the jet's lag is taken off
    u32 tuned;          // the pitch increment the pipe was tuned for, or 0
    i32 crack_last;     // the crackle's previous sample (it is heard through a difference)
    i32 hiss_last;      // the frying hiss, low-passed noise
    i32 dc_in, dc_out;
    i16 pipe[pipe_length];
};
// State shared by every voice.
struct Shared {
    u32 sounding;       // voices sounding: from three on, each plays a little quieter
};
}
