#pragma once
#include <opxy/audio.hpp>
#include "harper.h"

namespace butterfly {
const u32 half_frames = 32;        // the bank runs at half the sample rate
const u32 halfband_taps = 7;       // odd-phase taps of the 27-tap half-band that doubles it back
// Low partials sound at a quarter or an eighth of the rate (engine.cpp), and
// half-band stages bring them up: from an eighth to a quarter (K = 4 taps,
// keeping 2K - 1 = 7 inputs of history, 4 of its samples late) and from a
// quarter to a half (K = 3, keeping 5, 3 late). So the quarter rate's sum
// waits 8 of its samples to join the eighth's, and the half rate's 22.
const u32 eighth_history = 7, quarter_history = 5, quarter_wait = 8, half_wait = 22;

// One note: a bank of 17 partials, one a state of the chain, behind its gate.
struct Voice {
    // Read and written every block, so kept small and together: the voice's
    // state is fetched afresh each block.
    u32 phase[partials];       // Q32 cycles
    // What each partial plays apart from the gate, ramping a step a block
    // toward where the last solve landed it (engine.cpp).
    u32 increment[partials];   // its pitch, Q32 a full-rate sample (held at the bank's ceiling)
    i32 increment_step[partials];
    u16 amplitude[partials];   // the last block's, Q15 (lifted, to 1.57)
    u16 level[partials];       // Q15, lifted to the suites' loudness
    i16 power[partials];       // the power of the gate it follows, 1 + tilt x place, Q12
    i16 dim[partials];         // its tilt's darkening less the make-up, log2, Q12
    i16 level_step[partials], power_step[partials], dim_step[partials];
    u8 rate[partials];         // each partial's rate, 2^-rate of the full: 1 a half, 2 a quarter, 3 an eighth
    i32 low[half_frames + 2 * halfband_taps];   // the bank at half rate, after the last block's tail
    i32 eighth[eighth_history + half_frames / 4];      // the eighth rate's sum, after the last block's tail
    i32 quarter[quarter_history + half_frames / 2];    // the quarter-rate stage's input, after its tail
    i32 quarter_late[quarter_wait];                    // the quarter rate's sum still to come
    i32 half_late[half_wait];                          // the half rate's sum still to come
    u32 high;                  // blocks a half-rate partial's sum still reaches the stage to the full rate
    u32 quarter_live;          // 1 when a quarter-rate partial sounded last block (its sum waits)
    i32 gate;                  // the vactrol's cell, Q30
    u32 blocks;                // since the strike
    u32 beat, beat_site;       // the wingbeat's phases, Q32 turns
    u32 sounding;              // 1 while any partial is above silence
    u32 stage;                 // the next stage of the solve
    u32 stale;                 // 1: solve whole before the next sound (a strike, or the gate lit again)
    u32 note;                  // the note's increment the pitches landed at
    u32 struck;                // 1 when a note has started and not yet sounded
    u32 hush;                  // 1 while the last note fades in one block before a new strike
    // Read only at a landing, a new note or a strike.
    u32 ratio[partials];       // where its pitch last landed, over the note's, Q16
    // The solve in progress, a stage a block, and what it started from.
    Spectrum solving;
    Notes notes;
    Grid grid;
    i32 lambda, at, octaves, slope, makeup;
    u32 sum;                   // of the weights, between shape's halves
    i32 loudest;               // the loudest partial's level, Q15
    i32 sampled[5];            // flux, coupling, site, spread and tilt at the last solve's start
    u32 still;                 // solves started since a control last moved
    u32 strikes;               // notes this voice has struck: each scatters its phases its own way
};

// Voices share nothing: the spectrum is read from the tables.
struct Shared {};
}
