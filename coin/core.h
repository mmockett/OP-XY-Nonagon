#pragma once
#include <opxy/audio.hpp>
#include "spin.h"

namespace coin {
// A resonance: y = 2r cos w y1 - r^2 y2 + gain x.
struct Mode {
    i32 c1, c2;        // 2r cos w (Q30) and r^2 (Q31)
    i32 gain;          // the input's gain: the mode's level x sin w, Q31
    i32 y1, y2;
};
// One disk: where its contact point is, how much of its spin is left, and its
// ring, its first mode. Its upper modes are the voice's, shared by its disks.
struct Disk {
    u32 phase;         // the contact point's way round, Q32 cycles: a strike at each wrap
    u32 turn;          // the disk's own turn, which carries its eccentric weight round
    u32 strikes;       // contact strikes so far (the phase's whole cycles)
    i32 life;          // the share of its spin left, Q30
    i32 carry;         // the part of a strike that falls in the next sample
    u32 rolling;       // 1 while it rolls; 0 once it has left the surface and lies flat
    u32 silent;        // 1 once it lies flat and has rung out
    u32 hush;          // blocks in a row it has ended under -84 dB, lying flat
    i32 cosine;        // the ring's cos w, Q31, to damp it each block
    Mode ring;
};
struct Voice {
    Disk disk[spin::most];
    Mode upper[2];     // the disks' second and third modes, tuned to the first disk
    i32 upper_cosine[2];
    // The first disk's ring has a partner a little apart, as a disk that is
    // not quite round does: the pair beats slowly.
    Mode partner;
    i32 partner_cosine;
    i32 partner_level;   // its input gain (level x sin w) at its strongest, Q31
    u32 upper_flat;    // 1 once every disk lies flat
    Mode base;         // the base's resonance
    u32 count;         // disks in this note
    u32 seed;          // noise
    u32 sway;          // the hand's slow wobble while it keeps the disks spinning, Q32 cycles
    i32 hand;          // Q15: 1 while the disks are kept spinning, easing to 0 when let go
    i32 rough;         // the rolling noise, low-passed
    i32 last;          // the previous noise sample (the slip's hiss is its difference)
    i32 dc_in, dc_out; // the output's DC blocker
    u32 tuned;         // the pitch increment the disks' modes were tuned for, or 0
    u32 material;      // and the disk material
    u32 ground;        // the base (material), to know when it changed
    u32 quiet;         // 1 once every disk and the base have rung out
};
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
