#include "core.h"
#include "tables.h"
#include "wide.h"
#include <opxy/draw.hpp>
#include <opxy/q31.hpp>

// Mach: each note is an object flying past the listener in a straight line at
// a chosen Mach number. It emits a source signal s(tau) pitched by the key; the
// listener hears s at every retarded time tau solving t = tau + |r(tau)| (with
// the speed of sound 1), at 1/r, darkened by air with distance.
//
//   - Below Mach 1 there is one root: the familiar Doppler glide.
//   - Near Mach 1 the approach piles up into a burst.
//   - Above Mach 1 there is no root until the Mach cone arrives (an N-wave
//     boom), then two: one runs forwards through the flight and one runs
//     backwards, both sounding at once, diverging.
//
// Squaring the retarded-time equation gives a quadratic in tau,
//     h(tau) = (t - tau)^2 - (M tau)^2 - d^2 = 0,   t - tau > 0,
// which the voice solves every 16 samples by Newton steps from the last root,
// with h computed exactly in 64 bits and no square root. Between solves the
// phase advances linearly. At the cone both roots are born from the fold's
// own shape, sigma - sigma* = +-sqrt(fold (s - s*)); the listener's pitch is
// near infinite there, and the band-limiting keeps both roots silent until
// they come down into hearing, so the fold cannot click.
//
// The sources are oscillators and tables, so s(tau) is known at any tau: no
// delay line. A ground reflection is an image of the flight with its own roots.
// The engine never sees a note-off: each note launches one flight, and the
// OP-XY's amp envelope decides how much of it is heard (see README).
namespace mach {
namespace engine {
const i32 max31 = 2147483647;
const i32 unity30 = 1 << 30;
const i32 rho_scale = 11289650;      // 2^40 / substep_time: a sub-step's change in sigma as Q24 of dtau/dt
// Each source's level, Q30, matched by measured loudness: one note of a 30 m,
// 3 s flyby (at Mach 0.6 and 1.67, tune at the end) peaks near -24 LUFS over
// 400 ms, as Hexagon's synths do on a melody.
const i32 source_level[SOURCES] = {417000000, 569000000, 492000000, 367000000, 324000000};

using wide::W;
using wide::make;
using wide::mul;
using wide::umul;
using wide::shr;

// (a b) >> 30, as the Blackfin's fractional multiply gives it (one instruction).
inline i32 mul30(i32 a, i32 b) { return opxy::q31::multiply(a, b) * 2; }
inline i32 mul15(i32 a, i32 b) { return (a * b) >> 15; }
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }
inline i32 magnitude(i32 v) { return v < 0 ? (v == -max31 - 1 ? max31 : -v) : v; }
inline i32 wider(i32 a, i32 b) { a = magnitude(a); b = magnitude(b); return a > b ? a : b; }

// num / den * 2^q, saturated, without division.
inline i32 ratio(W num, W den, i32 q) { return wide::ratio(num, den, q, tables::inverse); }
inline i32 ratio(i32 num, i32 den, i32 q) { return wide::ratio(make(num), make(den), q, tables::inverse); }
inline u32 isqrt(W v) { return wide::isqrt(v, tables::inverse); }
// The same to 2^-14, quicker: for every sub-step's and block's work.
inline i32 quotient(W num, W den, i32 q) { return wide::ratio(num, den, q, tables::inverse, true); }
inline i32 quotient(i32 num, i32 den, i32 q) { return wide::ratio(make(num), make(den), q, tables::inverse, true); }

// 3u^2 - 2u^3 of u in Q16 (clamped to 0..1), Q30.
inline i32 smooth(i32 u) {
    u = clamp(u, 0, 65536);
    const i32 u2 = ((u >> 1) * (u >> 1)) >> 14;     // Q16
    const i32 u3 = ((u2 >> 1) * (u >> 1)) >> 14;    // Q16
    return (3 * u2 - 2 * u3) << 14;
}

// Fades a component out as its pitch nears Nyquist: Q15, full below 0.36 of
// the sample rate, silent from 0.45 (phase increments, Q32).
inline i32 alias(i32 increment) {
    const u32 a = static_cast<u32>(magnitude(increment));
    if (a <= 1546188226u) return 32768;
    if (a >= 1932735283u) return 0;
    return static_cast<i32>(((1932735283u - a) >> 8) * 711u >> 15);
}

// Where in a repeating period sigma falls, Q32 (period given as 1/period, Q16 per second).
inline u32 cycle(i32 since, i32 per) { return shr(mul(since, per), 10); }

// sqrt(x) for x in [0, 1] as Q30, returned as Q15: x's even power of two
// out, then a table over [1/4, 1).
inline i32 root30(u32 x) {
    if (x >= (1u << 30)) return 32768;
    if (x < 64) return 0;
    const u32 z = flight::clz(x) - 2;              // x = m 4^-(z/2) with m in [1/4, 1)
    const u32 even = z & ~1u;
    const u32 m = x << even;                       // Q30 in [2^28, 2^30)
    const u32 at = (m >> 24) - 16, frac = (m >> 8) & 0xffff;   // 48 segments
    const i32 a = tables::root[at], b = tables::root[at + 1];
    return (a + (((b - a) * static_cast<i32>(frac)) >> 16)) >> (even >> 1);
}

// Table lookup with linear interpolation (512 samples a cycle, a guard sample).
inline i32 lookup(const opxy::i16* t, u32 phase) {
    const u32 at = phase >> 23;
    const i32 frac = static_cast<i32>((phase >> 8) & 0x7fff);
    const i32 a = t[at], b = t[at + 1];
    return a + (((b - a) * frac) >> 15);
}

const opxy::i16* wave(u32 source) {
    return source == PAD ? tables::pad : source == SIREN ? tables::siren : tables::organ;
}

// The bell, a minor-third bell in tune with the key: hum, prime, tierce and
// nominal at these ratios (Q8), and their strengths (Q15).
const i32 bell_ratio[4] = {128, 256, 307, 512};
const i32 bell_strength[4] = {11469, 26214, 14746, 16384};
const u32 strike_hash[4] = {0x9e3779b9u, 0x7f4a7c15u, 0x94d049bbu, 0xbf58476du};
// The voice: three formants (Hz) of "oo", "ah" and "oh"; it opens from the
// first to the second over the first half of its flight and closes to the
// third over the second, so the backward image sings it in reverse.
const i32 vowel[3][3] = {{300, 870, 2240}, {730, 1090, 2440}, {570, 840, 2410}};
const i32 formant_strength[3] = {26214, 16384, 6554};
// The pad: two oscillators 0.3% either side of its pitch.
const i32 detune = 12885;            // 0.003, Q22

// log2 of a positive integer, Q16, to a fifth of a cent (tuning).
inline i32 log2_fine(u32 x) {
    if (!x) return 0;
    const i32 top = 31 - static_cast<i32>(flight::clz(x));
    const u32 m = top >= 21 ? x >> (top - 21) : x << (21 - top);    // 1.0 .. 2.0 in Q21
    const u32 frac = m & 0x1fffff;                                   // Q21 of the mantissa's fraction
    const u32 at = frac >> 16, part = frac & 0xffff;
    const i32 a = static_cast<i32>(tables::log2[at]), b = static_cast<i32>(tables::log2[at + 1]);
    return (top << 16) + a + (((b - a) * static_cast<i32>(part >> 4)) >> 12);
}
}

