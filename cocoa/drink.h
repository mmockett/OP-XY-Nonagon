#pragma once
#include <opxy/q31.hpp>
#include "tables.h"

// Cocoa's physics, shared by the engine and the screen, so the screen draws
// what the voices play without hearing them. Integer and 32-bit only.
//
// Stirring powder into hot milk fills it with tiny bubbles. Even a fraction
// of a percent of air makes the liquid nearly as compressible as air while it
// keeps water's density, so its sound speed collapses (Wood's equation) and
// the column's quarter-wave resonance with it. As the bubbles rise out, the
// pitch climbs back to the key. tools/make_tables.py has the derivations.
//
// The drink is eight classes of bubble, from an octave under the median
// radius to an octave over. Class i fills the column above s_i (the share it
// has cleared from the bottom) at void fraction d_i, and rises at its Stokes
// speed, clearing the column in h / v_i. The column rings as if its void
// fraction were sum d_i W(s_i), the pressure-weighted share each class keeps.
namespace cocoa {
using opxy::i32;
using opxy::u32;

// Controls, in definition.json's order.
enum Control { POWDER, TAPS, FIZZ, CUP, FILL, SPOON, POPS, STIR };

namespace drink {
using opxy::q31::multiply;
const u32 classes = 8;
const i32 octave = 65536;          // Q16 octaves
const i32 clear = 1 << 30;         // s: the whole column cleared, Q30
const i32 once_below = 655;        // taps under 0.02: a single tap

inline u32 q16(i32 control) { return static_cast<u32>(control < 0 ? 0 : control) >> 15; }
inline u32 choice(i32 control, u32 count) {
    const u32 c = (static_cast<u32>(control < 0 ? 0 : control) >> 16) * count >> 15;
    return c < count ? c : count - 1;
}
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }

inline u32 leading_zeros(u32 x) {
    if (!x) return 32;
    u32 n = 0;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8; x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4; x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2; x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1; }
    return n;
}
// log2 of a nonzero integer, Q16.
inline i32 log2_q16(u32 v) {
    const u32 top = 31 - leading_zeros(v);
    const u32 m = v << (31 - top);                    // [2^31, 2^32)
    const u32 i = (m >> 23) & 255, f = (m >> 7) & 0xffff;
    const u32 a = tables::log2_fraction[i], b = tables::log2_fraction[i + 1];
    return static_cast<i32>(top << 16) + static_cast<i32>(a + (((b - a) * (f >> 4)) >> 12));
}
// 2^(e mod 1), Q30, from e in Q16.
inline u32 mantissa(i32 e) {
    const u32 f = static_cast<u32>(e) & 0xffff, i = f >> 8, r = f & 255;
    const u32 a = tables::exp2_fraction[i], b = tables::exp2_fraction[i + 1];
    return a + (((b - a) * r) >> 8);
}
inline i32 whole(i32 e) { return e >> 16; }   // floor
// 2^(e / 65536) as an integer, for e from -2 to 31 octaves.
inline u32 exp2_int(i32 e) {
    const i32 k = whole(e);
    if (k < -2) return 0;
    if (k > 31) return 0xffffffffu;
    const u32 m = mantissa(e);
    return k >= 30 ? m << (k - 30) : m >> (30 - k);
}
// v x 2^(e / 65536), held under `ceiling`; exactly v when e is 0.
inline u32 scale(u32 v, i32 e, u32 ceiling) {
    if (!e) return v;
    if (e < -30 * octave) return 0;
    const i32 k = whole(e) + 2;
    const u32 m = static_cast<u32>(multiply(static_cast<i32>(v >> 1), static_cast<i32>(mantissa(e))));   // v 2^f / 4
    if (k <= 0) return m >> -k;
    if (k >= 31 || m > (ceiling >> k)) return ceiling;
    return m << k;
}
// A table of 65 points over a control (Q31), interpolated.
inline i32 along(const i32* t, i32 control) {
    const u32 c = static_cast<u32>(control < 0 ? 0 : control);
    const u32 i = c >> 25, f = (c >> 15) & 1023;
    return t[i] + (((t[i + 1] - t[i]) * static_cast<i32>(f)) >> 10);
}

// Wood's equation: how far the column's pitch falls (octaves, Q16) at void
// fraction x (Q31): half of log2 ((1 - x + x K) (1 - x (1 - rho_g / rho_l))).
inline i32 wood_drop(i32 x) {
    if (x <= 0) return 0;
    const u32 a = (1u << 17) + static_cast<u32>(multiply(x, tables::wood_k));          // 1 + (K - 1) x, Q17
    const u32 b = (1u << 30) - static_cast<u32>(multiply(x, tables::wood_density) >> 1);   // Q30
    return (log2_q16(a) - 17 * octave + log2_q16(b) - 30 * octave) / 2;
}
// The powder's drop, Q16 octaves: 4 powder^2, so the first half of the
// control scoops up to an octave and the rest reaches four.
inline i32 powder_drop(i32 control) {
    const u32 c = q16(control);
    return static_cast<i32>(((c * c) >> 16) * 4);
}
// The powder: the initial void fraction (Q31) that drops the column by that
// much (ignoring the density's tiny share): x0 = (4^drop - 1) / (K - 1).
inline i32 powder_void(i32 control) {
    const i32 two_drop = 2 * powder_drop(control);                       // 2 x the drop, Q16
    const i32 k = whole(two_drop);
    const u32 power = mantissa(two_drop) >> (14 - k);                 // 4^drop, Q16
    const u32 e = power - 65536u;
    return static_cast<i32>(((e >> 4) * tables::wood_inverse) >> 6);
}
// The liquid's height, in micrometres (20 to 80 mm).
inline u32 fill_um(i32 control) { return 20000u + ((60000u * q16(control)) >> 16); }
// log2 of the median bubble radius, metres, Q16 (50 to 400 um).
inline i32 log2_radius(i32 fizz) { return tables::log2_radius + static_cast<i32>((static_cast<u32>(tables::radius_span) >> 4) * q16(fizz) >> 12); }
// The median radius in micrometres, for the header.
inline u32 radius_um(i32 fizz) { return ((exp2_int(log2_radius(fizz) + 30 * octave) >> 6) * 62500u + (1u << 19)) >> 20; }

