#pragma once
#include <opxy/types.hpp>

// The flight, as both the engine and the screen read it from the controls:
// 32-bit integer arithmetic only, so the display can share it.
//
// The object flies a straight line at a constant Mach number M, passing the
// listener at its closest distance d at time 0. Distances are measured in
// sound-seconds (343 m is 1), so sound travels at 1. It sounds for `dur`
// seconds of its own time, from -dur/2 to +dur/2. A listener at time t hears
// what it emitted at every time tau with t = tau + |r(tau)|.
namespace mach {
using opxy::i32;
using opxy::u32;

// The controls, in definition.json's order: page 1, then page 2.
enum Control { MACH, PASS, SOURCE, DUR, BOOM, AIR, TUNE, ECHO };
enum Source { ORGAN, PAD, VOICE, BELL, SIREN, SOURCES };

namespace flight {
const i32 one = 65536;                 // Q16
const i32 lead = 1638;                 // Q16 seconds of silence before a cone arrives (25 ms)
const i32 margin = 1311;               // the cone must leave the object 20 ms inside its flight

// A normalized Q31 control as Q16, 0..65536.
inline i32 q16(i32 p) { return p <= 0 ? 0 : (p >> 15) + ((p >> 14) & 1); }

// 2^x for x in [0, 14), Q16 in, Q16 out.
inline i32 exp2_q16(i32 x) {
    static const opxy::u16 table[17] = {16384, 17109, 17867, 18658, 19484, 20347, 21247, 22188, 23170, 24196, 25268, 26386, 27554, 28774, 30048, 31379, 32768};
    if (x < 0) x = 0;
    const i32 whole = x >> 16, at = (x >> 12) & 15, frac = x & 4095;
    const i32 m = table[at] + (((table[at + 1] - table[at]) * frac) >> 12);   // Q14
    return m << (whole + 2);
}

// Leading zeros of a 32-bit word, without the compiler's helper (the Blackfin
// firmware links none).
inline u32 clz(u32 x) {
    if (!x) return 32;
    u32 n = 0;
    if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xff000000u)) { n += 8; x <<= 8; }
    if (!(x & 0xf0000000u)) { n += 4; x <<= 4; }
    if (!(x & 0xc0000000u)) { n += 2; x <<= 2; }
    if (!(x & 0x80000000u)) n += 1;
    return n;
}

// log2 of a positive integer, Q16 (linear between octaves, within 0.09 octave).
inline i32 log2_q16(u32 x) {
    if (!x) return 0;
    const i32 top = 31 - static_cast<i32>(clz(x));
    const u32 mantissa = top >= 16 ? x >> (top - 16) : x << (16 - top);   // 1.0 .. 2.0 in Q16
    return (top << 16) + static_cast<i32>(mantissa - 65536);
}