using namespace engine;

// Every word of a voice to zero, as Voice() makes it (the SDK's memset runs a byte at a time).
inline void clear(Voice& v) {
    u32* w = reinterpret_cast<u32*>(&v);
    for (u32 i = 0; i < sizeof(Voice) / 4; ++i) w[i] = 0;
}

inline void initialize(Voice& voice, Shared& shared, u32) {
    if (voice.boom_held && shared.booming) --shared.booming;
    clear(voice);
    voice.done = 1;
}

inline void start(Voice& voice, Shared& shared, u32) {
    // A note taken over mid-boom leaves the count of booms in progress.
    if (voice.boom_held && shared.booming) --shared.booming;
    voice.boom_held = 0;
    // start() has no controls: the first block after note-on sets the flight up.
    voice.fresh = 1;
    voice.done = 0;
}

inline void destroy(Voice& voice, Shared& shared, u32) {
    if (voice.boom_held && shared.booming) --shared.booming;
    voice.boom_held = 0;
    voice.done = 1;
}

// The source's phase at emission time `since` after the flight starts:
// f * since, exactly modulo one cycle (f as a phase increment a sample).
inline u32 phase_at(u32 f, i32 since) {
    const W n = mul(since, 44100);                    // samples, Q26
    const u32 whole = shr(n, 26), frac = n.lo & 0x3ffffffu;
    return f * whole + shr(umul(f, frac), 26);
}

// sqrt(M^2 - 1), Q28, and M^2 - 1, Q27 (above Mach 1).
inline i32 cone_root(i32 mach, i32& k27) {
    k27 = static_cast<i32>(shr(mul(mach, mach), 29)) - (1 << 27);
    return static_cast<i32>(isqrt(wide::shl(make(k27), 29)));
}

// The cone, the first arrival and the boom of one image.
inline void prepare(Voice& v, Image& im) {
    im.boom_at = -1;
    if (v.mach > (1 << 28) + (1 << 20)) {
        i32 k27;
        const i32 root = cone_root(v.mach, k27);                                      // Q28
        const i32 reach = static_cast<i32>(shr(mul(v.mach, root), 29));               // M sqrt(M^2 - 1), Q27
        const i32 star = -ratio(im.d, reach, 27);                                     // Q26
        if (star + im.tau0 > v.sigma_start - 67109) {   // at most 1 ms before the flight
            im.cone = 1;
            im.sigma_star = star;
            im.s_star = ratio(mul(im.d, root), make(v.mach), 0);
            // (sigma - sigma*)^2 = fold (s - s*) near the fold: fold = 2 d M / (M^2 - 1)^1.5
            const i32 k24 = static_cast<i32>(shr(mul(k27, root), 31));               // (M^2 - 1)^1.5, Q24 (22.6 at Mach 3)
            const i32 fold = ratio(mul(2 * im.d, v.mach), make(k24), -4);            // Q26
            im.fold = fold >> 10;
            // The images come in as the fold's sweep slows, over fold / 30
            // (about where dtau/dt falls through 3), and at least 40 ms: the
            // boom is the attack; the tone swells in after it, so a chord's
            // notes, all at their loudest just after the cone, don't pile up.
            const i32 rise = fold / 30 > 2684355 ? fold / 30 : 2684355;              // Q26 seconds
            im.onset = ratio(wide::power(42), make(rise), 0);
            im.rise = rise;
            im.birth = im.tau0 + im.s_star;
            // The N-wave's length, shared with the screen.
            const i32 ld = flight::log2_q16(static_cast<u32>(im.d)) - flight::log2_q16(3912100u);
            im.boom_length = clamp((flight::nwave(v.mach >> 12, im.d >> 10) * 44100) >> 16, 132, 2205);
            im.boom_at = static_cast<i32>(shr(mul(im.birth - v.t_start, 44100), 26));
            // Louder nearer: (20 m / d)^1/4.
            const i32 near = clamp(flight::exp2_q16(clamp((4 << 16) - (ld >> 2), 0, 13 << 16)) >> 4, 22938, 98304);   // Q16
            im.boom_scale = mul30(near << 14, im.level);
            const i32 end = im.boom_at + im.boom_length + 2205;
            if (end > v.boom_end) v.boom_end = end;
            return;
        }
    }
    const i32 sigma0 = v.sigma_start - im.tau0;
    const i32 x = static_cast<i32>(shr(mul(v.mach, sigma0), 28));
    const i32 r = static_cast<i32>(isqrt(wide::add(mul(x, x), mul(im.d, im.d))));
    im.birth = im.tau0 + sigma0 + r;
    // Without a cone the boom is the bow wave's pressure pulse as the object
    // passes (when the pass is in its flight): an N-wave a Mach 1.4 shock's
    // length, heard with the pass, at 0.4 + 0.6 M^2 of a shock's level.
    if (sigma0 < 0 && v.sigma_end - im.tau0 > 0) {
        const i32 ld = flight::log2_q16(static_cast<u32>(im.d)) - flight::log2_q16(3912100u);
        im.boom_length = clamp((flight::nwave(91750, im.d >> 10) * 44100) >> 16, 132, 2205);
        im.boom_at = static_cast<i32>(shr(mul(im.tau0 + im.d - v.t_start, 44100), 26));
        const i32 near = clamp(flight::exp2_q16(clamp((4 << 16) - (ld >> 2), 0, 13 << 16)) >> 4, 22938, 98304);   // Q16
        const i32 m2 = clamp(static_cast<i32>(shr(mul(v.mach, v.mach), 26)), 0, unity30);              // M^2, Q30
        im.boom_scale = mul30(mul30(near << 14, 429496730 + mul30(644245094, m2)), im.level);
        const i32 end = im.boom_at + im.boom_length + 2205;
        if (end > v.boom_end) v.boom_end = end;
    }
    // dsigma/dt = R / (R + M x) at the first sound, Q24 below
    const i32 den = (r >> 2) + static_cast<i32>(shr(mul(v.mach, x), 30));
    im.birth_step = den > 0 ? ratio(mul(substep_time, r >> 2), make(den), 0) : substep_time;
}

// The flight's two landing points, Q26 seconds of its own time from the
// pass: its start (side -1) and its end (side 1), but no further from the pass
// than half a second or the time it takes to fly four closest distances
// (where the glide is within a few cents of its end), so a played note (not
// the whole flight) is in tune through its sustain.
inline i32 landing(const Voice& v, i32 d, i32 side) {
    const i32 most = wider(33554432, ratio(make(d), make(v.mach), 30));
    return side < 0 ? (v.sigma_start > -most ? v.sigma_start : -most) : (v.sigma_end < most ? v.sigma_end : most);
}

