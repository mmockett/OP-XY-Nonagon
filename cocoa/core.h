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
const u32 liquid_modes = 6, cup_modes = 3, pops = 2;
// The rates (engine.cpp): the half-band stage from an eighth to a quarter
// keeps 2K - 1 = 7 inputs of history and delays by K = 4 of its samples (32 at
// the full rate); the stages to a half and to the full rate keep 5 and delay
// by 3 (12 and 6). So the quarter rate's sum waits 8 of its samples, the half
// rate's 22 and the full rate's 50.
const u32 kBlock = 64, kHistory4 = 7, kHistory3 = 5, kQuarterWait = 8, kHalfWait = 22, kFullWait = 50;   // two pops at once: a third would cost 1,100 instructions a block
// A mode's rates (1 / 2^r of the sample rate): its own (dst) and, while it
// fades to it over `left` more blocks, the one it is leaving (src), which
// holds the rest of its amplitude: the own rate's share ended the last block at kept.
const u32 fade_blocks = 8;
struct Band {
    opxy::u8 dst, src, left, spare;
    opxy::i32 kept;
};
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
    i32 ratio[liquid_modes];     // each liquid mode's ratio for filled_fill (log2, Q16)
    u32 liquid_loss[liquid_modes];   // and its loss once clear, for filled_cup
    i32 cup_log2[cup_modes];     // the wall's modes for filled_fill and filled_cup: log2 increments,
    u32 cup_inc[cup_modes];      // increments
    i32 cup_loss[cup_modes];     // and losses
    i32 filled_fill;
    u32 filled_cup, filled;      // filled: 1 once these hold them
    // The modes are sung at the lowest of four rates that holds them
    // (engine.cpp): an eighth, a quarter, a half or the full sample rate.
    // Half-band stages bring the slower sums back to the full rate, so the
    // faster sums and the pops wait as long as those stages delay the slow ones.
    Band band[liquid_modes + cup_modes];   // each mode's rates
    opxy::u8 live;                             // bits 0-3: rate 2^-r written last block; 4, 5: the quarter and half stages had input
    opxy::u8 spare[2];
    i32 eighth[kHistory4 + kBlock / 8];  // the eighth-rate sum, after the last block's tail
    i32 quarter[kHistory3 + kBlock / 4]; // the quarter-rate stage's input, after its tail
    i32 half[kHistory3 + kBlock / 2];    // the half-rate stage's input, after its tail
    i32 quarter_wait[kQuarterWait];      // each rate's sum still to come (tails)
    i32 half_wait[kHalfWait];
    i32 full_wait[kFullWait];
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
