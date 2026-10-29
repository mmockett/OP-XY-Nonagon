#pragma once
#include <opxy/audio.hpp>
#include "sand.h"

namespace sand {
// One plate mode in phasor form: its oscillator phase and its complex
// amplitude, a (in phase with the bow) and b (quadrature), each an amplitude
// times 65536, at most 4095 * 65536; (sa, sb) is the amplitude last sounded,
// so the next block ramps on from it.
struct Mode { u32 phase; i32 a, b, sa, sb; };

struct Voice {
    Mode modes[kModes];   // the figure's mode and six of the plate's modes above it
    Mode twin;            // the bowed mode's near-degenerate twin: its amplitude, and the beat's phase
    Mode harmonic;        // the bow's stick and slip, its harmonics: their amplitude (a) and last sounded (sa)
    Grain grains[kGrains];
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