// log2 of the rate the flight is heard at for its emission at sigma (Q26),
// dtau/dt = r / |r + M x|, Q16.
inline i32 heard(const Voice& v, i32 d, i32 sigma) {
    const i32 x = static_cast<i32>(shr(mul(v.mach, sigma), 28));
    const i32 r = static_cast<i32>(isqrt(wide::add(mul(x, x), mul(d, d))));
    const i32 den = (r >> 2) + static_cast<i32>(shr(mul(v.mach, x), 30));      // r (1 - M cos theta), Q24
    return log2_fine(static_cast<u32>(r >> 2)) - log2_fine(static_cast<u32>(den < 0 ? -den : den > 0 ? den : 1));
}

// tune: the source's pitch over the key. At -1 the flight's start sounds the
// key exactly (above Mach 1, where the backward image ends), at 1 its end;
// between them the source moves evenly in pitch, so the key sounds where the
// glide passes through it. Held within 2.5 octaves.
inline void tune(Voice& v, i32 d, i32 u) {
    const i32 most = 5 << 15;
    const i32 first = clamp(heard(v, d, landing(v, d, -1)), -most, most), last = clamp(heard(v, d, landing(v, d, 1)), -most, most);
    const i32 octaves = first + ((((last - first) >> 2) * ((u + 65536) >> 3)) >> 12);
    v.pitch = flight::exp2_q16(3 * 65536 - octaves) >> 3;
}

// Sets the flight up from the controls at note-on.
inline void setup(Voice& v, opxy::Parameters p) {
    const u32 fresh_source = flight::source(p[SOURCE]);
    clear(v);
    v.source = fresh_source;
    const i32 m16 = flight::mach(p[MACH]);
    v.mach = m16 << 12;
    const i32 metres = flight::pass_metres(p[PASS]);
    const i32 d = ratio(metres, 343, 10);                                        // Q26 sound-seconds
    const i32 length = flight::dur(p[DUR]);
    const i32 u = flight::tune(p[TUNE]);
    const i32 before = flight::approach(length, u);
    v.sigma_start = -(before << 10);
    v.sigma_end = (length - before) << 10;
    // The flight fades in over half its approach (at most 0.25 s) and out over
    // the last sixth of its departure (at most 0.25 s).
    const i32 fade = clamp(before >> 1, 1311, 16384), fade_out = clamp((length - before) / 6, 1311, 16384);
    v.fade = ratio(wide::power(32), make(fade), 0);
    v.fade_out = ratio(wide::power(32), make(fade_out), 0);
    v.fade_span = fade << 10;
    v.fade_out_span = fade_out << 10;
    v.loud = unity30 - flight::q16(p[PASS]) * 7373;   // 300 m is 6 dB quieter than 3 m at its closest
    // The bell is struck as the flight begins and again at its closest pass,
    // each ring lasting until the next strike or the flight's end; the siren
    // winds up every 0.9 s; the voice's vowel spans the flight.
    const i32 period = v.source == BELL ? clamp(before, 3277, length) : v.source == SIREN ? 58982 : length;
    v.period = ratio(wide::power(32), make(period), 0);
    v.period_samples = shr(mul(period, 44100), 16);
    v.ring = ratio(wide::power(32), make(clamp(length - before, 3277, length)), 0);
    const flight::Timeline line = flight::timeline(m16, flight::sound_seconds(metres), length, before);
    v.t = v.t_start = line.start << 10;

    Image& direct = v.image[0];
    direct.d = d;
    v.log_d = log2_fine(static_cast<u32>(d));
    direct.level = unity30;
    prepare(v, direct);
    if (direct.cone) {
        i32 k27;
        v.boom_r = ratio(mul(d, v.mach), make(cone_root(v.mach, k27)), 0);    // where the cone left: d M / sqrt(M^2 - 1)
    } else v.boom_r = d;
    // The air's corner at the closest pass, over its corner at 12 m: (12 m / d)^1/4,
    // Q16; and the boom's, over the pass's: sqrt(d / r) where the boom left, Q30.
    v.air_near = flight::exp2_q16(((log2_fine(2347812u) - v.log_d) >> 2) + (4 << 16)) >> 4;
    v.boom_near = flight::exp2_q16(((v.log_d - log2_fine(static_cast<u32>(v.boom_r))) >> 1) + (14 << 16));

    const i32 echo = flight::echo_level(p[ECHO]);
    Image& ground = v.image[1];
    ground.boom_at = -1;
    if (echo > 0) {
        // The listener stands h over flat ground under a level flight: the
        // image of the flight in the ground passes at d + 2h, at the same time.
        ground.d = d + 2 * ratio(flight::height_metres(p[ECHO]), 343, 10);
        ground.tau0 = 0;
        ground.level = echo << 15;
        prepare(v, ground);
    }
    tune(v, d, u);
    // The boom: from a soft, long thump (a rise at 100 Hz, three times the
    // shock's length) to the full crack (10 kHz), its level made up for the
    // energy the slower rise takes; off at 0, full level from an eighth up.
    const i32 b = flight::amount(p[BOOM]);
    const i32 w = static_cast<i32>(shr(mul(934, flight::exp2_q16((b * 13607) >> 10)), 16));    // 2 pi 100 Hz / fs * 100^b, Q16
    v.boom_rise = ratio(w, (1 << 16) + w, 31);
    v.boom_gain = mul30(smooth(b << 3), unity30 + ((32768 - b) << 15));
    for (u32 m = 0; m < 2; ++m)
        if (v.image[m].boom_at >= 0) {
            v.image[m].boom_length = v.image[m].boom_length * (3 * 32768 - 2 * b) >> 15;
            const i32 end = v.image[m].boom_at + v.image[m].boom_length + 2205;
            if (end > v.boom_end) v.boom_end = end;
        }
    for (u32 r = 0; r < 4; ++r) {
        Root& root = v.root[r];
        root.image = r >> 1;
        root.reversed = r & 1;
        const Image& im = v.image[root.image];
        root.state = (root.image && echo <= 0) || (root.reversed && !im.cone) ? DONE : WAITING;
    }
}

// The bell's clock: where in its ring the emission at sigma falls, Q32. It is
// struck as the flight begins and at its closest pass; each ring runs to the
// next strike (or the flight's end).
inline u32 strike_clock(const Voice& v, i32 sigma) {
    return sigma < 0 ? cycle(sigma - v.sigma_start, v.period) : cycle(sigma, v.ring);
}

// The source's pitch: the key's, times tune's factor (a phase increment).
inline u32 pitched(const Voice& v, u32 f) {
    const W p = umul(f, static_cast<u32>(v.pitch));
    return p.hi >= 65536 ? 0xffffffffu : shr(p, 16);
}

