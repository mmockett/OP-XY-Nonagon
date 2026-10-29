#pragma once
#include <opxy/types.hpp>
#include "plate.h"

// Sand's physics, shared by the sound and the screen: the plate's mode shapes,
// its materials, and how a grain of sand on it is thrown, flies and lands.
// Pure functions and small structs; nothing here changes at run time.
namespace sand {
using opxy::i16;
using opxy::i32;
using opxy::u16;
using opxy::u32;
using opxy::u8;

const u32 kGrains = 48;        // grains per voice's plate
const u32 kFull = 65535;       // the plate's far edge, in plate units (0..65535 a side)

// A Q31 control word as 0..32767.
inline i32 control(i32 word) { return word < 0 ? 0 : word >> 16; }
// An enum control word as an index below count, as the native Q31-to-Q15 path quantizes it.
inline u32 choice(i32 word, u32 count) {
    const u32 index = static_cast<u32>(control(word)) * count >> 15;
    return index < count ? index : count - 1;
}

// (a * b) >> 16 without a 64-bit product: exact while the result fits 32 bits.
inline u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffffu, bh = b >> 16, bl = b & 0xffffu;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}
// a + (b - a) * fraction / 32768 for fraction 0..32768, in 32 bits for any
// difference below 2^30: the difference is split at bit 15.
inline i32 mix(i32 a, i32 b, i32 fraction) {
    const i32 d = b - a;
    return a + (d >> 15) * fraction + ((d & 32767) * fraction >> 15);
}
inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }
inline i32 magnitude(i32 x) { return x < 0 ? -x : x; }

// Index of the highest set bit; value must not be 0.
inline u32 top_bit(u32 value) {
    u32 n = 0;
    if (value >= 1u << 16) { n += 16; value >>= 16; }
    if (value >= 1u << 8) { n += 8; value >>= 8; }
    if (value >= 1u << 4) { n += 4; value >>= 4; }
    if (value >= 1u << 2) { n += 2; value >>= 2; }
    if (value >= 1u << 1) n += 1;
    return n;
}
// Sixteenth-octave tables at file scope: the Blackfin GCC would otherwise
// define an inline function's static table in both the engine and display.
static const i32 log2_table[17] = {0, 5732, 11136, 16248, 21098, 25711, 30109, 34312, 38336,
                                   42196, 45904, 49472, 52911, 56229, 59434, 62534, 65536};
static const u32 exp2_table[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682,
                                   96785, 101070, 105545, 110218, 115098, 120194, 125515, 131072};
// log2(value) in Q16, within 0.0004 octave.
inline i32 log2_q16(u32 value) {
    if (!value) return 0;
    const u32 top = top_bit(value);
    const u32 normal = top >= 16 ? value >> (top - 16) : value << (16 - top);
    const u32 fraction = normal & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    return static_cast<i32>(top << 16) + log2_table[at] + ((log2_table[at + 1] - log2_table[at]) * static_cast<i32>(rest) >> 12);
}
// 2^(x / 65536) in Q16, for x from -16 to +15 octaves.
inline u32 exp2_q16(i32 x) {
    const i32 whole = x >> 16;
    const u32 fraction = static_cast<u32>(x) & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    const u32 m = exp2_table[at] + ((exp2_table[at + 1] - exp2_table[at]) * rest >> 12);
    if (whole >= 15) return m << 15;
    if (whole >= 0) return m << whole;
    return whole <= -31 ? 0 : m >> -whole;
}

// Uniform noise, -32768..32767, and a coin.
inline i32 noise15(u32& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<i32>(seed >> 16) - 32768;
}

// ---------------------------------------------------------------------------
// The plate.

// A mode's shape at (u, v), Q15: +-32767 at its loudest. Four table reads and
// two 16 x 16 products.
inline i32 shape(const PlateMode& mode, u32 u, u32 v) {
    const u32 i = u >> 8, j = v >> 8;
    const i32 a = kCos[mode.m][i] * kCos[mode.n][j], b = kCos[mode.n][i] * kCos[mode.m][j];
    return (mode.s ? a + b : a - b) >> 16;
}

