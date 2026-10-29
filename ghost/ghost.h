#pragma once
// Ghost's controls and the apparition's path, shared by the engine
// (sample.cpp) and the screen (display.cpp): integers only, no 64-bit
// arithmetic, no division by a variable in the sample loop.
#include <opxy/types.hpp>
#include "tables.h"

namespace ghost {
using opxy::i32;
using opxy::u32;

// The controls, by saved parameter index (definition.json).
enum Control : unsigned { kRing, kVoice, kSplit, kTrade, kBeat, kOrbit, kNear, kBed };

// A control as 0..65535.
inline u32 level(i32 parameter) { return parameter < 0 ? 0u : u32(parameter) >> 15; }

// 2^(x / 65536) in Q16, for x from -16 to +15 octaves.
inline u32 exp2_q16(i32 x) {
    const i32 whole = x >> 16;                      // floor
    const u32 part = u32(x - whole * 65536);        // 0..65535
    const u32 index = part >> 11, fraction = part & 2047;
    const u32 r = exp2_table[index] + (((exp2_table[index + 1] - exp2_table[index]) * fraction) >> 11);
    return whole >= 0 ? r << whole : r >> -whole;
}

// Sine and cosine of a 32-bit phase, Q15.
inline i32 sine(u32 phase) {
    const u32 index = phase >> 24, fraction = (phase >> 9) & 0x7fff;
    const i32 a = sine_table[index], b = sine_table[index + 1];
    return a + (((b - a) * i32(fraction)) >> 15);
}
inline i32 cosine(u32 phase) { return sine(phase + 0x40000000u); }

// ring: how long the resonance rings, 0.15 s to 12 s (exponential), in ms.
inline u32 ring_ms(u32 ring) { return 150u * exp2_q16(i32(ring * 6474u >> 10)) >> 16; }   // 6.32 octaves
// voice: breath, then the vowels oo, oh, ah, eh, ee; position 0..5 in Q16.
const u32 vowel_count = 6;
inline u32 vowel_position(u32 voice) { return voice * 5; }
// split: how far the harmonics divide between the ears (even and odd), Q16.
// trade: how fast the halves swap ears and back, in mHz: still at 0, then
// from a swap every 24 s, curved, to one every 1.5 s (12 s at 25, 5 s at 50).
inline u32 trade_mhz(u32 trade) { return trade ? 42u + (((trade * trade) >> 16) * 630u >> 16) : 0; }
// beat: the right ear's detune, 0 to 6 Hz, in mHz, curved between linear and
// square: (u + u^2) / 2, so slow beats have room without the top crowding.
inline u32 beat_mhz(u32 beat) { return (((beat * beat) >> 16) + beat) / 2 * 6000u >> 16; }
// orbit: its first quarter spreads the notes from the front round the head;
// the rest circles them, curved from a circle a minute to one every 2 s
// (14 s at 50).
inline u32 orbit_spread(u32 orbit) { return orbit >= 16384 ? 65536u : orbit * 4; }
inline u32 orbit_mhz(u32 orbit) {
    if (orbit <= 16384) return 0;
    const u32 x = (orbit - 16384) * 4 / 3;                    // 0..65535
    return 17u + (((x * x) >> 16) * 483u >> 16);
}
// bed: its share of what excites the resonators, Q16. With a sample loaded,
// the sample's share, bed itself: none at 0, all at 100. Without one, the
// share of Ghost's own whisper, rising as bed squared, so the default (50%)
// keeps the plain breath and hum mostly clear of it.
inline u32 bed_share(u32 bed, bool sampled) { return sampled ? bed : (bed * bed) >> 16; }
// near: the apparition's distance, 3 m down to 10 cm (at the ear), in cm.
inline u32 near_cm(u32 near) { return 300u * (exp2_q16(-i32(near * 5025u >> 10)) >> 2) >> 14; }

// Where a note's apparition is: an angle round the head (0 front, a quarter
// right, a half behind) and how near (0..65535), from its start angle, the
// orbit and the phase it has circled. Circling, it passes close by the right
// ear and recedes on the left.
struct Place { u32 angle; u32 near; };
inline Place place(u32 start, u32 circled, u32 orbit, u32 near) {
    const u32 spread = orbit_spread(orbit);
    Place p;
    p.angle = u32((i32(start) >> 16) * i32(spread >> 1)) * 2u + circled;
    const i32 moving = orbit > 16384 ? 65536 : 0;
    const i32 n = i32(near) + ((sine(p.angle) * 19660 >> 15) * moving >> 16);   // +-0.3 at the sides
    p.near = u32(n < 0 ? 0 : n > 65535 ? 65535 : n);
    return p;
}
// The orbit's phase advance per 64-sample block and per ms, for mHz.
const u32 orbit_block = 6233;   // 2^32 * 64 / 44100 / 1000
const u32 orbit_ms = 4295;      // 2^32 / 1000 / 1000

// A control table of 33 entries read at the nearest step.
inline i32 nearest(const opxy::i16* table, u32 value16) { return table[(value16 + 1024) >> 11]; }
}