// A root comes into hearing: at the cone (both roots, from sigma*) or, with
// no cone, as the flight's first sound arrives.
inline void born(Voice& v, Root& root, u32 f) {
    const Image& im = v.image[root.image];
    if (im.cone) {
        root.state = FOLD;
        root.sigma = im.sigma_star + im.tau0;
        root.step = root.bend = 0;
    } else {
        root.state = TRACKING;
        root.step = im.birth_step;
        root.bend = 0;
        // Where the root stood a sub-step before now, for solve()'s prediction.
        root.sigma = v.sigma_start + ratio(mul(v.t - im.birth, im.birth_step), make(substep_time), 0) - im.birth_step;
    }
    const i32 since = root.sigma - v.sigma_start;
    const u32 pf = pitched(v, f);
    // The source's phase origin is arbitrary: hashed from the key, so a chord's
    // notes, born at one instant of one flight, don't start in step.
    const u32 origin = pf * 0x9e3779b9u;
    const u32 spread = shr(umul(pf, detune), 22);
    root.phase = origin + phase_at(v.source == PAD ? pf - spread : pf, since);
    if (v.source == PAD) root.part[0] = origin * 3 + phase_at(pf + spread, since);
    root.gain = root.slope = root.low = 0;
    if (v.source == BELL) {
        root.clock = strike_clock(v, root.sigma);
        // Each partial's phase: its strike phase, plus its pitch times the
        // time since the latest strike (at the flight's start, or its pass).
        const i32 age = root.sigma < 0 ? root.sigma - v.sigma_start : root.sigma;
        for (u32 k = 0; k < 4; ++k) {
            root.offset[k] = pf * strike_hash[k];
            root.part[k] = root.offset[k] + phase_at(shr(umul(pf, static_cast<u32>(bell_ratio[k])), 8), age);
        }
    }
}


// The output's ceiling (half scale, before doubling: 2^29): linear to 0.7 of
// it, then a soft knee, e - e^2 / 2 over the next 0.6, flat from 1.3 times it.
inline i32 knee(i32 y) {
    const i32 start = 375809638, span = 322122547;     // 0.7 and 0.6 of 2^29
    // |y| without a branch; as unsigned, -2^31 reads 2^31, past the knee as magnitude()'s 2^31 - 1 is.
    const i32 sign = y >> 31;
    const u32 a = static_cast<u32>((y ^ sign) - sign);
    if (a <= static_cast<u32>(start)) return y;
    const i32 e = a >= static_cast<u32>(start + span) ? 32768 : ((static_cast<i32>(a - start) >> 12) * 6827) >> 14;   // (a - start) / span, Q15
    const i32 out = start + (span >> 15) * (e - ((e * e) >> 16));
    return y < 0 ? -out : out;
}

// What a block's pitch and air set for every root.
struct Block {
    u32 f;            // the key's pitch, a phase increment a sample
    i32 inv;          // voice: 97392 / f, Q32 (a formant in Hz over the fundamental)
    u32 part_end[4];  // bell: each partial's phase at the end of a strike period
    i32 air;          // the air's corner at the closest pass as w = 2 pi f / fs, Q16 (-1: open) ...
    i32 thick;        // ... and how fast it takes the level with distance: (d / r)^thick, Q16 (0.5 .. 1)
};

// The compression gain 1.25 rho / (0.25 + rho), Q30, from |rho| as Q24.
inline i32 doppler(i32 speed) {
    if (speed >= (8 << 24)) return tables::doppler[128];
    const u32 at = static_cast<u32>(speed) >> 20, frac = (static_cast<u32>(speed) >> 8) & 0xfff;
    const i32 a = tables::doppler[at], b = tables::doppler[at + 1];
    return a + ((b - a) >> 12) * static_cast<i32>(frac);
}

// The air's one-pole coefficient w / (1 + w), Q31, from w as Q16.
inline i32 absorb(i32 w) {
    if (w >= (32 << 16)) return max31;
    const u32 at = static_cast<u32>(w) >> 13, frac = (static_cast<u32>(w) >> 1) & 0xfff;
    const i32 a = tables::open[at], b = tables::open[at + 1];
    return max31 - ((a + (((b - a) * static_cast<i32>(frac)) >> 12)) << 16);
}