// The plate's material, from Plate (0..32767: metal, glass, wood). Glass rings
// a little less long than metal, and its high modes die sooner; wood is a dull,
// short knock that the bow fills quickly.
struct Material {
    i32 log2_loss;  // log2 of the bowed mode's per-block amplitude loss (Q16)
    i32 tilt;       // how much faster a mode loses per octave above the note (Q16)
    i32 halo;       // the bow's reach into the plate's upper modes (Q15)
    i32 rise;       // Q31: the share of the way the bow takes the bowed mode to its level each block
    i32 ring;       // Q15: how loudly a landing grain rings the plate's modes
    i32 fall;       // Q15 a screen step (5.8 ms): how much of its swing a released plate keeps
    i32 twin;       // Q15: the bowed mode's near-degenerate twin, beside it
    i32 beat;       // Q12: how fast the pair beats, times the figure's own rate
    i32 harmonic;   // Q12: how strongly the bow's stick and slip sound the note's harmonics
};
// Metal, glass, wood: T60 6 s, 3 s, 0.6 s (log2 loss = -6.640 - log2 T60);
// the bow takes 70, 45 and 25 ms to fill the mode at full Bow. Glass is the
// purest (the slowest beating and the faintest harmonics), wood the roughest.
const Material kMaterials[3] = {
    {-435177 - 169421, 26214, 6554, 44070000, 32767, 32549, 12452, 4096, 4096},
    {-435177 - 103872, 58982, 3932, 68160000, 26214, 32332, 9830, 2867, 2253},
    {-435177 + 48300, 104858, 3277, 121100000, 16384, 30649, 7209, 6554, 5120},
};
inline Material material(i32 plate) {
    const u32 which = plate >= 16384 ? 1 : 0;
    const i32 toward = (plate - static_cast<i32>(which) * 16384) * 2;
    const Material& a = kMaterials[which];
    const Material& b = kMaterials[which + 1];
    Material m;
    m.log2_loss = mix(a.log2_loss, b.log2_loss, toward);
    m.tilt = mix(a.tilt, b.tilt, toward);
    m.halo = mix(a.halo, b.halo, toward);
    m.rise = mix(a.rise, b.rise, toward);
    m.ring = mix(a.ring, b.ring, toward);
    m.fall = mix(a.fall, b.fall, toward);
    m.twin = mix(a.twin, b.twin, toward);
    m.beat = mix(a.beat, b.beat, toward);
    m.harmonic = mix(a.harmonic, b.harmonic, toward);
    return m;
}

// The bow's steady amplitude at full Bow (amplitude units, at most 4095). The
// bowed level follows Bow^0.3, so Bow is mostly pressure: it changes the
// strike, the harmonics and how hard the sand is thrown far more than the
// loudness (about 3 dB from 0.3 to full).
const i32 kBowAmplitude = 2100;
inline i32 bow_level(i32 bow) {
    if (bow <= 0) return 0;
    // 0.3 times log2(Bow), as a multiply and shift: no division on the Blackfin.
    const i32 octaves = (log2_q16(static_cast<u32>(bow)) - (15 << 16)) >> 4;  // Q12, at most -15 octaves
    return static_cast<i32>(times(static_cast<u32>(kBowAmplitude), exp2_q16(octaves * 19661 >> 12)));
}
// Bow runs from a strike to a full bow. At 0 the note strikes the plate, which
// then rings with its material's decay; as Bow rises the bow takes over,
// sustaining the bowed mode and damping it toward its own level. The strike
// fades as (1 - Bow)^3, so the default's bowed note keeps only a soft onset
// and a bowed legato line leaves the settled sand alone.
inline i32 strike_share(i32 bow) { const i32 r = 32767 - bow; return (r * r >> 15) * r >> 15; }  // Q15: (1 - Bow)^3
// The strike's amplitude in the bowed mode (amplitude units, at full strike),
// and how hard it jolts every grain on the plate (Q12 g): a struck note throws
// even settled sand, a bowed one does not.
const i32 kStrikeAmplitude = 2000, kJolt = 8192;

// ---------------------------------------------------------------------------
// Sand.

