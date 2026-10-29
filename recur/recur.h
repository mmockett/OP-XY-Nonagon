#pragma once
// What the sound and the screen share: the maps, their recurrence periods,
// the permutation for any iteration, and how the controls set the iteration
// being played. Integer only, so display code can use it too.
#include <opxy/types.hpp>

namespace recur {
using opxy::i32;
using opxy::u32;

// The controls, by saved parameter index (definition.json).
enum Control : unsigned { kPos, kGrid, kSlice, kMap, kStep, kRate, kDepth, kChrd };
// The maps, in map's order: Arnold's cat, the baker's map, the golden map
// (Fibonacci's matrix, the cat's square root), Pell's (2 + sqrt 3), a shear and
// the order-six turn.
enum Map : unsigned { kCat, kBaker, kGold, kPell, kShear, kHex };
const u32 map_count = 6, grid_count = 3, slice_count = 6, rate_count = 8, chord_count = 7;
const u32 most_taps = 3;   // chrd stacks at most three readers of the window

// A control as 0..65535, and an enum's choice.
inline u32 level(i32 parameter) { return parameter < 0 ? 0u : u32(parameter) >> 15; }
inline u32 choice(i32 parameter, u32 count) { return (level(parameter) * count) >> 16; }

// grid: the window is 2, 4 or 8 slices a side, 2^bits.
inline u32 tile_bits(i32 grid) { return 1 + choice(grid, grid_count); }
// slice: each cell's length in 64th notes at the project's tempo, from 1/64
// to 1/4; or 0, hiss: cells of single samples, in a square 16 times the
// grid's side (32, 64 or 128), shredding the window to noise.
const u32 slice64[slice_count] = {0, 1, 2, 4, 8, 16};
const u32 fine_extra = 4, smallest_fine_bits = 4;
// rate: 64th notes an iteration, from 4 bars to 1/16; 0 holds the iteration step sets.
const u32 rate64[rate_count] = {0, 256, 128, 64, 32, 16, 8, 4};

// The square hiss asks for, or the largest that fits a region of span frames:
// 0 when even the smallest doesn't, and the region is silent.
inline u32 fitting_bits(u32 bits, u32 span) {
    while (bits >= smallest_fine_bits && (1u << (2 * bits)) > span) --bits;
    return bits >= smallest_fine_bits ? bits : 0;
}

// A 2 x 2 matrix mod 2^bits: (x, y) -> (a x + b y, c x + d y). For the baker's
// map, a holds the bits it turns its ring by.
struct Mat { u32 a, b, c, d; };

// Each linear map's matrix, its entries mod any 2^bits (-1 is the mask).
inline Mat matrix(u32 map, u32 mask) {
    switch (map) {
    case kGold: return Mat{0, 1, 1, 1};
    case kPell: return Mat{2, 3, 1, 2};
    case kShear: return Mat{1, 1, 0, 1};
    case kHex: return Mat{1, 1, mask, 0};
    default: return Mat{1, 1, 1, 2};   // the cat: (x + y, x + 2y)
    }
}

// Iterations until each map returns to where it started, by side (2 .. 128).
// tests/sound.cpp checks them against the maps themselves.
const unsigned short periods[map_count][7] = {
    {3, 3, 6, 12, 24, 48, 96},     // cat
    {2, 4, 6, 8, 10, 12, 14},      // baker: 2 bits
    {3, 6, 12, 24, 48, 96, 192},   // gold: the Pisano period
    {2, 4, 4, 8, 16, 32, 64},      // pell
    {2, 4, 8, 16, 32, 64, 128},    // shear: N
    {3, 6, 6, 6, 6, 6, 6},         // hex
};
inline u32 period(u32 map, u32 bits) { return periods[map][bits - 1]; }

inline Mat multiply(const Mat& p, const Mat& q, u32 mask) {
    return Mat{(p.a * q.a + p.b * q.c) & mask, (p.a * q.b + p.b * q.d) & mask,
               (p.c * q.a + p.d * q.c) & mask, (p.c * q.b + p.d * q.d) & mask};
}

// The map applied k times, mod 2^bits: a matrix power by squaring, or for the
// baker's map its ring turned k places.
inline Mat power(u32 map, u32 k, u32 bits) {
    if (map == kBaker) return Mat{k % (2 * bits), 0, 0, 0};
    const u32 mask = (1u << bits) - 1;
    Mat result = {1, 0, 0, 1}, base = matrix(map, mask);
    for (; k; k >>= 1) {
        if (k & 1) result = multiply(result, base, mask);
        base = multiply(base, base, mask);
    }
    return result;
}

// bits reversed, for the baker's map: its ring reads y's bits backwards.
inline u32 reversed(u32 v, u32 bits) {
    u32 r = 0;
    for (u32 i = 0; i < bits; ++i, v >>= 1) r = r << 1 | (v & 1);
    return r;
}

// Where the map sends cell (x, y), given power()'s matrix. The baker's map is
// the discrete baker's transformation: x doubles, its top bit drops into y's
// top, and y's lowest bit feeds x's bottom. As a ring of 2 bits bits (y's bits
// backwards, then x's), it turns by one.
inline void send(const Mat& m, u32 map, u32 bits, u32& x, u32& y) {
    const u32 mask = (1u << bits) - 1;
    if (map == kBaker) {
        const u32 total = 2 * bits, s = m.a;
        const u32 ring = reversed(y, bits) << bits | x;
        const u32 turned = ((ring << s) | (ring >> (total - s))) & ((1u << total) - 1);
        x = turned & mask;
        y = reversed(turned >> bits, bits);
        return;
    }
    const u32 nx = (m.a * x + m.b * y) & mask, ny = (m.c * x + m.d * y) & mask;
    x = nx;
    y = ny;
}

// Iterations are Q20 (a period of 384 fits 32 bits).
const u32 iteration_bits = 20;

// rate's iterations a second, Q8, at a tempo (BPM, Q1 tenths as the transport gives it).
inline u32 rate256(u32 rate64_, u32 bpm_x10) { return rate64_ ? bpm_x10 * 4096 / (600 * rate64_) : 0; }

// step: where in the period the iteration starts, Q20.
inline u32 step_position(i32 parameter, u32 period) { return (level(parameter) * period) << 4; }

// blend's crossfade: how far toward the next iteration the sound is, Q16, at
// fraction f (Q16) of the way through an iteration. blend 0 steps at once;
// blend 1 crossfades all the way through; between, the crossfade fills the
// last blend of each iteration. reciprocal is crossfade_reciprocal(blend).
inline u32 crossfade_reciprocal(u32 blend16) { return blend16 ? 0xffffffffu / blend16 : 0; }
inline u32 toward_next(u32 f, u32 blend16, u32 reciprocal) {
    if (!blend16 || f + blend16 <= 65536) return 0;
    const u32 w = ((f + blend16 - 65536) * reciprocal) >> 16;
    return w > 65536 ? 65536 : w;
}

// depth: which cells move. The map's orbits (the cells one cell visits as it
// iterates) move or stay whole, so every iteration is still a permutation and
// still comes round at the period. An orbit moves when the hash of its first
// cell falls under depth (Q16, 65536 all). For squares up to 8 a side: a bit
// a cell, in two words.
inline u32 orbit_hash(u32 cell) { return ((cell + 1) * 2654435761u) >> 16; }
inline void moving_cells(u32 map, u32 bits, u32 depth16, u32 (&moving)[2]) {
    const u32 side = 1u << bits, cells = side * side;
    const Mat one = power(map, 1, bits);
    moving[0] = moving[1] = 0;
    for (u32 c = 0; c < cells; ++c) {
        u32 x = c & (side - 1), y = c >> bits, first = c;
        for (u32 n = 0; n < 64; ++n) {
            send(one, map, bits, x, y);
            const u32 at = y * side + x;
            if (at == c) break;
            if (at < first) first = at;
        }
        if (orbit_hash(first) < depth16) moving[c >> 5] |= 1u << (c & 31);
    }
}
inline bool moves(const u32 (&moving)[2], u32 cell) { return (moving[cell >> 5] >> (cell & 31)) & 1; }
// depth as Q16, 65536 at full.
inline u32 depth16(i32 parameter) { const u32 v = level(parameter); return v + (v >> 15); }
}