// Solves one root for the end of this sub-step and sets how it sounds over it.
inline void solve(Voice& v, Root& root, const Block& block) {
    const u32 f = block.f;
    const Image& im = v.image[root.image];
    const i32 s = v.t - im.tau0;
    const i32 old = root.sigma;
    const i32 star = im.sigma_star;
    const bool fresh = root.gain == 0 && root.slope == 0;
    i32 sig;
    int iterations = 1;
    if (root.state == FOLD) {
        // Near the cone the roots part as the square root of time: the first
        // guess; after it, from the last sub-step (the law holds only near
        // the cone). Newton steps until it settles.
        if (root.step) sig = old - im.tau0 + root.step;
        else {
            const i32 since = s > im.s_star ? s - im.s_star : 0;
            const i32 off = static_cast<i32>(wide::isqrt(wide::shl(mul(im.fold, since), 10), tables::inverse, true));
            sig = root.reversed ? star - off : star + off;
        }
        iterations = 6;
    } else {
        // Predicted from the last two sub-steps; Newton steps until it settles
        // (one, once a root is running smoothly).
        sig = old - im.tau0 + root.step + root.bend;
        iterations = 4;
    }
    for (int i = 0; i < iterations; ++i) {
        const i32 r = s - sig;
        const i32 x = static_cast<i32>(shr(mul(v.mach, sig), 28));
        const W h = wide::sub(wide::sub(mul(r, r), mul(x, x)), mul(im.d, im.d));    // Q52, exact
        const i32 den = (r >> 2) + static_cast<i32>(shr(mul(v.mach, x), 30));       // r + M x, Q24
        // Newton: sigma += h / (2 (r + M x)); Q52 / Q24 is Q28, so 2^-3 for Q26 and the half.
        const i32 delta = clamp(quotient(h, make(den), -3), -(1 << 25), 1 << 25);
        sig += delta;
        if (im.cone && (root.reversed ? sig > star : sig < star)) sig = star;
        if (sig > s - 1024) sig = s - 1024;   // never later than now
        // Settled: within half a microsecond, or (tracking, from a close
        // prediction) near enough that Newton's next step would be smaller still.
        if (magnitude(delta) < (root.state == FOLD || !root.bend ? 32 : 1024)) break;
    }
    const i32 now = sig + im.tau0;
    root.bend = root.state == FOLD ? 0 : now - old - root.step;
    root.step = now - old;
    root.sigma = now;
    // Out of the fold once it runs slower than 4 seconds a second, and is past
    // where the fold's own law, (sigma - sigma*)^2 = fold (s - s*), says so
    // (|sigma - sigma*| > fold / 8).
    if (root.state == FOLD && magnitude(root.step) < 4 * substep_time && magnitude(sig - star) > (im.fold << 7)) root.state = TRACKING;

    // How fast the source's time runs as heard (dtau/dt), Q24, at the
    // sub-step's end: the step is its mean over the sub-step, and half the
    // bend carries it on to the end, so a ramp from the last sub-step's end
    // to this one averages to the exact step.
    const i32 rho = wide::shr_within(mul(root.step + (root.bend >> 1), rho_scale), 16, 1 << 30);
    const i32 speed = magnitude(rho);
    // Partials fade by the higher pitch at either end of the sub-step's ramp,
    // so none sweeps down from above Nyquist into hearing while still sounding.
    const i32 fastest = !fresh && root.speed > speed ? root.speed : speed;
    root.speed = speed;
    const i32 r = s - sig;
    i32 g = mul30(im.level, v.loud);
    g = mul30(g, source_level[v.source]);
    // Distance: the true law is d / r, but a flyby's tail (and above Mach 1
    // both images, emitted far away) would then fall out of hearing; in open
    // air the voice falls as sqrt(d / r), 3 dB a doubling, and thick air
    // takes it toward d / r, 6 dB.
    // sqrt(d / r) for the air's corner, and the level's fall, from their
    // logarithms (Q16; exp2 of x + 14 is 2^x as Q30).
    const i32 fall = clamp(v.log_d - log2_fine(static_cast<u32>(r)), -(13 << 16), 0);
    const i32 near = flight::exp2_q16((fall >> 1) + (14 << 16));
    g = mul30(g, flight::exp2_q16(((fall >> 4) * (block.thick >> 4) >> 8) + (14 << 16)));
    g = mul30(g, doppler(speed));                                                  // compression, up to 1.25x
    // The window's fades, and the images' onset at the cone, where they act.
    if (now - v.sigma_start < v.fade_span) g = mul30(g, smooth(wide::shr_within(mul(now - v.sigma_start, v.fade), 26, 1 << 20)));
    if (v.sigma_end - now < v.fade_out_span) g = mul30(g, smooth(wide::shr_within(mul(v.sigma_end - now, v.fade_out), 26, 1 << 20)));
    // At the cone the two images are born together at the same value, and
    // their amplitude there is singular: they come in over 4 ms instead of at a
    // step (the boom covers the moment).
    if (im.cone && s - im.s_star < im.rise) g = mul30(g, smooth(wide::shr_within(mul(s - im.s_star, im.onset), 26, 1 << 20)));

    const u32 pf = pitched(v, f);
    const W heard_inc = umul(pf, wide::magnitude(rho));
    i32 inc = wide::shr_within(heard_inc, 24, max31);
    if (rho < 0) inc = -inc;
    const i32 since = now - v.sigma_start;
    i32 amp = 32768;
    if (v.source == VOICE) {
        // The vowel: "oo" opening to "ah", closing to "oh", over the flight.
        const i32 u = clamp(wide::shr_within(mul(since, v.period), 26, 1 << 20), 0, 65535);
        const u32 from = u < 32768 ? 0 : 1;
        const i32 glide = smooth((u - static_cast<i32>(from) * 32768) << 1);
        const i32 inv = block.inv;
        root.sweeping = 0;
        for (u32 k = 0; k < 3; ++k) {
            const i32 hz = vowel[from][k] + mul30(vowel[from + 1][k] - vowel[from][k], glide);
            root.ratio[k] = shr(umul(static_cast<u32>(hz), static_cast<u32>(inv)), 16);
            const i32 heard = wide::shr_within(umul(static_cast<u32>(hz * 97392), static_cast<u32>(fastest)), 24, max31);
            const i32 a = mul15(formant_strength[k], alias(heard));
            root.amp_was[k] = fresh ? a : root.part_amp[k];
            root.part_amp[k] = a;
            if (magnitude(a - root.amp_was[k]) > 512) root.sweeping = 1;
        }
        amp = mul15(amp, alias(wider(inc, fresh ? inc : root.inc)));
    } else if (v.source == BELL) {
        // Hum, prime and nominal are 1, 2 and 4 times one phase (part[0]); the
        // tierce has its own (part[2]). A strike lands on a sub-step's start:
        // the ring before it closes over the sub-step before (heard
        // backwards, the ring after it), and the new ring opens over the
        // sub-step from its strike phases. The low ring's envelope rides the
        // gain's ramp; the high ring's, over it, the partials' amplitudes.
        const u32 clock = strike_clock(v, now);
        const i32 travel = static_cast<i32>(clock - root.clock);
        const bool struck = !fresh && (travel >= 0 ? clock < root.clock : clock > root.clock);
        // The next sub-step strikes if it carries the clock past a ring's end
        // (a clock that jumped rather than ran says nothing).
        const u32 next = clock + static_cast<u32>(travel);
        const bool closing = magnitude(travel) < (1 << 28) && (travel >= 0 ? next < clock : next > clock);
        root.clock = clock;
        const u32 at = clock >> 22;
        root.sweeping = magnitude(travel) >= (1 << 28);
        for (u32 k = 0; k < 4; ++k) {
            const i32 to = k == 0 ? inc >> 1 : k == 1 ? inc : k == 3 ? clamp(inc, -(max31 >> 1), max31 >> 1) << 1
                         : wide::shr_within(mul(inc, bell_ratio[k]), 8, max31);
            if (fresh) root.part_inc[k] = to;
            i32 a = mul15(bell_strength[k], alias(wider(to, root.part_inc[k])));
            if (k >= 2) a = mul15(a, tables::bell_tone[at]);
            root.amp_was[k] = fresh || struck ? a : root.part_amp[k];
            root.part_amp[k] = a;
            root.part_to[k] = to;
            // A partial sweeping through the top of the band, a pitch changing
            // fast (at the fold) or a clock that jumped ramps sample by sample.
            if (magnitude(a - root.amp_was[k]) > 512 || magnitude((to >> 6) - (root.part_inc[k] >> 6)) > magnitude(to >> 12)) root.sweeping = 1;
        }
        for (u32 k = 0; k < 4; k += 2) {
            root.part_end[k] = block.part_end[k] + root.offset[k];
            if (struck) root.part[k] = travel >= 0 ? root.offset[k] : root.part_end[k];
        }
        if (struck) root.gain = 0;
        amp = closing ? 0 : tables::bell_low[at];
    } else {
        if (v.source == SIREN) {
            // The siren winds up into its note: a semitone below, rising over
            // the first 30% of every 0.9 s and holding the key for the rest.
            const i32 u = static_cast<i32>(cycle(since, v.period) >> 16);
            const i32 e = u < 19661 ? -(5461 - ((5461 * (smooth((u * 54613) >> 14) >> 15)) >> 15)) : 0;   // u / 0.3
            const i32 m = flight::exp2_q16(e + 65536) >> 1;
            inc = wide::shr_within(mul(inc, m), 16, max31);
        }
        // The table level the heard pitch allows: harmonics up to a quarter of
        // the sample rate at full weight, crossfading to the next level.
        const i32 p = flight::log2_q16(static_cast<u32>(wider(inc, fresh ? inc : root.inc))) - (22 << 16);
        u32 level = 0;
        i32 mix = 0;
        if (p >= (8 << 16)) {
            level = 8;
            amp = p >= (9 << 16) ? 0 : 32768 - ((p & 0xffff) >> 1);
        } else if (p > 0) { level = static_cast<u32>(p >> 16); mix = (p & 0xffff) >> 1; }
        if (v.source == PAD || root.image) {
            // The pad's soft top needs no crossfade: each oscillator reads the
            // next level up whole, and the two sit 0.3% either side. The
            // ground's reflection does the same with one oscillator: the
            // ground takes the highs.
            if (mix && level < 8) ++level;
            mix = 0;
            // One oscillator as loud as the pad's two, 0.3% apart (1 / sqrt 2).
            if (v.source == PAD && root.image) amp = 23170;
        }
        if (v.source == PAD && !root.image) {
            const i32 spread = static_cast<i32>(shr(mul(inc, detune), 22));
            const i32 sharp = inc + spread;
            inc -= spread;
            if (fresh) root.part_inc[0] = sharp;
            root.part_to[0] = sharp;
        }
        // The crossfade moves across the sub-step, except when the level
        // changes (the tables' crossfade meets itself there: a level at full
        // mix is the next level at none).
        if (fresh || level != root.level) { root.level = level; root.mix = mix << 5; }
        // A pitch changing fast (at the fold) ramps sample by sample.
        root.sweeping = !fresh && magnitude((inc >> 6) - (root.inc >> 6)) > magnitude(inc >> 12);
        root.mix_to = mix << 5;
    }
    // The pitch moves across the sub-step rather than stepping at its start.
    if (fresh) root.inc = inc;
    root.inc_to = inc;
    g = mul30(g, amp << 15);
    const bool past = root.reversed ? now < v.sigma_start : (now > v.sigma_end && root.step > 0);
    if (past) g = 0;
    root.slope = (g - root.gain) >> substep_bits;
    root.coef = block.air < 0 ? max31 : absorb(mul30(block.air, near));
    if (past && root.gain == 0) root.state = DONE;
}

