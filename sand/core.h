#pragma once
#include <opxy/audio.hpp>
#include "sand.h"

namespace sand {
// One plate mode in phasor form: its oscillator phase and its complex
// amplitude, a (in phase with the bow) and b (quadrature), each an amplitude
// times 65536, at most 4095 * 65536; (sa, sb) is the amplitude last sounded,
// so the next block ramps on from it.
struct Mode { u32 phase; i32 a, b, sa, sb; };

// The rates (engine.cpp): the half-band stage from an eighth to a quarter
// keeps 2K - 1 = 7 inputs of history and delays by K = 4 of its samples (32 at
// the full rate); the stages to a half and to the full rate keep 5 and delay
// by 3 (12 and 6). So the quarter rate's sum waits 8 of its samples, the half
// rate's 22 and the full rate's, and the rattle, 50.
const u32 kBlock = 64, kHistory4 = 7, kHistory3 = 5, kQuarterWait = 8, kHalfWait = 22, kFullWait = 50;

struct Voice {
    Mode modes[kModes];   // the figure's mode and six of the plate's modes above it
    Mode twin;            // the bowed mode's near-degenerate twin: its amplitude, and the beat's phase
    Mode harmonic;        // the bow's stick and slip, its harmonics: their amplitude (a) and last sounded (sa)
    Grain grains[kGrains];
    i16 shapes[kGrains][kModes];  // each grain's mode shapes where it lies (Q15), shapes[i][0] kMoved once it moves
    i32 load[kModes];     // the sand's weight on each mode: the sum over grains of shape^2 (Q15)
    i32 gather[kModes];   // the same, gathered over the round of looks in progress
    i32 controls[8];      // smoothed controls, 0..32767
    u32 primed;           // controls hold real values; the first block snaps them
    u32 figure;           // the figure the modes were last bowed in
    u32 cursor;           // the next grain to look at
    u32 seed;
    u32 struck;           // the note has just started: strike the plate this block
    u32 jolted;           // looks left of the strike's jolt
    u32 rounds;           // rounds of looks, for publishing the sand every fourth
    i32 click1, click2;   // the grain click filter's last two outputs
    i32 lost[kModes];     // each mode's log2 loss its kept and stirs were made for (0: none)
    i32 kept[kModes];     // keep(lost), Q31
    u32 stirs[kModes];    // stir()'s factor for lost, Q16
    // The modes are sounded at the lowest of four rates that holds them
    // (engine.cpp): an eighth, a quarter, a half or the full sample rate.
    // Half-band stages bring the slower sums back to the full rate, so the
    // faster paths and the rattle wait as long as those stages delay the slow ones.
    u8 rate[kModes];      // each mode's rate, 1 / 2^rate
    u8 live;              // bits 0-3: rate 2^-r written last block; 4, 5: the quarter and
                          // half stages had input; 6: a landing waits
    i32 eighth[kHistory4 + kBlock / 8];    // the eighth-rate sum, after the last block's tail
    i32 quarter[kHistory3 + kBlock / 4];   // the quarter-rate stage's input, after its tail
    i32 half[kHistory3 + kBlock / 2];      // the half-rate stage's input, after its tail
    i32 quarter_wait[kQuarterWait];        // each rate's sum still to come (tails)
    i32 half_wait[kHalfWait];
    i32 full_wait[kFullWait];
    i32 click_wait[kFullWait];             // landings still to come
};
// The plate's sand as the latest note left it. A note started while another
// still sounds takes over that sand, so a bowed legato line keeps its figure
// instead of pouring fresh sand on every note; a note with nothing sounding
// pours fresh sand. Bounded: one copy of the grains.
struct Shared {
    Grain sand[kGrains];
    u32 latest;   // the owner whose sand this is, 0 when none sounds
    u32 clock;    // renders by every voice
    u32 fresh;    // the clock when latest last rendered
};
}
