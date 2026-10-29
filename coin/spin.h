#pragma once
#include "tables.h"

// Coin's physics, shared by the engine and the screen, so the screen draws
// what the voices play without hearing them. Integer and 32-bit only.
//
// A disk spun on its edge settles: its tilt falls as (t0 - t)^p and the
// contact point circles at a rate Omega with Omega^2 * tilt constant, so
// Omega rises as (t0 - t)^(-n), n = p / 2, towards a finite-time singularity.
// p is 2/3 when rolling friction dissipates the energy (n = 1/3) and 1/3 when
// the viscous air under the disk does (Moffatt; n = 1/6). The disk leaves the
// surface, and the whirr stops, when the rate reaches the disk's ring: the
// whirr lands on the note, and the disk rings on there as it lies flat.
//
// Each disk keeps its life, the share of its spin left: one at the throw,
// falling a fixed step each 64-sample block, so it reaches zero at t0. Its
// rise, in octaves above the held rate, is -n log2(life).
namespace coin {
using opxy::i32;
using opxy::u32;

// Controls, in definition.json's order.
enum Control { SPIN, SIZE, DISK, BASE, WOBBLE, AIR, RING, MANY };

namespace spin {
const i32 one = 1 << 30;             // a whole life, Q30
const i32 octave = 65536;            // Q16 octaves
const i32 hold_at = 2061584302;      // spin from 0.96 up: the disk is kept spinning
const u32 most = 4;                  // disks in a note, at most
const i32 never = 0x7fffffff;        // a rise past any cut

inline u32 q16(i32 control) { return static_cast<u32>(control < 0 ? 0 : control) >> 15; }
inline u32 choice(i32 control, u32 count) {
    const u32 c = (static_cast<u32>(control < 0 ? 0 : control) >> 16) * count >> 15;
    return c < count ? c : count - 1;
}

inline u32 leading_zeros(u32 x) {
    u32 n = 0;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8; x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4; x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2; x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1; }
    return n;
}
// log2(x) for x > 0, Q16.
inline i32 log2_q16(u32 x) {
    const u32 z = leading_zeros(x);
    const u32 m = x << z;   // its leading one in bit 31
    const u32 i = (m >> 26) & 31u, f = (m >> 10) & 0xffffu;
    const u32 a = tables::log2_of[i], b = tables::log2_of[i + 1];
    return static_cast<i32>((31 - z) << 16) + static_cast<i32>(a + ((b - a) * f >> 16));
}
// 2^(the fraction of e), e Q16: Q30, 1 to 2.
inline u32 exp2_mantissa(i32 e) {
    const u32 frac = static_cast<u32>(e) & 0xffffu, i = frac >> 11, f = frac & 2047u;
    const u32 a = tables::exp2_of[i], b = tables::exp2_of[i + 1];
    return a + (((b - a) >> 8) * f >> 3);
}
// floor(e), e Q16, as a whole number.
inline i32 whole(i32 e) { return (e - static_cast<i32>(static_cast<u32>(e) & 0xffffu)) / 65536; }
// 2^e for 0 <= e < 31 octaves (Q16), as a whole number.
inline u32 exp2_q16(i32 e) {
    const i32 k = whole(e);
    return k >= 30 ? exp2_mantissa(e) << (k - 30) : exp2_mantissa(e) >> (30 - k);
}

// Spin: the settle time, 0.2 to 30 s exponentially over 0..0.96, kept up above.
inline bool held(i32 spin) { return spin >= hold_at; }
// Where spin sits in its exponential range, Q16 0..1.
inline u32 settle_position(i32 spin) {
    const u32 w = (q16(spin) >> 1) * 68267u >> 15;   // / 0.96
    return w > 65536 ? 65536 : w;
}
// A block's share of a life: (64 / 44100 s) / (0.2 x 150^w s), Q30:
// 2^(22.8935 - 7.2288 w).
inline i32 life_step(i32 spin) {
    return static_cast<i32>(exp2_q16(1500343 - static_cast<i32>(29609u * settle_position(spin) >> 12)));
}
// The settle time in hundredths of a second, 20 to 3000: 2^(4.3219 + 7.2288 w).
inline u32 settle_hundredths(i32 spin) {
    return exp2_q16(283241 + static_cast<i32>(29609u * settle_position(spin) >> 12));
}

// Air: the rate's exponent n, Q16: 1/3 (rolling friction) to 1/6 (viscous air).
inline i32 exponent(i32 air) { return 21845 - static_cast<i32>(q16(air) * 10923u >> 16); }

// Size: the held contact rate is 1 to 5 whole octaves under the disk's ring
// (Q16 octaves): a small coin buzzes an octave under it, a heavy disk rattles
// five. Whole octaves keep the ring on a harmonic of the strikes, in tune.
const u32 sizes = 5;
inline i32 below(i32 size) { return octave * static_cast<i32>(1 + choice(size, sizes)); }
// The rise at which the disk leaves the surface: the rate at the ring.
inline i32 top(i32 size) { return below(size); }

// Ring: how the disk rings once it lies flat, a T60 of 0.05 s (a dead slap)
// to 4 s (a bell on the note), 0.05 x 80^r. As 1 - r^2 a sample (Q31), about
// 13.8 / (T60 x 44100): 2^(23.680 - 6.3219 r).
inline i32 after_loss(i32 ring) {
    return static_cast<i32>(exp2_q16(1551895 - static_cast<i32>(25894u * (q16(ring) >> 4) >> 8)));
}
// That T60 in hundredths of a second, 5 to 400: 2^(2.3219 + 6.3219 r).
inline u32 after_hundredths(i32 ring) {
    return exp2_q16(152170 + static_cast<i32>(25894u * (q16(ring) >> 4) >> 8));
}

// The rise (Q16 octaves above the held rate) at a life (Q30), for exponent n.
inline i32 rise(i32 life, i32 n) {
    if (life <= 0) return never;
    const u32 d = static_cast<u32>(30 * octave - log2_q16(static_cast<u32>(life)));
    return static_cast<i32>((d >> 6) * static_cast<u32>(n) >> 10);
}
// The life (Q30) at which the rise reaches the cut: 2^(30 - top / n), or 0
// when that is under the life's resolution.
inline i32 life_at_cut(i32 cut, i32 n) {
    const u32 inverse = 0xffffffffu / static_cast<u32>(n);   // 1 / n, Q16
    const i32 e = 30 * octave - static_cast<i32>((static_cast<u32>(cut) >> 4) * (inverse >> 4) >> 8);
    return e < 0 ? 0 : static_cast<i32>(exp2_q16(e));
}

// Many: 1 to 4 disks in a note, of slightly different sizes.
inline u32 count(i32 many) { return 1 + choice(many, most); }
// Each disk's ring against the key (Q30), by how many disks the note has,
// centred so the chorus is in tune: 0; -3.5 and +3.5; 0, +6 and -6; -3, +3,
// -9 and +9 cents.
const i32 detune[most][most] = {{1073741824, 1073741824, 1073741824, 1073741824}, {1071573255, 1075914781, 1073741824, 1073741824}, {1073741824, 1077469586, 1070026959, 1073741824}, {1071882782, 1075604090, 1068174350, 1079338317}};
// How much faster each disk's life runs out (Q15), so each stops at its own
// moment: 1, 1.163, 0.877 and 1.333 times the settle rate.
const i32 hurry[most] = {0, 5341, -4030, 10912};
// Where each disk's contact point starts, so they do not strike together.
const u32 offset[most] = {0u, 0x5a000000u, 0xb3000000u, 0x2d000000u};

// A disk's next life after one block. Kept spinning, it is re-spun toward one;
// otherwise it loses its step.
inline i32 next_life(i32 life, i32 step, bool keep) {
    if (keep) return life + ((one - life) >> 9);
    return life - step;
}
// One disk's step from the controls.
inline i32 disk_step(i32 step, u32 disk) {
    return step + (step >> 8) * hurry[disk] / 128;
}
}
}