// A value held over the c-th of 2^bits pieces of a sub-step, moving from
// `from` to `to`: at the piece's end (an amplitude) or its mean (a phase step).
inline i32 held(i32 from, i32 to, u32 c, u32 bits, bool mean) {
    const i32 change = (to >> 1) - (from >> 1);
    return from + ((change >> bits) * static_cast<i32>(mean ? 2 * c - 1 : 2 * c));
}

// The sample loops. Each is a function of its own, not inlined into the
// dispatch, and keeps its root's state in locals: on the Blackfin that keeps
// it in registers, a zero-overhead loop without stack traffic. Each adds its
// root's sound into out, through the root's gain ramp and air.
#define MACH_LOOP __attribute__((noinline))

// The bell's ring over n samples at held steps and amplitudes: the hum, its 2x
// and 4x (prime and nominal) and the tierce. The ground's reflection keeps the
// low ring (hum and prime): the ground takes the highs, and the voice half the
// cost.
template <int n, bool image>
MACH_LOOP void rung(Root& root, i32* out, i32 step0, i32 step2, const i32* amp) {
    u32 hum = root.part[0], tierce = root.part[2];
    const opxy::i16 a0 = static_cast<opxy::i16>(amp[0]), a1 = static_cast<opxy::i16>(amp[1]), a2 = static_cast<opxy::i16>(amp[2]), a3 = static_cast<opxy::i16>(amp[3]);
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    for (i32 i = 0; i < n; ++i) {
        hum += static_cast<u32>(step0);
        i32 x = tables::sine[hum >> 20] * a0 + tables::sine[(hum << 1) >> 20] * a1;
        if (!image) {
            tierce += static_cast<u32>(step2);
            x += tables::sine[tierce >> 20] * a2 + tables::sine[(hum << 2) >> 20] * a3;
        }
        x >>= 14;
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
    }
    root.part[0] = hum;
    root.part[2] = tierce;
    root.gain = g;
    root.low = low;
}

// The voice over n samples: formant bursts under the pulse's window. The
// fundamental steps at its mean over the sub-step (so it ends where a ramp
// would); each formant's phase is the fundamental's times its ratio, exact at
// the sub-step's start and at every period's wrap, stepped between. The
// ground's reflection keeps the first formant: the ground takes the highs.
template <int formants, int n, bool forward>
MACH_LOOP void sung(Root& root, i32* out, u32 rise, const i32* inc, const i32* amp, u32* f) {
    const u32 r0 = root.ratio[0], r1 = root.ratio[1], r2 = root.ratio[2];
    const opxy::i16 a0 = static_cast<opxy::i16>(amp[0]), a1 = static_cast<opxy::i16>(amp[1]), a2 = static_cast<opxy::i16>(amp[2]);
    const u32 i0 = static_cast<u32>(inc[0]), i1 = static_cast<u32>(inc[1]), i2 = static_cast<u32>(inc[2]);
    u32 ph = root.phase, f0 = f[0], f1 = f[1], f2 = f[2];
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    for (i32 i = 0; i < n; ++i) {
        const u32 before = ph;
        ph += rise;
        if (forward ? ph < before : ph > before) {
            f0 = shr(umul(ph, r0), 16);
            if (formants > 1) f1 = shr(umul(ph, r1), 16);
            if (formants > 2) f2 = shr(umul(ph, r2), 16);
        } else {
            f0 += i0;
            if (formants > 1) f1 += i1;
            if (formants > 2) f2 += i2;
        }
        const i32 w = tables::pulse[ph >> 22];
        i32 sum = tables::sine[f0 >> 20] * a0;
        if (formants > 1) sum += tables::sine[f1 >> 20] * a1;
        if (formants > 2) sum += tables::sine[f2 >> 20] * a2;
        const i32 x = ((sum >> 15) * w) >> 15;
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
    }
    root.phase = ph;
    f[0] = f0; f[1] = f1; f[2] = f2;
    root.gain = g;
    root.low = low;
}

// A wavetable source: two levels crossfading.
template <int n>
MACH_LOOP void played(Root& root, i32* out, const opxy::i16* t0, const opxy::i16* t1, i32 inc, i32 mix) {
    u32 ph = root.phase;
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef, fade = mix >> 5;
    for (i32 i = 0; i < n; ++i) {
        ph += static_cast<u32>(inc);
        const i32 a = lookup(t0, ph), b = lookup(t1, ph);
        const i32 x = a + (((b - a) * fade) >> 15);
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
    }
    root.phase = ph;
    root.gain = g;
    root.low = low;
}

// The pad: two oscillators on one level, 0.3% apart. The ground's reflection
// of a wavetable source (sharp 0): one level, one oscillator.
template <int n>
MACH_LOOP void bowed(Root& root, i32* out, const opxy::i16* t0, i32 inc, i32 sharp) {
    u32 ph = root.phase, ph2 = root.part[0];
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    for (i32 i = 0; i < n; ++i) {
        ph += static_cast<u32>(inc);
        i32 x = lookup(t0, ph);
        if (sharp) {
            ph2 += static_cast<u32>(sharp);
            x = (x + lookup(t0, ph2)) >> 1;
        }
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
    }
    root.phase = ph;
    root.part[0] = ph2;
    root.gain = g;
    root.low = low;
}