// Integer square root.
inline u32 isqrt(u32 x) {
    u32 root = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= root + bit) { x -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

// Q16 products and quotients for values below 2^19.
inline i32 mul16(i32 a, i32 b) { return ((a >> 4) * (b >> 4)) >> 8; }
inline i32 div16(i32 a, i32 b) {
    if (b <= 0) return 0;
    return static_cast<i32>((static_cast<u32>(a) << 12) / static_cast<u32>(b)) << 4;
}

// mach: 0.3 .. 1 .. 3 (anchors), Q16.
inline i32 mach(i32 p) {
    const i32 v = q16(p);
    return v < 32768 ? 19661 + ((v * 45875) >> 15) : one + ((v - 32768) << 2);
}
// pass: the closest distance, 3 .. 300 m (exponential), metres Q16.
inline i32 pass_metres(i32 p) { return 3 * exp2_q16((q16(p) * 6803) >> 10); }   // log2(100) = 6.6439
// The same in sound-seconds, Q16.
inline i32 sound_seconds(i32 metres_q16) { return metres_q16 / 343; }
// source: organ, pad, voice, bell, siren (the native enum quantization).
inline u32 source(i32 p) {
    const u32 s = (static_cast<u32>(p < 0 ? 0 : p) >> 16) * SOURCES / 32768u;
    return s < SOURCES ? s : SOURCES - 1;
}
// dur: the flight's own time, 0.5 .. 8 s (exponential), Q16 seconds.
inline i32 dur(i32 p) { return exp2_q16(q16(p) << 2) >> 1; }
// tune: where in the flight the glide sounds exactly the key: -1 its start,
// 0 the closest pass, 1 its end (Q16, -65536 .. 65536).
inline i32 tune(i32 p) { return (q16(p) - 32768) * 2; }
// echo: the listener's height over the reflecting ground, 1.5 .. 63 m, metres Q16 ...
inline i32 height_metres(i32 p) { return (3 * exp2_q16((q16(p) * 5530) >> 10)) >> 1; }   // 2^5.4
// ... and how strongly the ground reflects, Q15: 0 with echo off, 0.8 from a third up.
inline i32 echo_level(i32 p) { const i32 v = q16(p); return v >= 21845 ? 26214 : (v * 26214) / 21845; }
// boom and air: amounts, Q15.
inline i32 amount(i32 p) { return q16(p) >> 1; }

// The boom's N-wave lasts about 7 ms at 20 m and Mach 1.4, longer further
// away (d^1/4) and nearer Mach 1 ((M^2 - 1)^-3/8): 3 .. 50 ms, Q16 seconds.
// m: Mach number, Q16, above 1; d: closest distance in sound-seconds, Q16.
inline i32 nwave(i32 m, i32 d) {
    const i32 k = mul16(m, m) - one;
    if (k <= 0 || d <= 0) return 3277;
    const i32 ld = log2_q16(static_cast<u32>(d)) - log2_q16(3821u);      // d / 20 m
    const i32 lk = log2_q16(static_cast<u32>(k)) - (16 << 16);
    const i32 e = (ld >> 2) - ((3 * lk) >> 3);
    // 7 ms * 2^e, via 2^(e + 6) / 64
    const i32 x = e + (6 << 16);
    const i32 t = x <= 0 ? 0 : static_cast<i32>((static_cast<u32>(exp2_q16(x < (13 << 16) ? x : 13 << 16)) >> 6) * 459u >> 16);
    return t < 197 ? 197 : t > 3277 ? 3277 : t;
}

// When the listener hears the flight, relative to its closest pass.
struct Timeline {
    i32 start;     // listener time at note-on, Q16 seconds
    i32 cone;      // seconds from note-on until the Mach cone arrives (0 when none)
    i32 length;    // seconds from note-on until the last sound arrives
    i32 sigma;     // the emission time the cone leaves from (0 when none)
    u32 has_cone;  // 1 when the object outruns its sound inside its flight
};

// d: closest distance in sound-seconds, Q16; m: Mach number, Q16; length: dur, Q16.
// The flight's window in its own time: tune puts the part it lands on the key
// in the bulk of the note. An approach of 0.5 - 0.4 tune of the flight comes
// before the closest pass (0.1 at the end, 0.5 at the pass, 0.9 at the start),
// the rest after. Q16 seconds: the window starts at -approach, ends at +depart.
inline i32 approach(i32 length, i32 tune_q16) { return mul16(length, 32768 - ((tune_q16 >> 4) * 26214 >> 12)); }

inline Timeline timeline(i32 m, i32 d, i32 length, i32 before) {
    Timeline t = {};
    const i32 after = length - before;
    // The distance at either end of the flight, sqrt((M sigma)^2 + d^2), via Q12.
    const i32 d12 = d >> 4;
    const i32 x0 = mul16(m, before) >> 4, x1 = mul16(m, after) >> 4;
    const i32 first = static_cast<i32>(isqrt(static_cast<u32>(x0) * static_cast<u32>(x0) +
                                             static_cast<u32>(d12) * static_cast<u32>(d12))) << 4;
    const i32 last = static_cast<i32>(isqrt(static_cast<u32>(x1) * static_cast<u32>(x1) +
                                            static_cast<u32>(d12) * static_cast<u32>(d12))) << 4;
    t.start = first - before;   // the first sound emitted arrives first ...
    if (m > one + 64) {
        // ... unless the object outruns its sound: then the cone, from
        // sigma* = -d / (M sqrt(M^2 - 1)), arrives first, at d sqrt(M^2 - 1) / M.
        const i32 k = mul16(m, m) - one;
        const i32 root = static_cast<i32>(isqrt(static_cast<u32>(k) << 12)) << 2;
        const i32 reach = mul16(m, root);
        const i32 sigma = reach > 0 ? div16(d, reach) : 1 << 24;
        if (sigma < before - margin) {
            t.has_cone = 1;
            t.sigma = -sigma;
            t.start = div16(mul16(d, root), m) - lead;
            t.cone = lead;
        }
    }
    t.length = after + last - t.start;   // the last sound emitted arrives last
    return t;
}
}
}
