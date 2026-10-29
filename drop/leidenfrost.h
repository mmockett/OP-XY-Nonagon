#pragma once
#include "tables.h"

// Drop's physics, shared by the engine and the screen, so the screen draws
// what the voices play without hearing them. Integer and 32-bit only.
//
// A drop of water on a plate hotter than its Leidenfrost point floats on its
// own vapour. A large one oscillates as a star of n lobes, and n follows its
// radius, since the lobes keep one wavelength (n ~ R; n = 2 to 13 up to
// R = 24 mm, Ma & Burton 2018). The star's frequency is set by the capillary
// length alone, so it holds as the drop shrinks (Brunet 2018). The drop beats
// at twice the star's oscillation, about 23 Hz for water, with a tone inside
// each beat (Singla & Rivera 2020).
//
// Here the lobes are whole, so a shrinking drop steps down through them: its
// wavenumber n/R creeps up while it keeps n lobes, its rate with it, and falls
// back at each step. Under 1.8 lobes' worth of radius it is a sphere, whose
// Rayleigh oscillation rises as R^(-3/2): a final, quickening chirp. Under the
// Leidenfrost point the drop touches the plate and boils away in a fraction
// of a second.
//
// Everything steps once a 64-sample block, the engine's and the screen's alike.
namespace drop {
using opxy::i32;
using opxy::u32;

// Controls, in definition.json's order.
enum Control { HEAT, SIZE, LIQUID, BEAT, GROOVE, VAPOUR, LIFE, CRACKLE };

namespace physics {
const i32 octave = 65536;              // Q16 octaves
const i32 one = 1 << 30;               // a whole life, Q30
const u32 most_lobes = 8;
const i32 full = 32767;                // Q15 one
const i32 top_speed = 5708;            // the ratchet at full groove: 9 cm/s, 60 teeth of 1.5 mm a second, Q16 teeth a block
const u32 hop_blocks = 40;             // a hop over a tooth: 58 ms off the vapour

inline u32 q16(i32 control) { return static_cast<u32>(control < 0 ? 0 : control) >> 15; }
inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }

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
    if (!x) x = 1;
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
inline i32 whole(i32 e) { return (e - static_cast<i32>(static_cast<u32>(e) & 0xffffu)) >> 16; }
// 2^e as a whole number, e Q16, held within 0..31 octaves.
inline u32 exp2_q16(i32 e) {
    e = clamp(e, 0, 31 * octave - 1);
    const i32 k = whole(e);
    return k >= 30 ? exp2_mantissa(e) << (k - 30) : exp2_mantissa(e) >> (30 - k);
}
// a + (b - a) t for t Q16 0..1, for values within +-2^17.
inline i32 lerp(i32 a, i32 b, u32 t) { return a + (((b - a) >> 1) * static_cast<i32>(t >> 2) >> 13); }
// A parabolic sine of a phase (Q32 cycles), Q15.
inline i32 wave(u32 phase) {
    const i32 x = static_cast<i32>(phase) >> 16;
    const i32 a = x < 0 ? -x : x;
    return (4 * x * (32768 - a)) >> 15;
}
// A tooth's fate, from the seed alone: the screen and every voice agree on it.
inline u32 hash(u32 seed, u32 n) {
    u32 h = (seed + 1u) * 0x9e3779b1u ^ n * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    return h ^ (h >> 15);
}

// The plate and the liquid, from the controls: the same each block for every drop.
struct Plate {
    i32 heat;          // the plate's temperature, tenths of a degree C
    i32 superheat;     // over the liquid's Leidenfrost point, tenths of a degree (-100 to +200 C)
    i32 floating;      // Q15: 0 boiling on the plate, full levitating; the step is 16 C wide
    i32 beat;          // the star's beat rate on its plateau, log2 Hz Q16 (0.5 to 23 Hz for water)
    i32 lobe;          // log2 of the radius each lobe takes, mm Q16
    i32 size;          // log2 of the lobe count a new drop's radius makes, Q16 (1.2 to 8.4)
    i32 float_life;    // log2 of an eight-lobe drop's floating life, s Q16
    i32 sizzle_life;   // log2 of an eight-lobe drop's life boiling on the plate, s Q16
    i32 crackle, groove, vapour;   // Q15
    u32 seed;          // the hops' pattern: fixed, so every drop of a size skips alike
};

inline Plate plate(const i32* controls) {
    Plate p;
    const u32 liquid = q16(controls[LIQUID]);
    const u32 part = liquid >= 32768 ? 1 : 0;
    const u32 t = (liquid - part * 32768) * 2;
    const i32 leidenfrost = lerp(tables::leidenfrost[part], tables::leidenfrost[part + 1], t);
    // Heat sets the plate 100 degrees under the liquid's Leidenfrost point to
    // 200 over it: 100 to 400 C for water, -200 to +100 C for liquid nitrogen,
    // so every liquid can boil or float.
    p.superheat = -1000 + static_cast<i32>(q16(controls[HEAT]) * 3000u >> 16);
    p.heat = leidenfrost + p.superheat;
    // A smooth step across 16 degrees: the drop floats a little before it
    // floats well, bouncing on and off the plate in between.
    const i32 x = clamp((p.superheat + 80) * 205, 0, full);   // Q15 over 160 tenths
    p.floating = (x * x >> 15) * (3 * full - 2 * x) >> 15;
    // Beat: the star's beat rate, 0.5 to 23 Hz for water. The papers' drops
    // beat at 23 Hz; slower is the physics bent, so the beats can be counted.
    p.beat = tables::slowest_beat + static_cast<i32>((q16(controls[BEAT]) >> 2) * (tables::beat_span >> 2) >> 12) +
             lerp(tables::beat_offset[part], tables::beat_offset[part + 1], t);
    p.lobe = lerp(tables::lobe_log[part], tables::lobe_log[part + 1], t);
    // Size: a radius of 1.2 to 8.4 lobes, a sphere to an eight-lobed star.
    const u32 nu = 78643u + (q16(controls[SIZE]) * 7372u >> 10);   // Q16 lobes
    p.size = log2_q16(nu) - 16 * octave;
    // Life: an eight-lobe water drop, 25 degrees over its point, lives 0.5 to
    // 60 s; hotter, it lives shorter, as the square root of the superheat.
    const i32 over = p.superheat > 0 ? p.superheat : 0;
    const i32 heat_term = (log2_q16(750) - log2_q16(static_cast<u32>(over) + 250)) / 2;
    p.float_life = tables::half_second + static_cast<i32>((q16(controls[LIFE]) >> 2) * (tables::life_span >> 2) >> 12) +
                   lerp(tables::life_offset[part], tables::life_offset[part + 1], t) + heat_term;
    // On the plate it is gone in 0.2 s just under the point, 0.8 s 100 degrees under.
    const i32 under = clamp(-p.superheat, 0, 1000);
    p.sizzle_life = tables::sizzle_life + under * 131;
    p.crackle = q15(controls[CRACKLE]);
    p.groove = q15(controls[GROOVE]);
    p.vapour = q15(controls[VAPOUR]);
    p.seed = 42;
    return p;
}

// A floating drop's brief contacts, a second: sparse far over the point and
// densest near it, an octave fewer every 12 degrees, as crackle squared:
// 24 a second far over the point to 324 at it, at full crackle.
inline i32 contacts(const Plate& p) {
    const i32 c2 = p.crackle * p.crackle >> 15;
    const i32 near = p.superheat <= 0 ? full : static_cast<i32>(exp2_q16(15 * octave - p.superheat * 546));
    return (24 + (300 * near >> 15)) * c2 >> 15;
}

// The life a drop of `nu0` lobes has, log2 s Q16: larger drops live longer,
// as R^(3/4) floating and R^(1/2) boiling.
inline i32 float_life(const Plate& p, i32 nu0) { return p.float_life + (nu0 - 3 * octave) * 3 / 4; }
inline i32 sizzle_life(const Plate& p, i32 nu0) { return p.sizzle_life + (nu0 - 3 * octave) / 2; }
// The life it would live now, log2 s Q16: floating or boiling, as it stands.
inline i32 lifetime(const Plate& p, i32 nu0) {
    return p.floating >= 16384 ? float_life(p, nu0) : sizzle_life(p, nu0);
}

// The lobes a radius of nu (log2 lobes, Q16) holds: down a step 0.2 under the
// whole count, so a drop keeps its star a while before giving up a lobe.
inline u32 lobes_at(i32 nu) {
    const u32 n = (exp2_q16(nu + 16 * octave) + 13107u) >> 16;
    return n < 1 ? 1 : n > most_lobes ? most_lobes : n;
}

// One drop, as both the engine and the screen keep it.
struct Drop {
    i32 life;      // Q30: the share of its life left
    i32 nu0;       // log2 of its radius in lobes when it landed, Q16
    i32 nu;        // log2 of its radius in lobes now, Q16
    u32 lobes;     // the star's lobes, 8 down to 2; 1 once a sphere; 0 once gone
    i32 star;      // the star oscillation's strength, Q15: it grows after each change
    u32 age;       // blocks since it landed
    i32 speed;     // the ratchet's push: teeth a block, Q16
    u32 travel;    // teeth travelled, Q16 (wraps)
    u32 hop;       // blocks left in a hop over a tooth
};
enum Event : u32 { stepped = 1, skipped = 2, landed = 4, gone = 8 };

inline void land(Drop& d, const Plate& p) {
    d.life = one;
    d.nu0 = d.nu = p.size;
    d.lobes = lobes_at(p.size);
    d.star = 19661;   // the star is half grown as it lands: a drop speaks at once
    d.age = 0;
    d.speed = 0;
    d.travel = 0;
    d.hop = 0;
}

// One block of a drop's life. Returns what happened, as Events.
inline u32 step(Drop& d, const Plate& p) {
    if (!d.lobes) return 0;
    u32 events = 0;
    ++d.age;
    // Evaporation: floating it loses its life over float_life, on the plate over sizzle_life.
    const u32 floating = exp2_q16(tables::per_block - float_life(p, d.nu0));
    const u32 boiling = exp2_q16(tables::per_block - sizzle_life(p, d.nu0));
    const u32 loss = floating + (boiling >> 15) * static_cast<u32>(full - p.floating) -
                     (floating >> 15) * static_cast<u32>(full - p.floating);
    d.life -= static_cast<i32>(loss);
    // A puddle's volume goes as R^2 and its evaporation as R^(3/2) (Biance
    // 2003), so R^(3/2) falls linearly: R = R0 life^(2/3).
    i32 down = d.life > 0 ? log2_q16(static_cast<u32>(d.life)) - 30 * octave : -8 * octave;
    if (down < -8 * octave) down = -8 * octave;
    d.nu = d.nu0 + ((down >> 4) * 43691 >> 12);
    if (d.life <= 0 || d.nu < tables::gone_nu) {
        d.lobes = 0;
        d.life = 0;
        return events | gone;
    }
    const u32 n = lobes_at(d.nu);
    if (n < d.lobes) {
        d.lobes = n;
        d.star = d.star * 3 / 10;   // the old mode dies, the new one grows
        events |= stepped;
    }
    d.star += (full - d.star) >> 6;   // grows over about 0.1 s
    // The ratchet: a levitating drop on sawtooth teeth is pushed along,
    // gathering speed over 0.4 s. Now and then it hops a tooth, chosen by the seed.
    const i32 target = (top_speed * p.groove >> 15) * p.floating >> 15;
    d.speed += (target - d.speed) / 256;
    const u32 before = d.travel >> 16;
    d.travel += static_cast<u32>(d.speed);
    const u32 tooth = d.travel >> 16;
    if (tooth != before && !d.hop && p.groove > 0 &&
        (hash(p.seed, tooth) & 0xffffu) < static_cast<u32>(p.groove) * 2 / 13) {   // up to 0.15 of the teeth
        d.hop = hop_blocks;
        events |= skipped;
    } else if (d.hop && !--d.hop) {
        events |= landed;
    }
    return events;
}

// The beat rate: twice the star's oscillation, log2 Hz Q16. With n lobes, the
// star's wavenumber is n / R; over its plateau the rate goes with it. A sphere
// oscillates as Rayleigh's, R^(-3/2). On the ratchet the beats drift faster
// as the drop gathers speed and wobble as it crosses the teeth.
inline i32 beat_rate(const Drop& d, const Plate& p) {
    i32 rate;
    if (d.lobes >= 2) rate = p.beat + (log2_q16(d.lobes << 16) - 16 * octave - d.nu) / 2;
    else rate = p.beat + tables::sphere_ratio / 2 + (tables::sphere_nu - d.nu) * 3 / 2;
    rate += d.speed + d.speed / 7;   // 0.1 octave at top speed
    rate += ((p.groove * 5243 >> 15) * wave(d.travel << 13)) >> 15;   // +-0.08 octave every 8 teeth
    return rate;
}
// The radius, log2 mm Q16.
inline i32 radius(const Drop& d, const Plate& p) { return d.nu + p.lobe; }
}
}