// A grain: where it is on the plate, how many more looks it stays in the air,
// how high it was thrown (its landing's loudness), how many looks it has sat
// still (the screen dims grains that have settled), and its own weight: real
// sand is a mix of sizes, so each grain needs between 0.7 and 1.3 times the
// plate's lift to leave it, and the grains settle one by one rather than all
// together.
struct Grain { u16 u, v; u8 air, height, rest, weight; };

// How the plate throws sand this block: each mode's peak acceleration per unit
// of its shape (Q12 g), what a grain must beat to leave the plate (Q12 g: one
// g, and more with friction), how far a hop travels per g of excess and how far
// it slides toward the low edge (plate units), the shake's largest random jolt
// (Q12 g), the chance a hop toward a louder place is refused (Q15), and how
// long a throw keeps the grain aloft (Q8 of a look per half g of excess); a
// tilted plate also pulls every grain downhill (slope, Q12 g).
struct Throw {
    i32 weight[kModes];
    i32 threshold, reach, tilt, slope, jitter, bias, flight;
};

// A mode of amplitude mag (0..4095) at ratio (Q16) over the note accelerates
// its antinodes by mag * ratio / 146 g: 146 is the amplitude at which the
// bowed mode's antinodes just lift a grain, so at the default Bow they reach
// about 10 g and the sand gathers within a tenth of the figure's swing of its
// nodal lines. Returned as Q12 g, at most 16 g.
inline i32 lift(i32 mag, u32 ratio) {
    const u32 l = static_cast<u32>(mag) * ratio;
    const u32 w = (l >> 11) - (l >> 14) - (l >> 16);
    return w > 65535u ? 65535 : static_cast<i32>(w);
}
// The plate's other modes throw sand at a quarter of their acceleration: a
// bowed plate is ruled by its bowed mode, and the faint modes ringing above it
// would otherwise keep the sand from ever settling on its figure. (A model's
// liberty: in a real plate they are simply much quieter.)
inline i32 lift_other(i32 mag, u32 ratio) { return lift(mag, ratio) >> 2; }

// The throw's sand-facing controls. Settle is friction: slippery sand (0)
// skitters at random wherever it is thrown and is lifted by the least swing,
// and bounces high, so it wanders a long time before it finds the narrow quiet
// lines; sticky sand (1) needs more to lift it, so the quiet lines are wider,
// lands dead and refuses louder ground, so it settles fast. Tilt slides each hop toward the
// low edge (the far edge from the bow, v = 65535); Shake jolts every grain at random.
inline void plan_throw(Throw& t, i32 settle, i32 tilt, i32 shake) {
    t.threshold = 4096 + (settle * 3072 >> 15);          // 1 g to 1.75 g
    t.reach = 1700 - (settle * 600 >> 15);               // 2.6% to 1.7% of the plate per g
    t.bias = settle * 31130 >> 15;                       // 0 to 0.95
    t.flight = 640 - (settle * 512 >> 15);               // Q8: 2.5 to 0.5 times as long aloft
    t.tilt = tilt * 3000 >> 15;                          // up to 4.6% of the plate a hop
    t.slope = (tilt * tilt >> 15) * 4096 >> 15;          // the pull downhill: up to 1 g, by Tilt squared
    t.jitter = (shake * shake >> 15) * 6144 >> 15;       // up to 1.5 g
}

// A grain's acceleration at (u, v) from the throw's modes (Q12 g), with each
// shape there written to shapes (Q15) when it is not null.
inline i32 acceleration(const Throw& t, const PlateMode* modes, u32 u, u32 v, i32* shapes) {
    i32 a = 0;
    for (u32 k = 0; k < kModes; ++k) {
        if (!t.weight[k] && !shapes) continue;
        const i32 s = shape(modes[k], u, v);
        if (shapes) shapes[k] = s;
        a += t.weight[k] * (magnitude(s) >> 3) >> 12;
    }
    return a;
}

// The clamp's washer at the plate's centre (7 px of the drawn 152): no grain
// lies on it. Compared in 1/16 plate units, so the squares stay in 32 bits.
const i32 kClampReach = 3018;
inline bool on_clamp(i32 u, i32 v) {
    const i32 du = (u - 32768) >> 4, dv = (v - 32768) >> 4, r = kClampReach >> 4;
    return du * du + dv * dv < r * r;
}

