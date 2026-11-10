#pragma once
#include <opxy/audio.hpp>
#include "harper.h"

namespace butterfly {
const u32 half_frames = 32;        // the bank runs at half the sample rate
const u32 halfband_taps = 7;       // odd-phase taps of the 27-tap half-band that doubles it back
const u32 side_tail = 10;          // the side's half-rate samples kept from the last block
const u32 side_floor = 3;          // the lean's floor: this many halvings of the note's power

// One note: a bank of 17 partials, one a state of the chain, behind its gate.
struct Voice {
    u32 phase[partials];       // Q32 cycles
    i32 amplitude[partials];   // the last block's, Q25
    i32 low[half_frames + 2 * halfband_taps];   // the bank at half rate, after the last block's tail
    i32 side[side_tail];       // the side's tail
    u32 leaned;                // 1 when the last block's odd states rang
    i32 balance;               // how far the first odd bus leans, Q16, at the end of the last block
    i32 width;                 // how far the wingbeat had opened the width then, Q16
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