// While a root sweeps, its steps and amplitudes move sample by sample: the
// same values sound() gave 64 one-sample pieces (held() at c = 1..64, an
// arithmetic progression), stepped here in one loop instead of 64 calls.
template <bool image>
MACH_LOOP void rung_swept(Root& root, i32* out, i32 s0, i32 d0, i32 s2, i32 d2, const i32* from, const i32* step) {
    u32 hum = root.part[0], tierce = root.part[2];
    i32 v0 = from[0] + 2 * step[0], v1 = from[1] + 2 * step[1], v2 = from[2] + 2 * step[2], v3 = from[3] + 2 * step[3];
    const i32 e0 = 2 * step[0], e1 = 2 * step[1], e2 = 2 * step[2], e3 = 2 * step[3];
    i32 st0 = s0 + d0, st2 = s2 + d2;
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    for (i32 i = 0; i < substep; ++i) {
        hum += static_cast<u32>(st0);
        i32 x = tables::sine[hum >> 20] * (v0 >> 1) + tables::sine[(hum << 1) >> 20] * (v1 >> 1);
        if (!image) {
            tierce += static_cast<u32>(st2);
            x += tables::sine[tierce >> 20] * (v2 >> 1) + tables::sine[(hum << 2) >> 20] * (v3 >> 1);
        }
        x >>= 14;
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
        st0 += 2 * d0; st2 += 2 * d2;
        v0 += e0; v1 += e1; v2 += e2; v3 += e3;
    }
    root.part[0] = hum;
    root.part[2] = tierce;
    root.gain = g;
    root.low = low;
}
template <int formants, bool forward>
MACH_LOOP void sung_swept(Root& root, i32* out, u32 rise, const i32* inc, const i32* from, const i32* step, u32* f) {
    const u32 r0 = root.ratio[0], r1 = root.ratio[1], r2 = root.ratio[2];
    i32 a0 = from[0] + 2 * step[0], a1 = from[1] + 2 * step[1], a2 = from[2] + 2 * step[2];
    const i32 e0 = 2 * step[0], e1 = 2 * step[1], e2 = 2 * step[2];
    const u32 i0 = static_cast<u32>(inc[0]), i1 = static_cast<u32>(inc[1]), i2 = static_cast<u32>(inc[2]);
    u32 ph = root.phase, f0 = f[0], f1 = f[1], f2 = f[2];
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    for (i32 i = 0; i < substep; ++i) {
        const u32 before = ph;
        ph += rise;
        if (forward ? ph < before : ph > before) {
            f0 = shr(umul(ph, r0), 16);
            if (formants > 1) f1 = shr(umul(ph, r1), 16);
            if (formants > 2) f2 = shr(umul(ph, r2), 16);
        } else {
            f0 += i0;
            if (formants > 1) f1 += i1;
            if (formants > 2) f2 += i2;
        }
        const i32 w = tables::pulse[ph >> 22];
        i32 sum = tables::sine[f0 >> 20] * a0;
        if (formants > 1) sum += tables::sine[f1 >> 20] * a1;
        if (formants > 2) sum += tables::sine[f2 >> 20] * a2;
        const i32 x = ((sum >> 15) * w) >> 15;
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
        a0 += e0; a1 += e1; a2 += e2;
    }
    root.phase = ph;
    f[0] = f0; f[1] = f1; f[2] = f2;
    root.gain = g;
    root.low = low;
}
MACH_LOOP void played_swept(Root& root, i32* out, const opxy::i16* t0, const opxy::i16* t1, i32 inc, i32 dinc, i32 mix, i32 dmix) {
    u32 ph = root.phase;
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    i32 in = inc + dinc, mx = mix + dmix;
    for (i32 i = 0; i < substep; ++i) {
        ph += static_cast<u32>(in);
        const i32 fade = mx >> 5;
        const i32 a = lookup(t0, ph), b = lookup(t1, ph);
        const i32 x = a + (((b - a) * fade) >> 15);
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
        in += 2 * dinc; mx += 2 * dmix;
    }
    root.phase = ph;
    root.gain = g;
    root.low = low;
}
MACH_LOOP void bowed_swept(Root& root, i32* out, const opxy::i16* t0, i32 inc, i32 dinc, i32 sharp, i32 dsharp, bool pad) {
    u32 ph = root.phase, ph2 = root.part[0];
    i32 g = root.gain, low = root.low;
    const i32 slope = root.slope, k = root.coef;
    i32 in = inc + dinc, sh = sharp + dsharp;
    for (i32 i = 0; i < substep; ++i) {
        ph += static_cast<u32>(in);
        i32 x = lookup(t0, ph);
        if (pad && sh) {
            ph2 += static_cast<u32>(sh);
            x = (x + lookup(t0, ph2)) >> 1;
        }
        g += slope;
        low += opxy::q31::multiply(opxy::q31::multiply(x << 15, g) - low, k);
        out[i] += low;
        in += 2 * dinc; sh += 2 * dsharp;
    }
    root.phase = ph;
    root.part[0] = ph2;
    root.gain = g;
    root.low = low;
}
// held()'s step: the change over a piece of 2^bits.
inline i32 piece(i32 from, i32 to, u32 bits) { return ((to >> 1) - (from >> 1)) >> bits; }

// One root's sub-step of samples, added to out. Steps, amplitudes and the
// tables' crossfade are held over the sub-step (each phase steps at its mean,
// so it ends where a ramp would); while sweeping, they move sample by sample
// along the ramps.
inline void sound(Voice& v, Root& root, i32* out) {
    const u32 bits = root.sweeping ? substep_bits : 0, pieces = 1u << bits;
    const i32 n = substep >> bits;
    if (v.source == VOICE) {
        const i32 mean = held(root.inc, root.inc_to, 1, 0, true);
        i32 incs[3], amp[3];
        u32 f[3];
        for (u32 j = 0; j < 3; ++j) {
            f[j] = shr(umul(root.phase, root.ratio[j]), 16);
            incs[j] = static_cast<i32>(shr(mul(mean, static_cast<i32>(root.ratio[j])), 16));
        }
        if (bits) {
            i32 step[3];
            for (u32 j = 0; j < 3; ++j) step[j] = piece(root.amp_was[j], root.part_amp[j], bits);
            const bool ahead = static_cast<i32>(mean) >= 0;
            if (root.image) ahead ? sung_swept<1, true>(root, out, mean, incs, root.amp_was, step, f) : sung_swept<1, false>(root, out, mean, incs, root.amp_was, step, f);
            else ahead ? sung_swept<3, true>(root, out, mean, incs, root.amp_was, step, f) : sung_swept<3, false>(root, out, mean, incs, root.amp_was, step, f);
        } else for (u32 c = 1; c <= pieces; ++c, out += n) {
            for (u32 j = 0; j < 3; ++j) amp[j] = held(root.amp_was[j], root.part_amp[j], c, bits, false);
            // The direction is fixed over the sub-step: a loop for each, so the wrap test is one compare.
            const bool ahead = static_cast<i32>(mean) >= 0;
            if (root.image) {
                if (ahead) bits ? sung<1, 1, true>(root, out, mean, incs, amp, f) : sung<1, substep, true>(root, out, mean, incs, amp, f);
                else bits ? sung<1, 1, false>(root, out, mean, incs, amp, f) : sung<1, substep, false>(root, out, mean, incs, amp, f);
            } else {
                if (ahead) bits ? sung<3, 1, true>(root, out, mean, incs, amp, f) : sung<3, substep, true>(root, out, mean, incs, amp, f);
                else bits ? sung<3, 1, false>(root, out, mean, incs, amp, f) : sung<3, substep, false>(root, out, mean, incs, amp, f);
            }
        }
    } else if (v.source == BELL) {
        i32 amp[4];
        if (bits) {
            i32 step[4];
            for (u32 j = 0; j < 4; ++j) step[j] = piece(root.amp_was[j], root.part_amp[j], bits);
            const i32 d0 = piece(root.part_inc[0], root.part_to[0], bits), d2 = piece(root.part_inc[2], root.part_to[2], bits);
            if (root.image) rung_swept<true>(root, out, root.part_inc[0], d0, root.part_inc[2], d2, root.amp_was, step);
            else rung_swept<false>(root, out, root.part_inc[0], d0, root.part_inc[2], d2, root.amp_was, step);
        } else for (u32 c = 1; c <= pieces; ++c, out += n) {
            const i32 step0 = held(root.part_inc[0], root.part_to[0], c, bits, true);
            const i32 step2 = held(root.part_inc[2], root.part_to[2], c, bits, true);
            for (u32 j = 0; j < 4; ++j) amp[j] = held(root.amp_was[j], root.part_amp[j], c, bits, false) >> 1;
            if (root.image) bits ? rung<1, true>(root, out, step0, step2, amp) : rung<substep, true>(root, out, step0, step2, amp);
            else bits ? rung<1, false>(root, out, step0, step2, amp) : rung<substep, false>(root, out, step0, step2, amp);
        }
        for (u32 j = 0; j < 4; ++j) root.part_inc[j] = root.part_to[j];
    } else {
        const opxy::i16* table = wave(v.source);
        const opxy::i16* t0 = table + root.level * (tables::wave_size + 1);
        const opxy::i16* t1 = table + (root.level < 8 ? root.level + 1 : 8) * (tables::wave_size + 1);
        const bool pad = v.source == PAD && !root.image;
        if (bits) {
            const i32 dinc = piece(root.inc, root.inc_to, bits);
            if (pad || root.image) bowed_swept(root, out, t0, root.inc, dinc, pad ? root.part_inc[0] : 0, pad ? piece(root.part_inc[0], root.part_to[0], bits) : 0, pad);
            else played_swept(root, out, t0, t1, root.inc, dinc, root.mix, piece(root.mix, root.mix_to, bits));
        } else for (u32 c = 1; c <= pieces; ++c, out += n) {
            const i32 inc = held(root.inc, root.inc_to, c, bits, true);
            if (pad || root.image) {
                const i32 sharp = pad ? held(root.part_inc[0], root.part_to[0], c, bits, true) : 0;
                bits ? bowed<1>(root, out, t0, inc, sharp) : bowed<substep>(root, out, t0, inc, sharp);
            } else {
                const i32 mix = held(root.mix, root.mix_to, c, bits, true);
                bits ? played<1>(root, out, t0, t1, inc, mix) : played<substep>(root, out, t0, t1, inc, mix);
            }
        }
        if (pad) root.part_inc[0] = root.part_to[0];
    }
    root.inc = root.inc_to;
    root.mix = root.mix_to;
}