// A hop that would leave the plate bounces off its rim, except at the low
// edge of a tilted plate, where sand piles against the rim instead.
inline i32 rebound(i32 x, i32 tilt) {
    if (x < 0) x = -x;
    if (x > static_cast<i32>(kFull)) x = tilt ? static_cast<i32>(kFull) : 2 * static_cast<i32>(kFull) - x;
    return clamp(x, 0, kFull);
}

// One look at a grain, five or six milliseconds apart. A grain in the air
// falls; one that lands reports how high it was thrown. A grain on the plate
// is thrown when the plate there beats gravity: higher and further the more it
// does, onto a random spot nearby, though a spot the plate shakes harder is
// often refused, so sand drifts to the quiet nodal lines and stays. Returns
// the landing's height (0 for none), plus on_plate when the grain was on the
// plate this look, in which case shapes (when not null) holds each mode's
// shape where it sits.
const u32 on_plate = 256;
inline u32 look(Grain& g, const Throw& t, const PlateMode* modes, u32& seed, i32* shapes) {
    u32 landed = 0;
    if (g.air) {
        if (--g.air) return 0;
        landed = g.height;
        g.height = 0;
    }
    i32 a = acceleration(t, modes, g.u, g.v, shapes);
    if (t.jitter) a += (noise15(seed) + 32768) * t.jitter >> 16;
    a += t.slope;  // on a steep plate even resting sand slips downhill
    // Static friction: a grain must beat its weight by a quarter of a g to be
    // thrown, so one at the edge of a quiet line does not twitch forever.
    i32 excess = a - (t.threshold * (179 + (g.weight * 154 >> 8)) >> 8) - 1024;
    if (excess <= 0) {
        if (g.rest < 255) ++g.rest;
        return landed | on_plate;
    }
    g.rest = 0;
    excess = excess > 16384 ? 16384 : excess;
    g.air = static_cast<u8>(2 + ((excess >> 11) * t.flight >> 8));  // 2..22 looks aloft
    g.height = static_cast<u8>(excess >> 6 > 254 ? 255 : 1 + (excess >> 6));
    const i32 span = t.reach * excess >> 12;
    const i32 u = rebound(static_cast<i32>(g.u) + (noise15(seed) * span >> 15), 0);
    const i32 v = rebound(static_cast<i32>(g.v) + (noise15(seed) * span >> 15) + t.tilt, t.tilt);
    // The landing spot is judged by the figure's mode alone, which rules the throw.
    const i32 there = t.weight[0] * (magnitude(shape(modes[0], static_cast<u32>(u), static_cast<u32>(v))) >> 3) >> 12;
    const i32 here = t.weight[0] * (magnitude(shape(modes[0], g.u, g.v)) >> 3) >> 12;
    if (there > here && noise15(seed) + 32768 < t.bias * 2)
        return landed | on_plate;  // refused: it falls back where it was
    // A hop onto the clamp lands against its washer instead, where the plate
    // is nearly still (the clamp holds a node), so sand piles round it.
    const i32 du = u - 32768, dv = v - 32768;
    const bool against = on_clamp(u, v);
    const i32 rim = kClampReach + (kClampReach >> 3);
    g.u = static_cast<u16>(against && magnitude(du) >= magnitude(dv) ? 32768 + (du < 0 ? -rim : rim) : u);
    g.v = static_cast<u16>(against && magnitude(du) < magnitude(dv) ? 32768 + (dv < 0 ? -rim : rim) : v);
    return landed | on_plate;
}

// Sand poured over the plate at random, as a note starts; what falls on the
// clamp slides off it to the plate's near half.
inline void pour(Grain* grains, u32 count, u32& seed) {
    for (u32 i = 0; i < count; ++i) {
        grains[i].u = static_cast<u16>(noise15(seed) + 32768);
        grains[i].v = static_cast<u16>(noise15(seed) + 32768);
        if (on_clamp(grains[i].u, grains[i].v)) grains[i].v = static_cast<u16>(grains[i].v - 2 * kClampReach);
        grains[i].air = grains[i].height = grains[i].rest = 0;
        grains[i].weight = static_cast<u8>(static_cast<u32>(noise15(seed) + 32768) >> 8);
    }
}
}
