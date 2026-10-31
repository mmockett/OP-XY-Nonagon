#pragma once
#include <opxy/audio.hpp>
#include "drink.h"

namespace cocoa {
// A mode: a sine whose phase increment glides and whose amplitude ramps
// linearly through each block. `ring` is its amplitude; `out` what the
// block ended at, after the guard that fades it out near the Nyquist rate.
struct Mode {
    u32 phase, inc;
    i32 ring, out;
};
// A bubble bursting at the surface: a Minnaert ring, falling by 2^-shift a sample.
struct Pop {
    u32 phase, inc, shift;
    i32 amp;
};
const u32 liquid_modes = 6, cup_modes = 3, pops = 2;   // two pops at once: a third would cost 1,100 instructions a block
struct Voice {
    Mode liquid[liquid_modes];   // the column's modes, gliding with the sound speed
    Mode cup[cup_modes];         // the cup's wall, fixed in hertz
    Pop pop[pops];
    i32 d[drink::classes];       // each bubble class's void fraction above its clearing, Q31
    i32 s[drink::classes];       // and the share of the column it has cleared, Q30
    i32 drop;                    // the pitch's fall at the last block's end, Q16 octaves
    i32 dc_in, dc_out;           // the output's DC blocker
    u32 tap_phase;               // the spoon's way to its next tap, Q32 cycles
    u32 taps;                    // taps so far in this note
    u32 seed;                    // random numbers: the pops, and where a mode at rest starts
    u32 seen;                    // the shared clock's tick this voice last rendered at
    u32 fresh;                   // 1 from start until the note's first block stirs
    u32 quiet;                   // 1 once a single tap has rung out
    u32 alive;                   // 1 from start to destroy: counted in Shared's alive
    u32 wait;                    // blocks before a chord's later note taps (a strum)
    u32 started_at;              // the shared tick at its start, to find its chord
    i32 share;                   // its share of the blow when a chord strikes together, Q15
    i32 foam;                    // the foam its stir raised, bursting away, Q15
    i32 slosh;                   // how hard the stirred drink sloshes, dying away, Q15
    u32 slosh_phase;             // the sloshing's phase, Q32 cycles
    i32 rate[drink::classes];    // the clearing rates for rated_fill and rated_fizz
    i32 rated_fill, rated_fizz;
    u32 rated;                   // 1 once rate holds them
};
// The drink every voice stirs: one mug a track. The latest stir's drink, and
// a clock (one tick a block while any voice renders) to clear it by.
struct Shared {
    u32 tick, stirred_at, stirs, poured;
    u32 leader;   // the newest note's voice, whose taps ring the cup
    u32 alive;    // voices between start and destroy
    u32 started_at, strum;   // the tick the latest note started at, and its place in a chord
    i32 d[drink::classes], s[drink::classes];
};
}
