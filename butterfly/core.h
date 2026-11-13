#pragma once
#include <opxy/audio.hpp>
#include "harper.h"

namespace butterfly {
const u32 half_frames = 32;        // the bank runs at half the sample rate
const u32 halfband_taps = 7;       // odd-phase taps of the 27-tap half-band that doubles it back

// One note: a bank of 17 partials, one a state of the chain, behind its gate.
struct Voice {
    u32 phase[partials];       // Q32 cycles
    i32 amplitude[partials];   // the last block's, Q25
    i32 low[half_frames + 2 * halfband_taps];   // the bank at half rate, after the last block's tail
    // What each partial plays apart from the gate, ramping a step a block
    // toward where the last solve landed it (engine.cpp).
    u32 increment[partials];   // its pitch, Q32 a full-rate sample (held at the bank's ceiling)
    i32 level[partials];       // Q15, lifted to the suites' loudness
    i32 power[partials];       // the power of the gate it follows, 1 + tilt x place, Q12
    i32 dim[partials];         // its tilt's darkening less the make-up, log2, Q16
    i32 increment_step[partials], level_step[partials], power_step[partials], dim_step[partials];
    u32 ratio[partials];       // where its pitch last landed, over the note's, Q16
    u32 note;                  // the note's increment the pitches landed at
    // The solve in progress, a stage a block, and what it started from.
    Spectrum solving;
    Notes notes;
    Grid grid;
    i32 lambda, at, octaves, slope, makeup;
    u32 sum;                   // of the weights, between shape's halves
    i32 loudest;               // the loudest partial's level, Q15
    i32 sampled[5];            // flux, coupling, site, spread and tilt at the last solve's start
    u32 stage;                 // the next stage of the solve
    u32 still;                 // solves started since a control last moved
    u32 stale;                 // 1: solve whole before the next sound (a strike, or the gate lit again)
    i32 gate;                  // the vactrol's cell, Q30
    u32 blocks;                // since the strike
    u32 beat, beat_site;       // the wingbeat's phases, Q32 turns
    u32 strikes;               // notes this voice has struck: each scatters its phases its own way
    u32 struck;                // 1 when a note has started and not yet sounded
    u32 sounding;              // 1 while any partial is above silence
    u32 hush;                  // 1 while the last note fades in one block before a new strike
};

// Voices share nothing: the spectrum is read from the tables.
struct Shared {};
}
