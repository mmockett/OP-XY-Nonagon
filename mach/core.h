#pragma once
#include <opxy/audio.hpp>
#include "flight.h"

namespace mach {

// Times and distances in the engine are Q26 sound-seconds (+-32 s, 15 ns).
const i32 substep = 64;               // samples between root solves
const u32 substep_bits = 6;
const i32 substep_time = 97391;       // 64 / 44100 s, Q26

// One way the sound reaches the listener: straight from the object, or off the
// ground (an image of the flight mirrored under it).
struct Image {
    i32 tau0;          // the image's closest pass, Q26 (0 for the direct sound)
    i32 d;             // its closest distance, Q26
    i32 level;         // Q30: 1 straight, the ground's reflection for the image
    u32 cone;          // 1 when its Mach cone leaves the object inside the flight
    i32 sigma_star;    // where the cone leaves, local to the image (sigma - tau0), Q26
    i32 s_star;        // when it arrives, local (t - tau0), Q26
    i32 fold;          // near the cone (sigma - sigma*)^2 = fold (s - s*), Q16 seconds
    i32 onset;         // 1 / how long its images take to come in at the cone, Q16 per second ...
    i32 rise;          // ... and that time, Q26
    i32 birth;         // listener time its first sound arrives, Q26
    i32 birth_step;    // without a cone: the first sub-step's change in emission time, Q26
    i32 boom_at;       // sample of the boom from note-on (-1: none)
    i32 boom_length;   // the N-wave's length in samples
    i32 boom_scale;    // Q30: how loud this image's boom is at full boom
    i32 boom_value, boom_drop;   // the N-wave as it plays: Q31 now, and its fall a sample
};

// One retarded time: a root of t = tau + |r(tau)| that the voice tracks.
struct Root {
    i32 sigma;         // the emission time heard now, Q26
    i32 step;          // its change over the last sub-step, Q26 (negative: heard backwards)
    i32 bend;          // ... and how much that changed from the sub-step before
    u32 phase;         // the source's phase, Q32 cycles
    i32 inc;           // phase a sample at the sub-step's start (negative backwards) ...
    i32 inc_to;        // ... and at its end
    i32 gain, slope;   // Q30, and its change a sample
    i32 low, coef;     // the air's one-pole low-pass: state and Q31 coefficient
    u32 level;         // wave sources: the table level ...
    i32 mix;           // ... and the crossfade toward the next, Q20, at the sub-step's start ...
    i32 mix_to;        // ... and at the sub-step's end
    u32 clock;         // bell: where in its strike period, Q32, at the sub-step's end
    u32 part[4];       // bell: the hum's phase (the prime and nominal are 2 and 4 times it) and the tierce's (part[2]), Q32; pad: part[0] is its second, sharper oscillator
    i32 part_inc[4];   // ... a sample now
    i32 part_to[4];    // ... and at the sub-step's end
    i32 part_amp[4];   // bell partial / voice formant amplitudes over the sub-step, Q15 ...
    i32 amp_was[4];    // ... and over the one before
    u32 sweeping;      // 1 while an amplitude or pitch changes fast: ramped sample by sample
    u32 part_end[4];   // bell: each partial's phase at the end of a period (heard backwards)
    u32 offset[4];     // bell: each partial's phase at a strike, hashed from the key so a chord's strikes don't add up in step
    u32 ratio[3];      // voice: each formant over the fundamental, Q16
    u32 state;         // waiting, fold, tracking, done
    u32 reversed;      // 1 for the root that runs back through the flight
    u32 image;         // 0 straight, 1 off the ground
    i32 speed;         // |dtau/dt| over the last sub-step, Q24
};
enum RootState { WAITING, FOLD, TRACKING, DONE };

struct Voice {
    Image image[2];
    Root root[4];      // straight forward, straight reversed, ground forward, ground reversed
    i32 mach;          // Q28
    i32 sigma_start, sigma_end;   // the flight, Q26
    i32 fade, fade_out;   // 1 / the window's fade in and fade out, Q16 per second ...
    i32 fade_span, fade_out_span;   // ... and their lengths, Q26
    i32 log_d;         // log2 of the closest distance (Q26), Q16: the distance law's reference
    i32 t;             // listener time now, Q26
    i32 t_start;       // ... at note-on
    u32 samples;       // since note-on
    u32 source;
    i32 loud;          // Q30: closer passes louder
    i32 pitch;         // the source's pitch over the key, Q16: tune puts the chosen moment on the key
    i32 boom_rise;     // the boom's rise, a one-pole coefficient, Q31: low a thump, high a crack
    i32 boom_gain;     // ... and its level, Q30, made up for what the rise takes away
    i32 period;        // bell strikes, siren wind-ups, the voice's vowel: 1 / period, Q16 per second
    u32 period_samples;   // the bell's first ring (from the flight's start to its pass) in samples
    i32 ring;          // 1 / the bell's second ring (from the pass to the flight's end), Q16 per second
    i32 boom_low, boom_lower;   // the booms' two low-passes (shock rise, air)
    i32 boom_r;        // the distance the straight boom left from, Q26
    i32 boom_end;      // sample after which no boom sounds
    i32 boom_share;    // Q15: this voice's share of a boom heard with others (1 / how many at once)
    u32 boom_held;     // 1 while this voice is counted among the booms in progress
    i32 dc_in, dc_out; // the output's DC blocker
    u32 fresh;         // 1 until the first block after note-on sets the flight up
    u32 done;
};

// Shared by every voice: how many booms are in progress. Every note of a chord
// flies the same path, so its booms land on the same sample and would add up
// whole; the k-th at once plays at 1/k (a chord of eight booms adds to 2.7).
struct Shared {
    u32 booming;
};
}