// The booms: each image's N-wave, through a shock's rise and the air (its
// corner at the closest pass as w, Q16; -1 open).
inline void booms(Voice& v, Shared& shared, opxy::Output out, i32 air) {
    // Nothing to play this block (before a boom, with both low-passes at rest):
    // the loop below would add zeros and leave every state as it is.
    if (!v.boom_low && !v.boom_lower) {
        const i32 first = static_cast<i32>(v.samples), last = first + static_cast<i32>(opxy::block_frames);
        bool any = false;
        for (u32 m = 0; m < 2; ++m) {
            const Image& im = v.image[m];
            if (im.boom_at >= 0 && im.boom_at < last && im.boom_at + (im.boom_length > 0 ? im.boom_length : 1) > first) any = true;
        }
        if (!any) return;
    }
    const i32 rise = v.boom_rise;
    const i32 k = air < 0 ? max31 : absorb(mul30(air, v.boom_near));
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const i32 n = static_cast<i32>(v.samples + i);
        i32 x = 0;
        for (u32 m = 0; m < 2; ++m) {
            Image& im = v.image[m];
            if (im.boom_at < 0) continue;
            const i32 at = n - im.boom_at;
            if (at == 0) {
                if (!v.boom_held) {
                    v.boom_held = 1;
                    ++shared.booming;
                    v.boom_share = 32768 / static_cast<i32>(shared.booming);
                }
                // The bow shock: up at once, then a straight fall through zero
                // to the tail shock, which returns to rest.
                im.boom_value = (mul30(mul30(im.boom_scale, v.boom_gain), 343597384) >> 15) * v.boom_share;   // Q31: 0.16 of full scale at a full crack
                im.boom_drop = im.boom_length > 0 ? 2 * (im.boom_value / im.boom_length) : 0;
            }
            if (at >= 0 && at < im.boom_length) {
                x += im.boom_value >> 1;
                im.boom_value -= im.boom_drop;
            }
        }
        v.boom_low += opxy::q31::multiply(x - v.boom_low, rise);
        v.boom_lower += opxy::q31::multiply(v.boom_low - v.boom_lower, k);
        out[i] += v.boom_lower;
    }
}

// A DC blocker (a pole at 6 Hz), then twice, within half scale; its state in
// locals, so stores to output need not reload it.
MACH_LOOP void finish(Voice& voice, opxy::Output output) {
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const i32 x = output[i];
        dc_out = x - dc_in + opxy::q31::multiply(dc_out, 2145500000);
        dc_in = x;
        output[i] = knee(dc_out) * 2;
    }
    voice.dc_in = dc_in;
    voice.dc_out = dc_out;
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
    if (voice.fresh) {
        setup(voice, parameters);
        voice.fresh = 0;
    }
    if (voice.done) return;
    // Air, live: the low-pass a distance r gets has its corner at
    // 200 Hz * 2^(5.5 (1 - air)) * (12 m / d)^1/4 * sqrt(d / r); with air at 0 it is open.
    const i32 a = flight::amount(parameters[AIR]);
    const i32 air = flight::air_w(a);
    Block block;
    block.f = pitch[0];
    block.inv = voice.source == VOICE ? quotient(97392, pitched(voice, block.f), 32) : 0;
    if (voice.source == BELL) {
        // Where each partial's phase is at a ring's end (mod 2^32).
        for (u32 k = 0; k < 4; ++k) block.part_end[k] = shr(umul(block.f, static_cast<u32>(bell_ratio[k])), 8) * voice.period_samples;
    }
    block.air = air < 0 ? -1 : static_cast<i32>(shr(mul(air, voice.air_near), 16));
    block.thick = 32768 + a;
    u32 live = 0;
    for (i32 sub = 0; sub < static_cast<i32>(opxy::block_frames) / substep; ++sub) {
        voice.t += substep_time;
        const u32 f = pitch[sub * substep];
        block.f = f;
        for (u32 r = 0; r < 4; ++r) {
            Root& root = voice.root[r];
            if (root.state == DONE) continue;
            ++live;
            if (root.state == WAITING) {
                if (voice.t - substep_time < voice.image[root.image].birth) continue;
                born(voice, root, f);
            }
            solve(voice, root, block);
            sound(voice, root, output + sub * substep);
        }
    }
    if (static_cast<i32>(voice.samples) < voice.boom_end) booms(voice, shared, output, block.air);
    else if (voice.boom_held) {
        voice.boom_held = 0;
        if (shared.booming) --shared.booming;
    }
    finish(voice, output);
    voice.samples += opxy::block_frames;
    if (!live && static_cast<i32>(voice.samples) >= voice.boom_end) voice.done = 1;
}
}
