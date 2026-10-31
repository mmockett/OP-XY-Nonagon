#pragma once
#include <opxy/types.hpp>
#include "tables.h"

// Harper's equation, shared by the engine and the screen.
//
// An electron hops along a chain of sites in a magnetic field. In the
// Landau gauge its amplitudes obey Harper's equation, the almost-Mathieu
// (Aubry-Andre) operator, on 17 sites m = -8..8:
//
//     H psi_m = psi_(m+1) + psi_(m-1) + 2 lambda cos(2 pi alpha m) psi_m.
//
// Against the flux alpha its energies draw Hofstadter's butterfly; at
// alpha = p/q they gather into q bands. lambda = 1 is the self-dual point:
// below it the states spread over the chain, above it each is localized.
//
// A note is the chain struck at one site: partial k rings at energy E_k,
// as loud as |psi_k(s)|^2, the weight state k gives the struck site. Struck
// where extended states live, every partial sounds; above lambda = 1, only
// the few states living near the struck site. The chain is mirror-symmetric
// (the potential's phase is 0 at its middle), as a butterfly is: each
// localized state has its twin on the other half.
//
// The spectrum is stored (tables.h, solved in double precision by
// tools/make_tables.py) on a grid of flux and coupling and read here by
// linear interpolation across flux, coupling and the struck site, matching
// states by rank: so a note starts at once and every control glides.
// Integers only, 32-bit, no division.
namespace butterfly {
using opxy::i16;
using opxy::i32;
using opxy::u16;
using opxy::u32;
using opxy::u8;

// The controls, in definition.json's order: page 1, then page 2.
enum Control { FLUX, COUPLING, SPREAD, STRIKE, GLOW, SITE, TILT, WINGBEAT };

const u32 partials = tables::sites;            // 17: one a state
const u32 half_chain = tables::half_sites;     // distances 0..8 from the middle

inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 q15(i32 control) { return clamp(control >> 16, 0, 32767); }   // a Q31 control as Q15

// sin and cos of a phase in Q32 turns, Q15, interpolated.
inline i32 sine(u32 phase) {
    const u32 at = phase >> (32 - tables::sine_bits);
    const i32 a = tables::sine[at], b = tables::sine[at + 1];
    const u32 frac = (phase >> (17 - tables::sine_bits)) & 0x7fffu;
    return a + (((b - a) * static_cast<i32>(frac)) >> 15);
}
inline i32 cosine(u32 phase) { return sine(phase + 0x40000000u); }

// Leading zero bits, by halves (the Blackfin compiler makes __builtin_clz a library call).
inline u32 leading_zeros(u32 a) {
    if (!a) return 32;
    u32 n = 0;
    if (!(a >> 16)) {
        n += 16;
        a <<= 16;
    }
    if (!(a >> 24)) {
        n += 8;
        a <<= 8;
    }
    if (!(a >> 28)) {
        n += 4;
        a <<= 4;
    }
    if (!(a >> 30)) {
        n += 2;
        a <<= 2;
    }
    return a >> 31 ? n : n + 1;
}

// 2^(e / 65536) as a mantissa in [2^15, 2^16) (Q15) and a whole power of two.
inline void exp2_parts(i32 e, u32& mantissa, i32& whole) {
    whole = e >> 16;   // floor
    const u32 f = static_cast<u32>(e) & 0xffffu;
    const u32 at = f >> 8, frac = f & 255u;
    const u32 a = tables::exp2[at], b = tables::exp2[at + 1];
    mantissa = a + (((b - a) * frac) >> 8);
}
// 2^(e / 65536) in Q15 for e <= 0 (0 below 2^-15).
inline i32 exp2_down(i32 e) {
    u32 m;
    i32 whole;
    exp2_parts(e, m, whole);
    if (whole < -16) return 0;
    return static_cast<i32>(whole >= 0 ? 32768u : m >> -whole);
}
// log2(a) in Q16 for a > 0.
inline i32 log2_q16(u32 a) {
    const i32 bits = 31 - static_cast<i32>(leading_zeros(a));
    const u32 m = bits >= 16 ? a >> (bits - 16) : a << (16 - bits);   // [2^16, 2^17)
    const u32 f = m & 0xffffu, at = f >> 8, frac = f & 255u;
    const u32 l0 = tables::log2m[at], l1 = at + 1 < 256 ? tables::log2m[at + 1] : 65536u;
    return (bits << 16) + static_cast<i32>(l0 + (((l1 - l0) * frac) >> 8));
}

// (a * b) >> 16 for a, b >= 0 whose result fits: four 16-bit products.
inline u32 mul16u(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffffu, bh = b >> 16, bl = b & 0xffffu;
    return ((ah * bh) << 16) + ah * bl + al * bh + ((al * bl) >> 16);
}
inline i32 mul16(i32 a, i32 b) {
    const bool negative = (a < 0) != (b < 0);
    const u32 r = mul16u(a < 0 ? 0u - static_cast<u32>(a) : static_cast<u32>(a),
                         b < 0 ? 0u - static_cast<u32>(b) : static_cast<u32>(b));
    const i32 v = r > 0x7fffffffu ? 0x7fffffff : static_cast<i32>(r);
    return negative ? -v : v;
}

// 1 / d in Q16 for d in Q16, saturating at +-2^30 (d within 2^-14 of 0).
// A seed from the table, then one Newton step: about 2^-15 relative.
const i32 reciprocal_limit = 1 << 30;
inline i32 reciprocal(i32 d) {
    const bool negative = d < 0;
    const u32 a = negative ? 0u - static_cast<u32>(d) : static_cast<u32>(d);
    if (a < 4) return negative ? -reciprocal_limit : reciprocal_limit;
    const i32 length = 32 - static_cast<i32>(leading_zeros(a));
    const i32 shift = 16 - length;
    const u32 m = shift >= 0 ? a << shift : a >> -shift;   // [2^15, 2^16)
    const u32 y = tables::recip[(m >> 7) - 256];          // about 2^30 / m
    const u32 t = (0x80000000u - m * y + 16384u) >> 15;    // 2 - m y, Q15 of 2^30
    const u32 y1 = (y * t + 16384u) >> 15;
    const i32 up = 2 + shift;
    const u32 r = up >= 0 ? y1 << up : y1 >> -up;
    const i32 v = r > static_cast<u32>(reciprocal_limit) ? reciprocal_limit : static_cast<i32>(r);
    return negative ? -v : v;
}


// 2^(e / 65536) in Q16, for e up to 14 octaves; 0 far below.
inline u32 exp2_q16(i32 e) {
    u32 m;
    i32 whole;
    exp2_parts(e, m, whole);   // m: Q15, 1 to 2
    if (whole >= -1) return m << clamp(whole + 1, 0, 15);
    return whole < -17 ? 0 : m >> (-whole - 1);
}

// ---------------------------------------------------------------- controls

// Flux: alpha from 0 to 1/2 flux quantum, Q32 of a turn. The chain at
// 1 - alpha is the chain at alpha, so the control spans the half that differs.
inline u32 flux(i32 control) { return static_cast<u32>(clamp(control, 0, 0x7fffffff)); }

// Coupling: lambda from 0 to 2, Q12, with a detent at the self-dual point:
// the middle 6% of the control is lambda = 1 exactly.
const i32 detent_low = 15401, detent_high = 17367;   // Q15 of the control
inline i32 coupling(i32 control) {
    const i32 c = q15(control);
    if (c < detent_low) return (c * 17430) >> 16;                              // 4096 / 15401
    if (c <= detent_high) return 4096;
    return 4096 + (((c - detent_high) * 17430) >> 16);                         // 4096 / 15400
}

// Spread: the spectrum's span from 3 to 48 semitones, 3 * 16^v; as log2 of
// octaves, Q16 (-2 to 2).
inline i32 spread_log2(i32 control) { return ((q15(control) * 4) << 1) - (2 << 16); }
// The span in semitones, Q8.
inline i32 spread_semitones(i32 control) {
    u32 m;
    i32 whole;
    exp2_parts(spread_log2(control), m, whole);   // octaves
    const u32 st = m * 12u;                        // Q15 semitones before the power of two
    return static_cast<i32>(whole >= 0 ? (st << whole) >> 7 : (st >> -whole) >> 7);
}

// Strike: how hard a note strikes, Q15. The gate holds at 1 - strike while
// the note is held, and every strike above 0 pulses it open first. The
// knob's curve, 1 - (1 - k) 2^(-3k), lowers that held hum 3 to 6 dB every
// tenth of its travel (-7 dB at 25, -15 at 50, -26 at 75), a pluck at 100.
inline i32 strike(i32 control) {
    const i32 k = q15(control);
    return 32767 - (((32767 - k) * exp2_down(-6 * k)) >> 15);
}

// Glow: the time a struck gate takes to close from open to -60 dB, 40 ms to
// 8 s, exponentially; log2 seconds, Q16.
inline i32 glow_log2(i32 control) { return -304340 + ((q15(control) * 15655) >> 10); }

// Site: the struck site's distance from the middle of the chain, 0 to 8, Q16.
inline i32 site(i32 control) { return q15(control) << 4; }

// Tilt: how dark the top of the spectrum is, and how much more quickly it
// follows the gate down: a partial's gate is the note's to the power
// 1 + tilt x place, and the upper three quarters of the span a further
// tilt x 8 dB down at the top (and all of it tilt x 1 dB up, to keep the
// note's loudness, never above a partial's own level), tilt 0 to 3, Q16.
inline i32 tilt(i32 control) { return q15(control) * 6; }

// Wingbeat: a slow beat of the flux (up to 1/48 either way) and of the
// struck site (up to 1.5 sites), 0.2 to 0.6 beats a second as it deepens.
// Each note beats from its own strike, starting at rest.
inline u32 wingbeat_rate(i32 control) { return 1246571u + ((2493142u >> 6) * static_cast<u32>(q15(control)) >> 9); }   // Q32 turns a block
inline i32 wingbeat_flux(i32 control, u32 beat) { return (((q15(control) * 2731) >> 15) * sine(beat)) >> 0; }            // Q32 turns (2^32 / 48 = 2731 x 32768)
// The site beats on a phase of its own, 0.618 times as fast.
inline i32 wingbeat_site(i32 control, u32 beat) { return (((q15(control) * 3) >> 1) * sine(beat)) >> 14; }   // Q16 sites
inline u32 wingbeat_site_rate(u32 rate) { return (rate >> 10) * 633u; }   // 0.618 of the flux's
// ---------------------------------------------------------------- snap

// Snap pulls alpha towards the nearest simple fraction (the Farey sequence
// of order 8), as Talbot's does: each fraction's basin reaches to the
// mediants with its neighbours, so simpler fractions hold wider basins. At
// snap s, alpha rests on the fraction for the share s of each half-basin
// nearest it and crosses the rest 1 / (1 - s) times as fast.
const i32 snap_pull = 9830;   // 0.3, Q15: the engine's and the screen's
inline u32 warp_half(u32 offset, u32 inverse, u32 width, i32 s) {
    const i32 u = clamp(static_cast<i32>(((offset >> 8) * inverse) >> 15), 0, 32768);
    if (u <= s) return 0;
    const i32 pulled = clamp(mul16((u - s) << 1, reciprocal((32768 - s) << 1)) >> 1, 0, 32768);   // (u - s) / (1 - s), Q15
    return (static_cast<u32>(pulled) * (width >> 12) >> 15) << 12;
}
inline u32 interval(u32 a) {
    u32 i = tables::first[a >> 24];
    while (i + 1 < tables::fraction_count - 1 && a - tables::fraction[i] >= tables::fraction[i + 1] - tables::fraction[i]) ++i;
    return i;
}
inline u32 warp(u32 a, i32 s) {
    if (s <= 0) return a;
    const u32 i = interval(a);
    const u32 f = tables::fraction[i], g = tables::fraction[i + 1], m = tables::mediant[i];
    if (a - f < m - f) return f + warp_half(a - f, tables::inverse_low[i], m - f, s);
    return g - warp_half(g - a, tables::inverse_high[i], g - m, s);
}
// The fraction alpha rests on exactly (its index), or -1.
inline i32 resting(u32 a) {
    const u32 i = interval(a);
    if (a == tables::fraction[i]) return static_cast<i32>(i);
    if (a == tables::fraction[i + 1]) return static_cast<i32>(i + 1 < tables::fraction_count - 1 ? i + 1 : 0);
    return -1;
}
// The fraction whose basin holds alpha.
inline u32 nearest(u32 a) {
    const u32 i = interval(a);
    const u32 j = a - tables::fraction[i] < tables::mediant[i] - tables::fraction[i] ? i : i + 1;
    return j < tables::fraction_count - 1 ? j : 0;
}


// ---------------------------------------------------------------- the spectrum

// What a note plays: each partial's energy and the struck site's weight,
// and the key, the lowest band's middle.
struct Spectrum {
    i32 energy[partials];   // Q12, rising
    i32 weight[partials];   // |psi_k(s)|^2, Q16; they sum to about one
    i32 key;                // Q12
};

// Where flux and coupling fall on the tables' grid (128ths of a turn and
// the fractions to q = 8, so a snapped flux lands on an exact spectrum): the
// corner and the fractions across (Q15).
struct Grid {
    u32 corner;   // (flux step x couplings + coupling) of the lower corner
    i32 across;   // across flux, Q15
    i32 up;       // across coupling, Q15
};
inline Grid grid(u32 alpha, i32 lambda) {
    Grid g;
    const u32 a = alpha > 0x80000000u ? 0u - alpha : alpha;   // the spectrum mirrors about 1/2
    u32 i = tables::flux_first[(a >> 23) < 255u ? a >> 23 : 255u];
    while (i + 2 < tables::fluxes && a >= tables::flux_point[i + 1]) ++i;
    g.across = clamp(static_cast<i32>(mul16u(a - tables::flux_point[i], tables::flux_inverse[i]) >> 16), 0, 32767);
    u32 l = 0;
    while (l + 2 < tables::couplings && lambda >= static_cast<i32>(tables::coupling_q12[l + 1])) ++l;
    const i32 low = tables::coupling_q12[l];
    g.up = clamp(((lambda - low) * tables::coupling_inverse[l]) >> 12, 0, 32767);
    g.corner = i * tables::couplings + l;
    return g;
}
inline i32 mix(i32 a, i32 b, i32 t) { return a + (((b - a) * t) >> 15); }   // t Q15

// State k's |psi| at distance d (0..8) from the middle, Q15, at the grid point.
inline i32 amplitude_at(const Grid& g, u32 k, u32 d) {
    const u32 c = tables::couplings;
    const u8* a = tables::amplitude + (g.corner * partials + k) * half_chain + d;
    const u32 next_flux = c * partials * half_chain, next_coupling = partials * half_chain;
    const i32 low = mix(a[0] << 7, a[next_flux] << 7, g.across);
    const i32 high = mix(a[next_coupling] << 7, a[next_flux + next_coupling] << 7, g.across);
    return mix(low, high, g.up);
}

// The spectrum of the chain at flux alpha (Q32 turns) and coupling lambda
// (Q12), struck at distance `site` from its middle (Q16).
inline void spectrum(u32 alpha, i32 lambda, i32 site, Spectrum& out) {
    const Grid g = grid(alpha, lambda);
    const u32 c = tables::couplings;
    const i16* e = tables::energy + g.corner * partials;
    const u32 nf = c * partials, nc = partials;
    const u32 d0 = static_cast<u32>(clamp(site >> 16, 0, static_cast<i32>(half_chain) - 1));
    const u32 d1 = d0 + 1 < half_chain ? d0 + 1 : d0;
    const i32 between = d0 + 1 < half_chain ? (site >> 1) & 0x7fff : 0;
    for (u32 k = 0; k < partials; ++k) {
        out.energy[k] = mix(mix(e[k], e[nf + k], g.across), mix(e[nc + k], e[nf + nc + k], g.across), g.up);
        const i32 a = mix(amplitude_at(g, k, d0), amplitude_at(g, k, d1), between);   // Q15
        out.weight[k] = (a * a) >> 14;                                                   // Q16
    }
    const i16* key = tables::key + g.corner;
    out.key = mix(mix(key[0], key[c], g.across), mix(key[1], key[c + 1], g.across), g.up);
}

// Where state k lives on the half-chain, for the screen: its mean distance
// from the middle (Q8 sites) and how many sites it spreads over (Q8).
inline void extent(u32 alpha, i32 lambda, u32 k, i32& centre, i32& spread) {
    const Grid g = grid(alpha, lambda);
    u32 sum = 0, moment = 0, fourth = 0;
    for (u32 d = 0; d < half_chain; ++d) {
        const u32 a = static_cast<u32>(amplitude_at(g, k, d)) >> 7;   // Q8
        const u32 w = (a * a) * (d ? 2u : 1u);                        // both halves, Q16
        sum += w;
        moment += w * d;
        fourth += ((a * a) >> 8) * ((a * a) >> 8) * (d ? 2u : 1u);    // Q16 of |psi|^4
    }
    if (!sum) sum = 1;
    centre = static_cast<i32>(mul16u(moment, static_cast<u32>(reciprocal(static_cast<i32>(sum)))) >> 8);   // moment / sum, Q8
    spread = fourth ? clamp(reciprocal(static_cast<i32>(fourth)) >> 8, 1 << 8, static_cast<i32>(partials) << 8) : static_cast<i32>(partials) << 8;
}

// ---------------------------------------------------------------- the note

// A spectrum as the note plays it: each partial's place above the key
// (0 at the key, 1 a full span of 4 + 4 lambda higher, Q16) and its level.
struct Notes {
    i32 place[partials];   // Q16
    i32 level[partials];   // Q15; they sum to at most `loudest`
    i32 participation;     // 1 / sum of the weights squared: how many partials the strike excites, Q8
};

// The levels are the weights scaled by R^(-1/4), R = sum w^2: a strike
// that excites few partials plays them a little louder each, one that
// excites many a little quieter, halfway between equal peak and equal
// power, and trimmed by 1 - 0.6 R so that a chord of pings peaks no higher
// than a chord of full bells. All 17 sum to at most `loudest`, in phase.
const i32 loudest = 32767;
const i32 level_scale = 16100;   // Q15: 17 equal weights sum to 0.97

// The key is the lowest band's middle (the states within 7% of the span
// of the lowest, about 1.7 semitones at the default spread), weighted as
// the strike excites them, so the hum (heard at its amplitude-weighted
// middle) is on the key; with none of it excited, the table's plain middle.
inline i32 key_of(const Spectrum& s, i32 lambda) {
    const i32 window = (((4 << 12) + 4 * lambda) * 2294) >> 15;   // 7% of the span, Q12
    u32 weight = 0;
    i32 moment = 0;
    for (u32 k = 0; k < partials && s.energy[k] < s.energy[0] + window; ++k) {
        const u32 w = static_cast<u32>(s.weight[k]) >> 6;   // Q10
        weight += w;
        moment += static_cast<i32>(w) * (s.energy[k] - s.energy[0]);   // Q22, at most 1024 x window x 17
    }
    if (weight < 16) return s.key;
    return s.energy[0] + (mul16(moment, reciprocal(static_cast<i32>(weight) << 6)) >> 10);   // moment / weight, Q12
}

inline void shape(const Spectrum& s, i32 lambda, Notes& out) {
    const i32 inverse_span = reciprocal(((4 << 12) + 4 * lambda) << 4);   // 1 / (4 + 4 lambda), Q16
    const i32 key = key_of(s, lambda);
    u32 sum = 0;
    for (u32 k = 0; k < partials; ++k) {
        out.place[k] = mul16((s.energy[k] - key) * 16, inverse_span);
        sum += static_cast<u32>(s.weight[k]);
    }
    if (sum < 64) sum = 64;
    const i32 inverse_sum = reciprocal(static_cast<i32>(sum));
    u32 r = 0;   // sum of normalised weights squared, Q16
    i32 w[partials];
    for (u32 k = 0; k < partials; ++k) {
        w[k] = clamp(static_cast<i32>(mul16u(static_cast<u32>(s.weight[k]), static_cast<u32>(inverse_sum))), 0, 65536);
        r += static_cast<u32>((w[k] >> 1) * (w[k] >> 1)) >> 14;
    }
    if (r < 16) r = 16;
    out.participation = clamp(reciprocal(static_cast<i32>(r)) >> 8, 0, 255 << 8);
    const i32 gain = clamp(exp2_down(-(log2_q16(r) - (16 << 16)) / 4 - (3 << 16)) << 3, 0, 1 << 17);   // R^(-1/4), Q15
    const i32 scale = mul16(static_cast<i32>((static_cast<u32>(gain) * static_cast<u32>(level_scale)) >> 15),
                            65536 - static_cast<i32>((r * 39) >> 6));
    for (u32 k = 0; k < partials; ++k) out.level[k] = static_cast<i32>((static_cast<u32>(w[k] >> 1) * static_cast<u32>(scale)) >> 15);
}

// ---------------------------------------------------------------- the gate

// The gate, in Moth's vocabulary: a vactrol whose LED follows the note,
// held at 3/4 of 1 - strike, with a strike's pulse at the note's start that
// lights it the strike's share of the way from there to open: a ping over
// the held level, a pluck at strike 100. The cell opens with a 1 ms time
// constant during the pulse (4.4 ms; more gently for a strike under 1/8),
// then settles toward the held level: up at a
// twentieth of glow's pace (an unstruck note swells in), down at glow's,
// three times faster while it is fully lit, as a CdS cell does. Each
// partial follows the gate to the power 1 + tilt x place, so the top of the
// spectrum closes first, as through a lowpass gate.
const i32 open = 1 << 30;
const i32 block_log2e = -583262;   // log2 of a block's seconds times log2 e, Q16 (log2 0.0020936)
const i32 fall_log2 = 172628;      // log2 6.21: glow is the time to fall 60 dB
const u32 pulse_blocks = 3;

// The share of a gap a block leaves, Q15, for a time constant of 2^(log2_tau) seconds.
inline i32 keep(i32 log2_tau) { return exp2_down(-static_cast<i32>(exp2_q16(block_log2e - log2_tau))); }

// d x k for d in Q30 and k in Q15, keeping d's low bits, so a long glow's
// tail falls at its own pace rather than the rounding's.
inline i32 scale_gap(i32 d, i32 k) { return (d >> 15) * k + (((d & 0x7fff) * k) >> 15); }

// The held level, Q30: 3/4 of 1 - strike, fading `blocks` after a strike
// with a time constant of 4 glows over strike squared, so a short glow's hum
// dies with its ring and a long glow's sustains, and an unstruck pad holds.
inline i32 held_level(u32 blocks, i32 struck, i32 glow) {
    const i32 held = (32767 - struck) * 24576;
    if (struck <= 0 || !blocks) return held;
    const i32 log2_t = 131072 + glow - 2 * (log2_q16(static_cast<u32>(struck)) - (15 << 16));   // log2 (4 glow / strike^2)
    const i32 per_block = static_cast<i32>(exp2_q16(block_log2e - log2_t));                   // log2 units a block, Q16
    const i32 fade = exp2_down(-static_cast<i32>(blocks) * per_block);
    return (held >> 15) * fade;
}

inline i32 gate_step(i32 g, u32 blocks, i32 struck, i32 glow) {
    const i32 held = held_level(blocks, struck, glow);
    if (struck > 0 && blocks < pulse_blocks) {
        // The pulse lights the cell the strike's share of the way from the held level to open.
        const i32 lit = held + ((open - held) >> 15) * struck;
        const i32 k = struck >= 4096 ? 7680 : 32768 - (((32768 - 7680) * struck) >> 12);   // 0.23 a block: 1 ms
        return g >= lit ? g : lit - scale_gap(lit - g, k);
    }
    if (g < held) {
        // The swell: 2 ms, plus a twentieth of glow.
        const u32 tau = 131u + exp2_q16(glow - 283247);   // Q16 seconds (log2 20 = 4.32)
        const i32 k = keep(log2_q16(tau) - (16 << 16));
        return held - scale_gap(held - g, k);
    }
    if (g > held) {
        const u32 shape = 19661u + ((45875u * (static_cast<u32>(open - g) >> 14)) >> 16);   // 0.3 lit .. 1 dark, Q16
        const i32 k = keep(glow - fall_log2 + log2_q16(shape) - (16 << 16));
        return held + scale_gap(g - held, k);
    }
    return g;
}
// partial_gate below with its gate's log2 already taken: e = log2(g >> 15) - 15 (Q16), or 1 for a dark gate.
inline i32 partial_gate_at(i32 e, i32 slope, i32 place) {
    if (e == 1) return 0;
    const i32 tp = mul16(slope, place > 0 ? place : 0);
    const i32 above = place > 16384 ? (((place - 16384) >> 2) * 87381) >> 14 : 0;
    return exp2_down(mul16(e, 65536 + tp) - ((mul16(slope, above) * 85) >> 6) + (((slope >> 2) * 21845) >> 15));
}
// A partial's share of the gate g (Q30), at its place, Q15.
inline i32 partial_gate(i32 g, i32 slope, i32 place) {
    const u32 light = static_cast<u32>(g) >> 15;   // Q15
    if (!light) return 0;
    const i32 e = log2_q16(light) - (15 << 16);    // <= 0
    const i32 tp = mul16(slope, place > 0 ? place : 0);   // tilt x place, Q16
    // And darker by tilt x 8 dB at the top, from nothing a quarter of the
    // span up, so the low partials that carry the note's loudness keep it,
    // made up by tilt x 1 dB, never above the partial's own level.
    const i32 above = place > 16384 ? (((place - 16384) >> 2) * 87381) >> 14 : 0;   // 0 to 1 over the upper 3/4, Q16
    return exp2_down(mul16(e, 65536 + tp) - ((mul16(slope, above) * 85) >> 6) + (((slope >> 2) * 21845) >> 15));
}
}