// Each class's clearing rate: the share of the column it clears a block, Q30.
inline void rates(i32 fill, i32 fizz, i32* rate) {
    const i32 height = log2_q16(fill_um(fill)) - 1306235;   // log2 metres (log2 10^6 = 1306235, Q16)
    const i32 r = log2_radius(fizz);
    for (u32 i = 0; i < classes; ++i) {
        i32 speed = tables::log2_stokes + 2 * (r + tables::class_radius[i]);
        if (speed > tables::log2_top_speed) speed = tables::log2_top_speed;
        rate[i] = static_cast<i32>(exp2_int(tables::log2_block + speed - height));
    }
}
// Clears each class by `blocks` blocks at its rate.
inline void advance(i32* s, const i32* rate, u32 blocks) {
    for (u32 i = 0; i < classes; ++i) {
        if (s[i] >= clear) continue;
        const u32 left = static_cast<u32>(clear - s[i]);
        const u32 r = static_cast<u32>(rate[i]);
        s[i] = blocks && r && (blocks >= left / r + 1 || r * blocks >= left) ? clear : s[i] + static_cast<i32>(r * blocks);
    }
}
// W(s), Q30: the pressure-weighted share of the column a class still fills.
inline i32 weighted(i32 s) {
    if (s >= clear) return 0;
    const u32 u = static_cast<u32>(s < 0 ? 0 : s), i = u >> 21, f = (u >> 11) & 1023;
    const i32 a = tables::clearing[i], b = tables::clearing[i + 1];
    return a + ((((b - a) >> 6) * static_cast<i32>(f)) >> 4);
}
// The column's effective void fraction, Q31.
inline i32 effective(const i32* d, const i32* s) {
    i32 x = 0;
    for (u32 i = 0; i < classes; ++i)
        if (s[i] < clear) x += multiply(d[i], weighted(s[i])) << 1;
    return x;
}
// A note stirs the drink: by `stir` (Q15) of the way from what is left
// toward a fresh drink at void fraction x0, mixing what is left back down
// the column by as much. Stir 0 leaves the drink alone; 1 makes it fresh.
inline void stir(i32* d, i32* s, i32 x0, i32 amount) {
    if (amount <= 0) return;
    if (amount > 32767) amount = 32767;
    for (u32 i = 0; i < classes; ++i) {
        const i32 fresh = multiply(x0, tables::class_share[i]);
        const i32 left = s[i] >= clear ? 0 : multiply(d[i], (clear - s[i]) << 1);   // its column average
        const i32 mixed = left + (((fresh - left) >> 15) * amount) + (((((fresh - left) & 32767) * amount)) >> 15);
        const i32 depth = amount >= 32767 ? 0 : static_cast<i32>((static_cast<u32>(s[i] >= clear ? clear : s[i]) >> 15) * static_cast<u32>(32767 - amount));
        // d' = mixed / (1 - s'), in three steps of long division (1 - s' is at least stir).
        const u32 open = static_cast<u32>(clear - depth) >> 10;   // 1 - s', Q20
        const u32 m = static_cast<u32>(mixed < 0 ? 0 : mixed), q = m / open, r = m % open;
        const u32 r1 = (r << 10) / open, r2 = (((r << 10) % open) << 10) / open;
        d[i] = static_cast<i32>((q << 20) + (r1 << 10) + r2);
        s[i] = depth;
    }
}
// How far a note's stir takes a drink still settling toward a fresh one
// (stir), Q15: the square root of the control, so each quarter of its travel
// scoops a second note about as much further.
inline i32 restir(i32 control) {
    const u32 c = q16(control);
    if (!c) return 0;
    const u32 r = exp2_int((log2_q16(c) + 14 * octave) / 2);
    return r > 32767u ? 32767 : static_cast<i32>(r);
}
// The spoon's tapping rate, Q32 cycles a block: 0.5 to 12 Hz, 0 for a single tap.
inline u32 tap_step(i32 control) {
    const u32 c = q16(control);
    if (c < static_cast<u32>(once_below) * 2) return 0;
    // log2 Hz = -1 + log2(24) c; log2 of a block's Q32 cycles for 1 Hz is 32 + log2(64/44100).
    return exp2_int(-octave + static_cast<i32>((c * 4695u) >> 10) + 1479246);
}
// The tapping rate in tenths of a hertz, for the header.
inline u32 tap_tenths(i32 control) {
    const u32 step = tap_step(control);
    return ((step >> 12) * 6891u + (1u << 19)) >> 20;   // Hz = step x 44100 / (64 x 2^32)
}
}
}
