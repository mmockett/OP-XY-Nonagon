#pragma once
#include <opxy/types.hpp>
#include "tables.h"

// The Fermi-Pasta-Ulam-Tsingou chain, shared by the engine and the screen.
// Integer and 32-bit only, so the screen can run it too.
//
// N masses with fixed ends, joined by springs whose force on a stretch d is
//     f(d) = d + d^2 (alpha + beta d).
// Units are strain-normalized: a note loads mode m with a bond strain of
// amplitude 1, x_j = sin(j m pi / (N+1)) / d0, d0 = w_m. Then the regime
// depends only on alpha and beta, which the energy knob sets: in these units
// turning energy up is the same as loading the chain harder. Springs splits
// the strength g between the quadratic and the cubic term:
//     alpha = (1 - s) g,   beta = 1.6 alpha^2 + s g.
// The 1.6 alpha^2 keeps the quadratic chain's well bounded (it needs more
// than alpha^2 / 3), so a hard-loaded alpha chain thermalizes instead of
// flying apart. tools/calibrate.py found the strengths at which each N and m
// is pure, breathes deeply and thermalizes; the knob runs through them.
//
// The integrator is symplectic Euler (kick, then drift), which is leapfrog on
// a staggered clock. In integers each half is a shear: v += F(x) depends only
// on x, and x += v dt only on v, so every step is exactly invertible and the
// rounding cannot drain or pump energy. tests/sound.cpp runs it backward to
// the starting state bit for bit.
namespace fermi {
using opxy::i16;
using opxy::i32;
using opxy::u16;
using opxy::u32;

namespace chain {
const u32 most = 32;          // masses
const u32 voiced = 10;        // modes voiced as partials (each with its body's harmonics)
const u32 ticks_per_unit = 8; // chain time is counted in eighths
enum { ENERGY, MASSES, SPRINGS, TIME, DISPERSION, MODE, DAMPING, SEED };

// A control's choice among `count`, as stock quantizes enums (Q31 to Q15).
inline u32 choice(i32 value, u32 count) {
    const u32 q = value < 0 ? 0 : static_cast<u32>(value) >> 16;
    const u32 c = q * count >> 15;
    return c >= count ? count - 1 : c;
}
// A control as Q15, 0..32768.
inline i32 amount(i32 value) {
    if (value <= 0) return 0;
    return static_cast<i32>((static_cast<u32>(value) + 32768u) >> 16);
}
// Leading zero bits, by halving (no runtime helper on the Blackfin).
inline u32 leading_zeros(u32 v) {
    if (!v) return 32;
    u32 n = 0;
    if (!(v & 0xffff0000u)) { n += 16; v <<= 16; }
    if (!(v & 0xff000000u)) { n += 8; v <<= 8; }
    if (!(v & 0xf0000000u)) { n += 4; v <<= 4; }
    if (!(v & 0xc0000000u)) { n += 2; v <<= 2; }
    if (!(v & 0x80000000u)) n += 1;
    return n;
}
// 2^(l / 4096) x 2^24 for l in Q12, saturating; l below -24 octaves is 0.
inline u32 exp2_q24(i32 l) {
    const i32 whole = (l >> 12) + 24;   // floor
    const u32 f = static_cast<u32>(l & 4095);
    const u32 i = f >> 4, r = f & 15;
    const u32 lo = tables::exp2_fraction[i], hi = i < 255 ? tables::exp2_fraction[i + 1] : 65536u;
    const u32 mantissa = 65536u + lo + (((hi - lo) * r) >> 4);   // Q16, 1..2
    if (whole < 0) return 0;
    if (whole >= 31) return 0xffffffffu;
    return whole >= 16 ? mantissa << (whole - 16) : mantissa >> (16 - whole);
}
// floor(sqrt(v)).
// sqrt(v), by table: normalized to [2^30, 2^32), its top byte indexes the
// table and the next interpolates (to about 1 part in 10^4).
inline u32 root(u32 v) {
    if (v < 2) return v;
    const u32 z = leading_zeros(v) & ~1u;
    const u32 m = v << z;
    const u32 i = m >> 24, f = (m >> 16) & 255;
    const u32 lo = tables::sqrt_table[i], hi = tables::sqrt_table[i + 1];
    return (lo + (((hi - lo) * f) >> 8)) >> (z >> 1);
}

// A coefficient as a 14-bit mantissa and a right shift: value = mantissa / 2^shift.
struct Coefficient { i32 mantissa, shift, half; };   // half: rounding, 2^(shift - 1)
inline Coefficient coefficient(u32 q24, i32 scale) {
    Coefficient c = {0, 0, 0};
    if (!q24) return c;
    const u32 bits = 32 - leading_zeros(q24);   // q24 < 2^bits
    i32 shift = 24;
    u32 m = q24;
    if (bits > 14) { m >>= bits - 14; shift -= static_cast<i32>(bits - 14); }
    else { m <<= 14 - bits; shift += static_cast<i32>(14 - bits); }
    shift -= scale;   // the force keeps alpha d + beta d^2 at 12 + scale fractional bits
    if (shift > 30) return c;   // too small for the chain to feel
    if (shift < 1) { m <<= 1 - shift; shift = 1; }
    c.mantissa = static_cast<i32>(m);
    c.shift = shift;
    c.half = 1 << (shift - 1);
    return c;
}

// The controls as the chain reads them, once a block (or a frame).
struct Setup {
    u32 size;               // index into the tables' sizes
    u32 n, m;               // masses and the struck mode
    u32 seed;               // 0..63
    i32 energy, springs;    // Q15
    u32 strength;           // g, Q24
    u32 alpha, beta;        // Q24
    Coefficient a, b;       // the coefficients (kept for the tests' exact twin)
    i32 scale;              // extra fractional bits of alpha d + beta d^2, so it keeps 14 bits
    i32 ma, mb, common;     // both as mantissas over one shift: alpha d + beta d^2 = (ma t + mb q) >> common
    i32 down;               // the final shift of t r to Q20
    u32 shift;              // dt = 2^-shift chain time units
    u32 dt_ticks;           // ticks a step
    u32 most_steps;         // steps a block at most
    u32 per_block;          // ticks a block, Q16
    u32 loss;               // damping: the share of every mode's swing lost a block, Q24
    u32 smooth;             // and the Laplacian's share, which grows as w^2: high modes die first, Q24
    i32 dispersion;         // Q15: 0 harmonic partials, 1 the chain's own
};

inline u32 strength_of(u32 size, u32 m, i32 energy, i32 springs) {
    // The calibrated anchors, log-interpolated across springs' five positions...
    const i32 sp = springs * 4;                       // Q15 x 4
    const u32 s0 = static_cast<u32>(sp >> 15) > 3 ? 3 : static_cast<u32>(sp >> 15);
    const i32 sf = sp - static_cast<i32>(s0 << 15);   // Q15
    const i32* lo = &tables::anchor[((size * 4 + m - 1) * 5 + s0) * 3];
    const i32* hi = lo + 3;
    i32 a[3];
    for (u32 i = 0; i < 3; ++i) a[i] = lo[i] + (((hi[i] - lo[i]) * sf) >> 15);
    // ...then along energy: pure at 0, deep at 1/2, thermal at 1.
    const i32 e2 = energy * 2;   // Q15, 0..2
    const i32 l = e2 <= 32768 ? a[0] + (((a[1] - a[0]) * e2) >> 15)
                              : a[1] + (((a[2] - a[1]) * (e2 - 32768)) >> 15);
    return exp2_q24(l);
}

inline void configure(Setup& s, const i32* controls) {
    s.size = choice(controls[MASSES], tables::sizes);
    s.n = tables::masses[s.size];
    s.m = choice(controls[MODE], 4) + 1;
    s.seed = choice(controls[SEED], 64);
    s.energy = amount(controls[ENERGY]);
    s.springs = amount(controls[SPRINGS]);
    s.dispersion = amount(controls[DISPERSION]);
    s.strength = strength_of(s.size, s.m, s.energy, s.springs);
    const u32 g = s.strength;
    s.alpha = ((g >> 10) * static_cast<u32>(32768 - s.springs)) >> 5;
    const u32 root_alpha = s.alpha >> 12;   // Q12
    const u32 floor = ((root_alpha * root_alpha) >> 13) * 13107u;   // 1.6 alpha^2, Q24
    s.beta = floor + (((g >> 10) * static_cast<u32>(s.springs)) >> 5);
    // alpha d + beta d^2 at the largest stretch the force takes (4): it gets
    // 16 bits, so a small coefficient keeps its precision.
    const u32 widest = 4 * s.alpha + 16 * s.beta;
    const i32 scale = widest ? 28 - static_cast<i32>(32 - leading_zeros(widest)) : 16;
    s.scale = scale < 0 ? 0 : scale > 16 ? 16 : scale;
    s.a = coefficient(s.alpha, s.scale);
    s.b = coefficient(s.beta, s.scale);
    // One shift for both, set by the larger: 14 bits for it, fewer for the
    // smaller (whose share of the force is then small too).
    {
        const u32 big = s.alpha > s.beta ? s.alpha : s.beta;
        const i32 bits = big ? static_cast<i32>(32 - leading_zeros(big)) : 1;
        i32 common = 38 - bits - s.scale;   // so that big * 2^(common + scale - 24) has 14 bits
        common = common < 1 ? 1 : common > 30 ? 30 : common;
        const i32 e = common + s.scale - 24;
        s.ma = static_cast<i32>(e >= 0 ? s.alpha << e : s.alpha >> -e);
        s.mb = static_cast<i32>(e >= 0 ? s.beta << e : s.beta >> -e);
        s.common = common;
        s.down = 4 + s.scale;
    }
    // The springs' stiffest slope over a strain of 2.5: 1 + 5 alpha + 18.75 beta.
    const u32 stiff = (1u << 24) + 5 * s.alpha + (s.beta >> 2) * 75;
    s.shift = stiff <= (9u << 22) ? 1 : stiff <= (9u << 24) ? 2 : 3;
    s.dt_ticks = ticks_per_unit >> s.shift;
    // Time: the fastest the CPU allows (most_steps of dt 1/2 a block), slowed
    // by 2^(-8 time).
    s.most_steps = tables::most_steps[s.size];
    const i32 time = amount(controls[TIME]);
    s.per_block = (exp2_q24(-time) >> 8) * (s.most_steps * 4);   // Q16 ticks; -time is -8 time octaves, Q12
    // Damping: 0 is lossless, then the struck mode's T60 from 30 s down to
    // 1 s. Mode k loses more as w_k^2 grows, as a string's modes do: the top
    // voiced partial dies four times as fast as the struck mode, so a note
    // settles toward its struck mode as it fades. (A thermalized chain keeps
    // feeding its high modes, so it fades faster than the caption says.)
    const i32 damping = amount(controls[DAMPING]);
    if (damping == 0) s.loss = s.smooth = 0;
    else {
        // ln 1000 x 64 / 44100 / T60, with log2 T60 = log2 30 x (1 - damping).
        const i32 l60 = 20098 - ((20098 * damping) >> 15);   // Q12
        const u32 per_second = exp2_q24(-l60);                // 1 / T60, Q24
        const u32 share = ((per_second >> 8) * 10512u) >> 12; // x 0.010025, Q24
        s.smooth = (share * 3 * tables::inverse_square[s.size]) >> 8;   // 3 share / w_top^2
        const u32 w = tables::strain[s.size * 4 + s.m - 1];             // w_m, Q15
        const u32 at_m = ((s.smooth >> 4) * ((w * w) >> 15)) >> 11;      // smooth x w_m^2
        s.loss = at_m < share - (share >> 2) ? share - at_m : share >> 2;
    }
}
// The breathing period in chain time units, log2 Q12, from tools/calibrate.py's
// table (bilinear in springs and energy). In chaos there is no recurrence: the
// table holds the last recurrent period, a time scale of the mixing.
inline i32 period_log2(const Setup& s) {
    const i32 sp = s.springs * 4, ep = s.energy * 8;
    const u32 s0 = static_cast<u32>(sp >> 15) > 3 ? 3 : static_cast<u32>(sp >> 15);
    const u32 e0 = static_cast<u32>(ep >> 15) > 7 ? 7 : static_cast<u32>(ep >> 15);
    const i32 sf = sp - static_cast<i32>(s0 << 15), ef = ep - static_cast<i32>(e0 << 15);
    const i32* row = &tables::period[((s.size * 4 + s.m - 1) * 5 + s0) * 9 + e0];
    const i32 lo = row[0] + (((row[1] - row[0]) * ef) >> 15);
    const i32 hi = row[9] + (((row[10] - row[9]) * ef) >> 15);
    return lo + (((hi - lo) * sf) >> 15);
}
// Chain time ticks the clock runs a block, log2 Q12: time slows it by
// 2^(-8 time) from the CPU's limit, most_steps steps of dt 1/2; at the limit
// a smaller dt (stiff springs) slows it too.
inline i32 clock_log2(const Setup& s, i32 time) {
    const i32 asked = 2 * 4096 - time, most = static_cast<i32>(3 - s.shift) * 4096;
    return tables::log2_most_steps[s.size] + (asked < most ? asked : most);
}
// The breathing period in seconds, log2 Q12.
inline i32 period_seconds_log2(const Setup& s, i32 time) {
    // ticks: 8 a unit; blocks: 44100 / 64 a second (log2 689.0625 = 9.4285).
    return period_log2(s) + 3 * 4096 - clock_log2(s, time) - 38619;
}

// The chain: displacements and velocities, and its own clock.
struct Chain {
    i32 x[most + 2];   // displacement, Q20; x[0] and x[n + 1] are the walls
    i32 v[most + 2];   // velocity, Q20 a unit of chain time
    u32 n, m, size;
    u32 ticks;         // chain time since the note, eighths of a unit
    u32 clock;         // ticks owed, Q16
    u32 blocks;        // blocks since the note
};

inline u32 scramble(u32 h) {
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
// A note: all the energy in mode m, the chain at rest at its widest. The seed
// adds a perturbation of about 3e-6 of the swing (+-32 of Q20), which a
// recurrent chain shrugs off and a chaotic one amplifies.
inline void init(Chain& c, const Setup& s) {
    c.n = s.n;
    c.m = s.m;
    c.size = s.size;
    c.ticks = 0;
    c.clock = 0;
    c.blocks = 0;
    const u32 half = s.n / 2;
    const i16* row = &tables::shape[tables::shape_at[s.size] + (s.m - 1) * half];
    const i32 f = static_cast<i32>(tables::start[s.size * 4 + s.m - 1]);
    for (u32 j = 1; j <= s.n; ++j) {
        const bool far = j > half;
        i32 sv = row[(far ? s.n + 1 - j : j) - 1];
        if (far && !(s.m & 1)) sv = -sv;
        c.x[j] = (sv * f) >> 6;   // Q15 x Q11 = Q26, to Q20
        if (s.seed) c.x[j] += static_cast<i32>(scramble(s.seed * 0x9e3779b1u + j) >> 26) - 32;
        c.v[j] = 0;
    }
    c.x[0] = c.v[0] = 0;
    for (u32 j = s.n + 1; j < most + 2; ++j) c.x[j] = c.v[j] = 0;
}

// The spring's force on a stretch d (Q20): d + d^2 (alpha + beta d). Every
// shift rounds to nearest, written ((a >> (k - 1)) + 1) >> 1 so it needs no
// constant: floors would bias the force by a fraction of its last bit, which
// is enough to pump energy into a long run. A stretch stays well under 4 (a
// unit strain is the note's own), which keeps every product inside 32 bits;
// guard() below catches a chain that ever leaves that range.
inline i32 force(i32 d, i32 ma, i32 mb, i32 common1, i32 down1) {
    const i32 t = ((d >> 7) + 1) >> 1;                             // Q12
    const i32 q = (((t * t) >> 11) + 1) >> 1;                      // d^2, Q12
    const i32 r = (((ma * t + mb * q) >> common1) + 1) >> 1;       // alpha d + beta d^2, Q(12 + scale)
    return d + ((((t * r) >> down1) + 1) >> 1);
}
inline i32 force(i32 d, const Setup& s) { return force(d, s.ma, s.mb, s.common - 1, s.down - 1); }
// One step of dt = 2^-shift: kick every mass by its springs' forces, then
// drift it. Each half is exactly invertible in integers. Two passes (the
// springs' forces, then the masses) keep each loop's constants in registers.
inline void step(Chain& c, const Setup& s) {
    const i32 ma = s.ma, mb = s.mb, common1 = s.common - 1, down1 = s.down - 1;
    const u32 n = c.n;
    i32 f[most + 1];
    const i32* x = c.x;
    i32 before = 0;   // the left wall
    for (u32 j = 0; j <= n; ++j) {
        const i32 after = x[j + 1];
        f[j] = force(after - before, ma, mb, common1, down1);
        before = after;
    }
    const i32 sh1 = static_cast<i32>(s.shift) - 1;
    i32* xs = c.x + 1;
    i32* vs = c.v + 1;
    for (u32 j = 0; j < n; ++j) {
        const i32 v = vs[j] + ((((f[j + 1] - f[j]) >> sh1) + 1) >> 1);
        vs[j] = v;
        xs[j] += ((v >> sh1) + 1) >> 1;
    }
}
// A block of the clock: the steps it owes, at most most_steps (the backlog
// beyond is dropped, so the chain slows rather than the CPU).
inline u32 owed(Chain& c, const Setup& s) {
    c.clock += s.per_block;
    const u32 need = s.dt_ticks << 16;
    u32 steps = 0;
    while (c.clock >= need && steps < s.most_steps) {
        c.clock -= need;
        ++steps;
    }
    if (c.clock > need) c.clock = need;
    return steps;
}
// Damping, once a block, in audio time so it holds whatever the clock: every
// mode's swing shrinks by `loss`, and by `smooth` times w^2 more (the
// Laplacian of the displacements and velocities: a string's internal friction).
inline void relax(i32* a, u32 n, i32 loss, i32 smooth) {
    i32 before = 0;   // the wall
    for (u32 j = 1; j <= n; ++j) {
        const i32 here = a[j];
        const i32 bend = 2 * here - before - a[j + 1];
        a[j] = here - (((here >> 12) * loss) >> 12) - (((bend >> 8) * (smooth >> 4)) >> 12);
        before = here;
    }
}
// It runs four blocks' worth at a time, the displacements in one block and
// the velocities two later (6 ms steps in a fade of a second or more).
inline void damp(Chain& c, const Setup& s) {
    ++c.blocks;
    if (!s.loss) return;
    if ((c.blocks & 3) == 0) relax(c.x, c.n, static_cast<i32>(s.loss << 2), static_cast<i32>(s.smooth << 2));
    if ((c.blocks & 3) == 2) relax(c.v, c.n, static_cast<i32>(s.loss << 2), static_cast<i32>(s.smooth << 2));
}
// Once a block: a chain that has somehow left the range the force is built
// for (a stretch of 4, far past any note's) is struck again rather than let
// overflow. It never happens in the tests' corners; it is a seat belt.
inline bool guard(Chain& c, const Setup& s) {
    if ((c.blocks & 3) != 1) return false;   // every fourth block is plenty
    i32 widest = 0;
    for (u32 j = 1; j <= c.n; ++j) {
        const i32 a = c.x[j] < 0 ? -c.x[j] : c.x[j];
        widest = a > widest ? a : widest;
    }
    if (widest < (1 << 27)) return false;
    init(c, s);
    return true;
}
// A block: its steps, then the damping. Returns the steps.
inline u32 advance(Chain& c, const Setup& s) {
    const u32 steps = owed(c, s);
    for (u32 i = 0; i < steps; ++i) {
        step(c, s);
        c.ticks += s.dt_ticks;
    }
    damp(c, s);
    guard(c, s);
    return steps;
}
// log2 of v, Q12, for v > 0.
inline i32 log2_q12(u32 v) {
    const u32 bits = 31 - leading_zeros(v);
    const u32 f = bits >= 8 ? (v >> (bits - 8)) & 255 : (v << (8 - bits)) & 255;
    return static_cast<i32>(bits << 12) + tables::log2_fraction[f];
}

// The chain folded about its middle, for projecting onto modes: odd modes
// are symmetric, even ones antisymmetric. Q11.
struct Folded {
    i32 px[most / 2], mx[most / 2], pv[most / 2], mv[most / 2];
};
inline void fold(const Chain& c, Folded& f) {
    const u32 half = c.n / 2;
    for (u32 j = 1; j <= half; ++j) {
        const i32 a = c.x[j] >> 9, b = c.x[c.n + 1 - j] >> 9;
        const i32 p = c.v[j] >> 9, q = c.v[c.n + 1 - j] >> 9;
        f.px[j - 1] = a + b;
        f.mx[j - 1] = a - b;
        f.pv[j - 1] = p + q;
        f.mv[j - 1] = p - q;
    }
}
// Mode k's energy over the note's (Q24): 1 is all of it. The leapfrog's own
// invariant, exact for the linear chain: E = (v^2 (1 - (w dt/2)^2) +
// w^2 x_half^2) / 2, with the position taken half a step back, where the
// velocity is.
inline u32 energy(const Chain& c, const Folded& f, u32 k, u32 shift) {
    const u32 half = c.n / 2;
    const i16* row = &tables::shape[tables::shape_at[c.size] + (k - 1) * half];
    const i32* p = (k & 1) ? f.px : f.mx;
    const i32* q = (k & 1) ? f.pv : f.mv;
    i32 a = 0, b = 0;
    for (u32 j = 0; j < half; ++j) {
        a += (p[j] * row[j]) >> 3;   // Q26, to Q23
        b += (q[j] * row[j]) >> 3;
    }
    const i32 at = a - (b >> (shift + 1));
    const u32 index = c.size * 33 + k;
    const i32 cx = (static_cast<i32>(tables::omega[index]) * (at >> 12)) >> 13;              // Q13
    const i32 cv = ((b >> 10) * static_cast<i32>(tables::kinetic[index * 3 + shift - 1])) >> 15;   // Q13
    const u32 sum = static_cast<u32>(cx * cx) + static_cast<u32>(cv * cv);                    // Q26
    return ((sum >> 10) * tables::inverse_energy[c.size]) >> 6;                                // Q24
}
}
}
