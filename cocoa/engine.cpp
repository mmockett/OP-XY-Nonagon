#include "core.h"

// Cocoa: the hot chocolate effect. Each note stirs the drink, filling it with
// bubbles, and starts a spoon tapping the mug. Every tap strikes the liquid
// column's modes at the sound speed the drink has then (drink.h), so as the
// bubbles rise out, each tap rings higher than the last, and a ringing tap
// glides up too, until the clear column rings at the key. The cup's wall
// rings with each tap at its own fixed pitches, and bubbles burst at the
// surface as Minnaert pops.
//
// A synth voice on this SDK hears its note start but not its release, and
// gets no velocity: the spoon taps for as long as the OP-XY's amp envelope
// keeps the voice, and its release ends the note (README.md).
//
// The modes are table sines whose increments glide sample by sample, so a tap's
// ring keeps its level as its pitch climbs (a resonator's recursion would
// lose a quarter of it each octave). A tap adds to each mode's amplitude over
// the spoon's contact time; between taps they decay, faster while bubbles
// damp the column.
namespace cocoa {
namespace sound {
using opxy::q31::multiply;
using drink::octave;
const i32 strike = 480000000;       // the fundamental's tap in a clear drink, Q31 (0.22)
const i32 most = 1 << 30;           // a mode's amplitude, at most 0.5
const i32 knee = 429496730;         // 0.2: the soft clip starts here, and stops at 0.325
const i32 silent = 1 << 16;         // under this a mode is not rendered (audit: was 1 << 14)
const u32 ceiling = 0x80000000u;    // an increment at the Nyquist rate
const u32 fade_from = 1717986918u;  // 0.40 of the sample rate: modes fade out from here
const u32 fade_to = 1932735283u;    // to silence at 0.45
const i32 bubble_damping = 49152;   // a mode decays (1 + 0.75 x drop in octaves) times faster
const i32 pop_level = 200000000;    // a pop's ring at pops 100%, Q31
const u32 foam_pops = 2853;         // pops a block from a full head of foam at pops 100% (30 a second), Q16
const i32 slosh_depth = 1092;       // the slosh's pitch swing at stir 100: 20 cents, Q16 octaves
const u32 pop_low = 29215459u;      // pops ring from 300 Hz
const u32 pop_high = 681691819u;    // to 7 kHz
const u32 strum_most = 8;           // a chord's notes tap 0 to 7 blocks (10 ms) apart, each its own
// The column's ring time once clear (T60, Q8 s) by cup, as make_tables.py's.
const u32 ring_time[3] = {230, 461, 179};
// Notes struck together share the blow: each at n^(-1/4) of a lone note's,
// so a chord is louder than one note but its peaks stay in bounds.
const i32 chord_share[8] = {32767, 27554, 24898, 23170, 21913, 20936, 20145, 19483};

// The guard: 1 below 0.40 of the sample rate, 0 above 0.45, Q15.
inline i32 guard(u32 inc) {
    if (inc <= fade_from) return 32768;
    if (inc >= fade_to) return 0;
    return static_cast<i32>(((fade_to - inc) >> 16) * 10u);
}
inline i32 with_guard(i32 ring, i32 g) { return g >= 32768 ? ring : (ring >> 15) * g; }
// An amplitude by a block's decay factor (Q30).
inline i32 decayed(i32 ring, u32 factor) { return multiply(ring, static_cast<i32>(factor)) << 1; }
// The decay over a block for a loss in octaves a block (Q16), Q30.
inline u32 decay(i32 loss) { return drink::exp2_int(30 * octave - loss); }
// The spoon's strike on a mode, Q15: its contact filters the modes above its
// corner (both log2 increments, Q16).
inline i32 spoon(i32 log2_inc, i32 corner) {
    const i32 i = drink::clamp((log2_inc - corner + 6 * octave) >> 13, 0, 96);
    return tables::spoon_gain[i];
}
// What makes up the spoon's loss at the fundamental, Q13 (at most 4): the
// spoon sets the tone, not the level.
inline i32 boost(i32 log2_inc, i32 corner) {
    const i32 i = drink::clamp((log2_inc - corner + 6 * octave) >> 13, 0, 96);
    return tables::spoon_boost[i];
}
// A mode's strike, Q31: the tap, the mode's level, the spoon at the mode and
// the fundamental's make-up.
inline i32 strike_of(i32 hit, i32 level, i32 gain, i32 make_up) {
    return (hit >> 15) * ((((level * gain) >> 15) * make_up) >> 13);
}
inline u32 next(u32& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

// Adds a mode's block to sum (Q27): its amplitude ramps by `slope` until
// sample `split`, then by `after`; its increment by `glide` a sample. Two
// plain loops, so neither tests the split each sample.
__attribute__((noinline)) void sing(i32* sum, Mode& m, u32 inc, i32 glide, i32 amp, i32 slope, i32 after, u32 split) {
    u32 p = m.phase;
    i32 a = amp;
    if (!glide) {   // a steady pitch: the same sums without the glide's add
        for (u32 i = 0; i < split; ++i) {
            sum[i] += ((a >> 16) * tables::sine[p >> 20]) >> 3;
            a += slope;
            p += inc;
        }
        for (u32 i = split; i < opxy::block_frames; ++i) {
            sum[i] += ((a >> 16) * tables::sine[p >> 20]) >> 3;
            a += after;
            p += inc;
        }
        m.phase = p;
        return;
    }
    for (u32 i = 0; i < split; ++i) {
        sum[i] += ((a >> 16) * tables::sine[p >> 20]) >> 3;
        a += slope;
        p += inc;
        inc += static_cast<u32>(glide);
    }
    for (u32 i = split; i < opxy::block_frames; ++i) {
        sum[i] += ((a >> 16) * tables::sine[p >> 20]) >> 3;
        a += after;
        p += inc;
        inc += static_cast<u32>(glide);
    }
    m.phase = p;
}
// Sings a mode's block at 1 / 2^R of the sample rate: every 2^R-th sample of
// the full rate's, with the same phases and amplitudes there. Over 2^R
// samples the phase moves 2^R increments and the glide's 2^R (2^R - 1) / 2,
// and the increment 2^R glides; the amplitude ramps by `slope` until sample
// `split`, then by `after`.
template <u32 R>
__attribute__((noinline)) void sing_at(i32* sum, Mode& m, u32 inc, i32 glide, i32 amp, i32 slope, i32 after, u32 split) {
    const u32 steps = 1u << R;
    u32 p = m.phase;
    i32 a = amp;
    const u32 ramped = (split + steps - 1) >> R;   // samples before the split
    const i32 ramp = slope << R;
    if (!glide) {
        const u32 advance = inc << R;
        for (u32 i = 0; i < ramped; ++i) {
            sum[i] += (static_cast<opxy::i16>(static_cast<u32>(a) >> 16) * tables::sine[p >> 20]) >> 3;
            a += ramp;
            p += advance;
        }
        a = amp + slope * static_cast<i32>(split) + after * static_cast<i32>((ramped << R) - split);
        if (!after) {
            const opxy::i16 held = static_cast<opxy::i16>(static_cast<u32>(a) >> 16);
            for (u32 i = ramped; i < (opxy::block_frames >> R); ++i) {
                sum[i] += (held * tables::sine[p >> 20]) >> 3;
                p += advance;
            }
        } else {
            for (u32 i = ramped; i < (opxy::block_frames >> R); ++i) {
                sum[i] += (static_cast<opxy::i16>(static_cast<u32>(a) >> 16) * tables::sine[p >> 20]) >> 3;
                a += after << R;
                p += advance;
            }
        }
        m.phase = p;
        return;
    }
    const u32 bend = static_cast<u32>(glide) << R, curve = static_cast<u32>(glide) * ((steps * (steps - 1)) >> 1);
    u32 advance = (inc << R) + curve;
    for (u32 i = 0; i < ramped; ++i) {
        sum[i] += (static_cast<opxy::i16>(static_cast<u32>(a) >> 16) * tables::sine[p >> 20]) >> 3;
        a += ramp;
        p += advance;
        advance += bend << R;
    }
    a = amp + slope * static_cast<i32>(split) + after * static_cast<i32>((ramped << R) - split);
    for (u32 i = ramped; i < (opxy::block_frames >> R); ++i) {
        sum[i] += (static_cast<opxy::i16>(static_cast<u32>(a) >> 16) * tables::sine[p >> 20]) >> 3;
        a += after << R;
        p += advance;
        advance += bend << R;
    }
    m.phase = p;
}

// The rates' sums for a block: at the full rate, a half, a quarter and an
// eighth (indexed by r, the rate 1 / 2^r).
struct Sums {
    i32* at[4];
    u32 written;     // bit r: something was sung at rate r (its sum is cleared on the first)
};
inline void clear_sum(Sums& sums, u32 r) {
    if (sums.written & (1u << r)) return;
    sums.written |= 1u << r;
    i32* const sum = sums.at[r];
    for (u32 i = 0; i < (opxy::block_frames >> r); ++i) sum[i] = 0;
}
// Sings a mode at rate r.
inline void sing_rate(Sums& sums, u32 r, Mode& m, u32 inc, i32 glide, i32 amp, i32 slope, i32 after, u32 split) {
    clear_sum(sums, r);
    if (r == 3) sing_at<3>(sums.at[3], m, inc, glide, amp, slope, after, split);
    else if (r == 2) sing_at<2>(sums.at[2], m, inc, glide, amp, slope, after, split);
    else if (r == 1) sing_at<1>(sums.at[1], m, inc, glide, amp, slope, after, split);
    else sing(sums.at[0], m, inc, glide, amp, slope, after, split);
}

// A mode's rate, from the highest increment it sings this block. The
// half-band stages that double each rate pass it flat (within 0.0003 dB) with
// its images 72 dB down up to 1.385 kHz (an eighth), 2.076 kHz (a quarter) and
// 4.153 kHz (a half). A mode moves to a faster rate at 0.8 of those and back
// only below 0.65, so it fades between rates well inside both rates' bands
// and does not hop to and fro.
const u32 rate_up[3] = {323573682u, 161747884u, 107909836u};     // 0.8 of 4.153, 2.076, 1.385 kHz: rates 1, 2, 3
const u32 rate_down[3] = {262903616u, 131420156u, 87676742u};    // 0.65 of them
inline u32 rate_for(u32 now, u32 top) {
    // (Counted, not branched: each comparison is a flag the Blackfin adds.)
    const u32 most = 3u - (top >= rate_up[2]) - (top >= rate_up[1]) - (top >= rate_up[0]);   // the slowest that holds it
    const u32 least = 3u - (top >= rate_down[2]) - (top >= rate_down[1]) - (top >= rate_down[0]);
    const u32 r = now > most ? most : now;
    return r < least ? least : r;
}
// Whether a tap's block is sung at the full rate: its strike ramps in over
// the spoon's contact, as short as 4 samples, whose edge the slower rates
// would round off.
const bool taps_at_full_rate = true;

// The half-band stages that double a rate: from n inputs after 2K - 1 of
// history (low), 2n outputs, each input in turn (K of them late) and the
// value between it and the next, plus add (a faster path, waiting as long),
// clamped when they feed another stage. Equiripple half-bands, Q31 taps of the
// odd phase, doubled: images 72 dB down, flat within 0.0003 dB to 0.126 (K = 4)
// and 0.094 (K = 3) of the output rate. Inputs are within 2^29, so each pair's
// sum fits.
const i32 taps4[4] = {1311826555, -311214238, 88841655, -16251572};
const i32 taps3[3] = {1279783048, -244845092, 39343291};
const i32 stage_limit = 1 << 29;
inline i32 bound(i32 x, i32 low, i32 high) {
    x = x > high ? high : x;
    return x < low ? low : x;
}
template <u32 K> inline i32 between(const i32* x);
template <> inline i32 between<4>(const i32* x) {
    return multiply(x[0] + x[1], taps4[0]) + multiply(x[-1] + x[2], taps4[1]) +
           multiply(x[-2] + x[3], taps4[2]) + multiply(x[-3] + x[4], taps4[3]);
}
template <> inline i32 between<3>(const i32* x) {
    return multiply(x[0] + x[1], taps3[0]) + multiply(x[-1] + x[2], taps3[1]) + multiply(x[-2] + x[3], taps3[2]);
}
// (out is not declared __restrict: the compiler then reads each input afresh
// rather than sliding them through registers, which costs it the hardware loop.)
template <u32 K, bool Clamp>
__attribute__((noinline)) void twice(const i32* low, const i32* add, i32* out, u32 n) {
    const i32* x = low + K - 1;
    for (u32 j = 0; j < n; ++j, ++x, add += 2, out += 2) {
        const i32 even = x[0] + add[0], odd = between<K>(x) + add[1];
        out[0] = Clamp ? bound(even, -stage_limit, stage_limit) : even;
        out[1] = Clamp ? bound(odd, -stage_limit, stage_limit) : odd;
    }
}
template <u32 K, bool Clamp>
__attribute__((noinline)) void twice(const i32* low, i32* out, u32 n) {
    const i32* x = low + K - 1;
    for (u32 j = 0; j < n; ++j, ++x, out += 2) {
        const i32 odd = between<K>(x);
        out[0] = x[0];
        out[1] = Clamp ? bound(odd, -stage_limit, stage_limit) : odd;
    }
}

// A mode's block: decays it, or on a tap strikes it by `hit` over the
// spoon's contact (2^contact samples), and sings it gliding from its
// increment to `to`.
// One path of a mode's block: from its phase now (keeping the phase it ends at
// only when told to, so a mode on two paths moves on once).
inline void path(Sums& sums, u32 r, Mode& m, u32 phase, u32 inc, i32 glide, i32 amp, i32 slope, i32 after, u32 split, bool keep) {
    const u32 was = m.phase;
    m.phase = phase;
    sing_rate(sums, r, m, inc, glide, amp, slope, after, split);
    if (!keep) m.phase = was;
}
// The share of what is left to move that a fade moves this block, 1 / left (Q31; left > 1).
const i32 fade_share[17] = {0, 0, 1073741824, 715827883, 536870912, 429496730, 357913941, 306783378, 268435456,
                           238609294, 214748365, 195225786, 178956971, 165191050, 153391689, 143165577, 134217728};
static_assert(fade_blocks >= 2 && fade_blocks <= 16, "fade_share covers 2 to 16 blocks");

inline void play(Sums& sums, Band& band, Mode& m, u32 to, i32 loss, bool tap, i32 hit, u32 contact, u32 phase) {
    using opxy::block_frames;
    const i32 start = m.out;
    const bool rest = m.ring < silent && start == 0;
    const u32 from = m.inc && !rest ? m.inc : to;
    if (tap) {
        if (rest) m.phase = phase;   // a mode at rest starts where the voice's dice say (its onset ramps from 0)
        m.ring = m.ring + hit > most ? most : m.ring + hit;
    } else {
        m.ring = decayed(m.ring, decay(loss));
    }
    const i32 end = with_guard(m.ring, guard(to));
    m.inc = to;
    m.out = end < silent ? 0 : end;
    if (start == 0 && m.out == 0) {
        band.left = 0;
        return;
    }
    const i32 glide = static_cast<i32>(to - from) >> 6;
    const u32 top = from > to ? from : to;
    const u32 p = m.phase;
    if (tap && taps_at_full_rate) {
        // A tap's strike rings in at the full rate (its contact can be 4
        // samples), and moves to the mode's own rate over the next blocks.
        // The mode's own rate holds its share through the block.
        const i32 strike = (m.out - start) >> contact;
        const u32 split = 1u << contact;
        if (!band.left && !start) band.dst = static_cast<opxy::u8>(rate_for(band.dst, top));
        const i32 own = band.left ? band.kept : start, lent = start - own;
        const bool moving = band.left && band.src;   // between slower rates: the old one hands its share to the full rate
        if (moving && lent) path(sums, band.src, m, p, from, glide, lent, (-lent) >> 6, 0, block_frames, false);
        const i32 handed = moving ? lent >> 6 : 0;
        if (!band.dst) {   // the mode's own rate is the full rate
            path(sums, 0, m, p, from, glide, moving ? own : start, strike + handed, handed, split, true);
            band.left = 0;
            return;
        }
        if (own) path(sums, band.dst, m, p, from, glide, own, 0, 0, block_frames, false);
        path(sums, 0, m, p, from, glide, moving ? 0 : lent, strike + handed, handed, split, true);
        band.kept = own;
        band.src = 0;
        band.left = static_cast<opxy::u8>(fade_blocks);
        return;
    }
    if (!band.left) {
        // A mode moving to another rate fades from the old to the new.
        const u32 r = rate_for(band.dst, top);
        if (r != band.dst) {
            if (start) {
                band.src = band.dst;
                band.kept = 0;
                band.left = fade_blocks;
            }
            band.dst = static_cast<opxy::u8>(r);
        }
    }
    (void)p;
    if (!band.left || band.src == band.dst) {
        band.left = 0;
        sing_rate(sums, band.dst, m, from, glide, start, (m.out - start) >> 6, 0, block_frames);
        return;
    }
    // Fading: the new rate's share grows to the whole over the fade's blocks;
    // the two shares sum to the mode's own ramp.
    const i32 moved = band.left == 1 ? m.out : band.kept + multiply(m.out - band.kept, fade_share[band.left]);
    const i32 lent = start - band.kept, owed = m.out - moved;
    if (lent || owed) path(sums, band.src, m, p, from, glide, lent, (owed - lent) >> 6, 0, block_frames, false);
    path(sums, band.dst, m, p, from, glide, band.kept, (moved - band.kept) >> 6, 0, block_frames, true);
    band.kept = moved;
    --band.left;
}
}

inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    for (u32 k = 0; k < liquid_modes; ++k) voice.liquid[k].phase = voice.liquid[k].inc = 0, voice.liquid[k].ring = voice.liquid[k].out = 0;
    for (u32 k = 0; k < cup_modes; ++k) voice.cup[k].phase = voice.cup[k].inc = 0, voice.cup[k].ring = voice.cup[k].out = 0;
    for (u32 k = 0; k < pops; ++k) voice.pop[k].amp = 0, voice.pop[k].phase = voice.pop[k].inc = 0, voice.pop[k].shift = 6;
    for (u32 i = 0; i < drink::classes; ++i) voice.d[i] = 0, voice.s[i] = drink::clear;
    voice.drop = 0;
    voice.dc_in = voice.dc_out = 0;
    voice.tap_phase = voice.taps = 0;
    voice.seed = 0x9e3779b9u ^ (owner * 0x85ebca6bu);
    voice.seen = 0;
    voice.fresh = 0;
    voice.quiet = 1;
    voice.alive = 0;
    voice.wait = 0;
    voice.started_at = 0;
    voice.share = 32767;
    voice.foam = voice.slosh = 0;
    voice.slosh_phase = 0;
    voice.rated = 0;
    voice.filled = 0;
    for (u32 k = 0; k < liquid_modes + cup_modes; ++k) voice.band[k].dst = 3, voice.band[k].src = 3, voice.band[k].left = 0, voice.band[k].spare = 0, voice.band[k].kept = 0;
    voice.live = 0;
    voice.spare[0] = voice.spare[1] = 0;
    for (u32 i = 0; i < kHistory4 + kBlock / 8; ++i) voice.eighth[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 4; ++i) voice.quarter[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 2; ++i) voice.half[i] = 0;
}

// Each note stirs on its first block, where the controls are known. A note
// that finds every other note ended finds the drink settled: a pause clears
// it. Notes that start together (a chord) tap a block apart, as a strum.
inline void start(Voice& voice, Shared& shared, opxy::u32 owner) {
    if (!shared.alive) shared.poured = 0;
    if (!voice.alive) ++shared.alive;
    voice.alive = 1;
    const bool together = shared.started_at == shared.tick && shared.alive > 1;
    shared.strum = together ? shared.strum + 1 : 0;
    shared.started_at = shared.tick;
    voice.started_at = shared.tick;
    voice.wait = shared.strum % sound::strum_most;
    voice.fresh = 1;
    shared.leader = owner;
    voice.quiet = 0;
}

inline void destroy(Voice& voice, Shared& shared, opxy::u32) {
    if (voice.alive && shared.alive) --shared.alive;
    voice.alive = 0;
}

// The output stage, a function of its own so its loop is a hardware loop.
// The clamp's bound, hi = 2^27 - 1, and the knee's reach, 2^29, are passed in
// so the compiler keeps the clamps as minimums and maximums rather than
// branches (which a Blackfin mispredicts at 8 cycles a sample). The knee
// itself is branchless: over is how far past the knee, at most 2^29, where
// its curve meets the ceiling.
inline i32 finish_one(i32 x, i32& dc_in, i32& dc_out, i32 hi, i32 reach) {
    using namespace sound;
    dc_out = x - dc_in + dc_out - (dc_out >> 10);
    dc_in = x;
    const i32 v = bound(dc_out, -hi, hi);
    const i32 sign = v >> 31;               // 0 or -1: |v| and the sign restored without a branch
    const i32 o = ((v ^ sign) - sign) << 4;
    const i32 over = bound(o - knee, 0, reach);
    const i32 top = knee + reach;
    const i32 r = (o < top ? o : top) - (multiply(over, over) << 1);
    return (r ^ sign) - sign;
}
__attribute__((noinline)) void finish(Voice& voice, const i32* sum, opxy::Output output, i32 hi, i32 reach, u32 count) {
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    for (unsigned i = 0; i < count; ++i) output[i] = finish_one(sum[i], dc_in, dc_out, hi, reach);
    for (unsigned i = count; i < opxy::block_frames; ++i) output[i] = 0;
    voice.dc_in = dc_in;
    voice.dc_out = dc_out;
}

// Back to the full rate. The eighth rate's sum, after its history, doubles to
// the quarter rate and joins the quarter rate's (waiting 8 of its samples, as
// late as the stage is); that doubles and joins the half rate's (waiting 22),
// and that the full rate's (waiting 50). A sum not written for a block is all
// zeros, and a stage whose input and history are all zeros is skipped. Returns
// the full-rate sum, in tone.
inline void back_to_full_rate(Voice& voice, sound::Sums& sums, i32* full, i32* half, i32* quarter, opxy::Output output,
                              u32 count = opxy::block_frames) {
    using namespace sound;
    using opxy::block_frames;
    const u32 wrote = sums.written, last = voice.live;
    u32 live = wrote & 15u;
    i32* const waits[3] = {full, half, quarter};
    i32* const tails[3] = {voice.full_wait, voice.half_wait, voice.quarter_wait};
    const u32 lengths[3] = {kFullWait, kHalfWait, kQuarterWait};
    for (u32 r = 0; r < 3; ++r) {
        const u32 bit = 1u << r, n = block_frames >> r, w = lengths[r];
        i32* const wait = waits[r];
        if (wrote & bit) {
            if (last & bit) for (u32 i = 0; i < w; ++i) wait[i] = tails[r][i];
            else for (u32 i = 0; i < w; ++i) wait[i] = 0;
            for (u32 i = 0; i < w; ++i) tails[r][i] = wait[n + i];
        } else if (last & bit) {   // a tail with nothing after it
            for (u32 i = 0; i < w; ++i) wait[i] = tails[r][i];
            for (u32 i = w; i < w + n; ++i) wait[i] = 0;
        }
    }
    const bool to_quarter = (wrote | last) & 4u, to_half = (wrote | last) & 2u, to_full = (wrote | last) & 1u;
    // Eighth to quarter.
    i32* const stage4 = voice.quarter + kHistory3;
    const bool eighth_on = (wrote | last) & 8u;
    if (eighth_on) {
        i32* const eighth = sums.at[3];
        if (!(wrote & 8u)) for (u32 i = 0; i < block_frames / 8; ++i) eighth[i] = 0;
        for (u32 i = 0; i < block_frames / 8; ++i) eighth[i] = bound(eighth[i], -stage_limit, stage_limit);
        if (to_quarter) twice<4, true>(voice.eighth, quarter, stage4, block_frames / 8);
        else twice<4, true>(voice.eighth, stage4, block_frames / 8);
        for (u32 i = 0; i < kHistory4; ++i) voice.eighth[i] = voice.eighth[block_frames / 8 + i];
    } else if (to_quarter) {
        for (u32 i = 0; i < block_frames / 4; ++i) stage4[i] = bound(quarter[i], -stage_limit, stage_limit);
    }
    const bool quarter_in = eighth_on || to_quarter;
    if (quarter_in) live |= 16u;
    // Quarter to half.
    i32* const stage3 = voice.half + kHistory3;
    const bool quarter_on = quarter_in || (last & 16u);
    if (quarter_on) {
        if (!quarter_in) for (u32 i = 0; i < block_frames / 4; ++i) stage4[i] = 0;
        if (to_half) twice<3, true>(voice.quarter, half, stage3, block_frames / 4);
        else twice<3, true>(voice.quarter, stage3, block_frames / 4);
        for (u32 i = 0; i < kHistory3; ++i) voice.quarter[i] = voice.quarter[block_frames / 4 + i];
    } else if (to_half) {
        for (u32 i = 0; i < block_frames / 2; ++i) stage3[i] = bound(half[i], -stage_limit, stage_limit);
    }
    const bool half_in = quarter_on || to_half;
    if (half_in) live |= 32u;
    voice.live = static_cast<opxy::u8>(live);
    // Half to full.
    const bool half_on = half_in || (last & 32u);
    // (The output stage's clamp, 2^27 - 1, and its knee's reach, 2^29, are
    // passed in so the compiler keeps them branchless.)
    const i32 hi = (1 << 27) - 1, reach = 1 << 29;
    if (half_on) {
        if (!half_in) for (u32 i = 0; i < block_frames / 2; ++i) stage3[i] = 0;
        i32 tone[block_frames];
        if (to_full) twice<3, false>(voice.half, full, tone, block_frames / 2);
        else twice<3, false>(voice.half, tone, block_frames / 2);
        for (u32 i = 0; i < kHistory3; ++i) voice.half[i] = voice.half[block_frames / 2 + i];
        finish(voice, tone, output, hi, reach, count);
    } else {
        if (!to_full) for (u32 i = 0; i < block_frames; ++i) full[i] = 0;
        finish(voice, full, output, hi, reach, count);
    }
}

// A voice that falls silent (it has rung out, or a strum holds it back)
// stops at once, as it always has: the slower rates' last samples, still on
// their way up, are played out (kFullWait of them) and the rest let go.
inline void flush(Voice& voice, opxy::Output output) {
    using namespace sound;
    using opxy::block_frames;
    i32 full[kFullWait + block_frames], half[kHalfWait + block_frames / 2], quarter[kQuarterWait + block_frames / 4];
    Sums sums;
    sums.at[0] = full + kFullWait;
    sums.at[1] = half + kHalfWait;
    sums.at[2] = quarter + kQuarterWait;
    sums.at[3] = voice.eighth + kHistory4;
    sums.written = 0;
    back_to_full_rate(voice, sums, full, half, quarter, output, kFullWait);   // the last block's samples, then zeros
    for (u32 i = 0; i < kHistory4 + kBlock / 8; ++i) voice.eighth[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 4; ++i) voice.quarter[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 2; ++i) voice.half[i] = 0;
    voice.live = 0;
}

inline void render(Voice& voice, Shared& shared, opxy::u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace sound;
    using opxy::block_frames;

    // The shared clock: the first voice to render in a block moves it on.
    if (voice.seen == shared.tick) ++shared.tick;
    voice.seen = shared.tick;
    if (voice.quiet || voice.wait) {
        if (voice.wait) --voice.wait;
        if (voice.live) {
            flush(voice, output);
        } else {
            for (unsigned i = 0; i < block_frames; ++i) output[i] = 0;
        }
        return;
    }

    // The controls.
    const i32 fill = parameters[FILL];
    const u32 cup = drink::choice(parameters[CUP], 3);
    const u32 step = drink::tap_step(parameters[TAPS]);
    const i32 hard = static_cast<i32>(drink::q16(parameters[SPOON]));
    const i32 corner = tables::log2_soft_corner + static_cast<i32>((static_cast<u32>(tables::corner_span) >> 4) * static_cast<u32>(hard) >> 12);
    // The contact, 4 ms to 75 us, as a power of two of samples: 64 to 4.
    const u32 contact = static_cast<u32>(drink::clamp((tables::log2_soft_samples - (static_cast<i32>((static_cast<u32>(tables::corner_span) >> 4) * static_cast<u32>(hard) >> 12)) + 32768) >> 16, 2, 6));
    i32* const rate = voice.rate;
    if (!voice.rated || fill != voice.rated_fill || parameters[FIZZ] != voice.rated_fizz) {
        drink::rates(fill, parameters[FIZZ], rate);
        voice.rated = 1;
        voice.rated_fill = fill;
        voice.rated_fizz = parameters[FIZZ];
    }

    // A new note stirs the drink as the last stir left it, cleared since,
    // and the spoon taps at once.
    bool tap = false;
    if (voice.fresh) {
        i32 d[drink::classes], s[drink::classes];
        for (u32 i = 0; i < drink::classes; ++i) {
            d[i] = shared.poured ? shared.d[i] : 0;
            s[i] = shared.poured ? shared.s[i] : drink::clear;
        }
        drink::advance(s, rate, shared.tick - shared.stirred_at);
        // A settled drink (the first note after a pause) is stirred afresh.
        const i32 amount = shared.poured ? drink::restir(parameters[STIR]) : 32767;
        drink::stir(d, s, drink::powder_void(parameters[POWDER]), amount);
        for (u32 i = 0; i < drink::classes; ++i) {
            voice.d[i] = shared.d[i] = d[i];
            voice.s[i] = shared.s[i] = s[i];
        }
        shared.stirred_at = shared.tick;
        shared.poured = 1;
        ++shared.stirs;
        voice.seed = (shared.stirs * 0x9e3779b9u) ^ (owner * 0x85ebca6bu) ^ 0x2545f491u;
        voice.tap_phase = 0;
        voice.taps = 0;
        voice.fresh = 0;
        const u32 together = shared.started_at == voice.started_at ? shared.strum : 0;
        voice.share = chord_share[together < 7 ? together : 7];
        // The stir raises a head of foam, more for more powder, and sets the
        // drink sloshing as hard as it is stirred.
        const i32 foam = 16384 + static_cast<i32>(drink::q16(parameters[POWDER]) >> 2);
        voice.foam = voice.foam > foam ? voice.foam : foam;
        voice.slosh = static_cast<i32>(drink::q16(parameters[STIR]) >> 1);
        voice.slosh_phase = 0;
        tap = true;
    } else if (step) {
        const u32 before = voice.tap_phase;
        voice.tap_phase += step;
        tap = voice.tap_phase < before;
    }
    if (tap) ++voice.taps;

    // The bubbles rise; the column's pitch at the block's end. The drink
    // sloshes at its first sloshing mode, swinging the pitch as it dies away.
    drink::advance(voice.s, rate, 1);
    const i32 void_now = drink::effective(voice.d, voice.s);
    const i32 drop = drink::wood_drop(void_now);
    voice.drop = drop;
    i32 wobble = 0;
    if (voice.slosh) {
        voice.slosh_phase += static_cast<u32>(drink::along(tables::slosh_inc, fill));
        wobble = (((voice.slosh * slosh_depth) >> 15) * tables::sine[voice.slosh_phase >> 20]) >> 15;
        voice.slosh -= (voice.slosh >> 9) + 1;   // dies away in about 0.7 s
        if (voice.slosh < 64) voice.slosh = 0;
    }
    const i32 shift = wobble - drop;   // the liquid's pitch against the clear key, Q16 octaves

    // The tap's strength, a little uneven, as a hand's is.
    const i32 lean = static_cast<i32>((voice.taps * 2654435761u) >> 24) - 128;   // -128..127
    // A bubbly column's impedance (rho c) is low, so it takes up less of the
    // spoon's blow: the tap's level falls a quarter octave each octave of drop.
    // Fast taps on a long-ringing column pile up: each strikes softer by
    // 1 / sqrt(1 + rate T60 / 2), so tapping holds about a single tap's level.
    const u32 pile = 256u + drink::tap_tenths(parameters[TAPS]) * ring_time[cup] / 20u;   // Q8
    const i32 soften = (drink::log2_q16(pile) - 8 * octave) / 2;
    const i32 hit = static_cast<i32>(drink::scale(static_cast<u32>(((strike >> 15) * voice.share >> 15) * (32768 + lean * 20)),
                                                  -drop / 4 - soften, ceiling));

    // The sums at each rate. The faster rates' sums wait for the eighth's:
    // each starts with the last block's tail still to come.
    i32 full[kFullWait + block_frames], half[kHalfWait + block_frames / 2], quarter[kQuarterWait + block_frames / 4];
    Sums sums;
    sums.at[0] = full + kFullWait;
    sums.at[1] = half + kHalfWait;
    sums.at[2] = quarter + kQuarterWait;
    sums.at[3] = voice.eighth + kHistory4;
    sums.written = 0;

    // The liquid's modes: tuned to the key with no bubbles, falling by the drop.
    const u32 key = pitch[0] ? pitch[0] : 1;
    const i32 log2_key = drink::log2_q16(key);
    const i32 damping = 65536 + ((drop >> 2) * (bubble_damping >> 6) >> 8);   // Q16
    const i32 depth = drink::along(tables::liquid_depth, fill);              // shallow drinks ring shorter
    // The spoon strikes the drink's modes up to a corner that follows the key,
    // from half an octave under it (soft) to three and a half over (hard), so
    // it brightens alike at every pitch; the wall's clink takes its absolute
    // contact (corner, above).
    const i32 tone = log2_key - octave / 2 + 4 * hard;
    const i32 make_up = boost(log2_key + shift, tone);
    // and a hard spoon's sharper blow carries more into the upper modes: 0.3
    // (wood) to 2 (silver) times their level, about 1.1 at the default.
    const u32 hard2 = (static_cast<u32>(hard) * static_cast<u32>(hard)) >> 16;
    const i32 upper = 9830 + static_cast<i32>((((static_cast<u32>(hard) + hard2) >> 1) * 55706u) >> 16);   // Q15
    bool ringing = false;
    // What the fill and the cup alone set, worked out when they change: each
    // liquid mode's ratio and clear loss, and the wall's pitches and losses.
    if (!voice.filled || fill != voice.filled_fill || cup != voice.filled_cup) {
        for (u32 k = 0; k < liquid_modes; ++k) {
            voice.ratio[k] = drink::along(tables::liquid_ratio[k], fill);
            voice.liquid_loss[k] = drink::scale(static_cast<u32>(tables::liquid_decay[cup]), voice.ratio[k] / 2 + depth, ceiling);
        }
        const i32 lowered = drink::along(tables::cup_lowering, fill);
        const i32 damped = drink::along(tables::cup_damping, fill);
        for (u32 k = 0; k < cup_modes; ++k) {
            voice.cup_log2[k] = tables::cup_pitch[cup][k] + lowered;
            voice.cup_inc[k] = drink::exp2_int(voice.cup_log2[k]);
            voice.cup_loss[k] = static_cast<i32>(drink::scale(static_cast<u32>(tables::cup_decay[cup][k]), damped, ceiling));
        }
        voice.filled = 1;
        voice.filled_fill = fill;
        voice.filled_cup = cup;
    }
    for (u32 k = 0; k < liquid_modes; ++k) {
        Mode& m = voice.liquid[k];
        const i32 ratio = voice.ratio[k];
        const u32 to = drink::scale(key, ratio + shift, ceiling);
        // A mode at rest, untapped, has nothing to do but follow the drink's
        // pitch: its last trace (under silent, -102 dB) is let go rather than
        // decayed on.
        if (!tap && m.out == 0 && m.ring < silent) {
            m.ring = 0;
            m.inc = to;
            voice.band[k].left = 0;
            continue;
        }
        const i32 loss = static_cast<i32>((voice.liquid_loss[k] * static_cast<u32>(damping)) >> 16);
        i32 struck = 0;
        const i32 level = k ? (tables::liquid_level[k] * upper) >> 15 : tables::liquid_level[0];
        if (tap) struck = strike_of(hit, level, spoon(log2_key + ratio + shift, tone), make_up);
        play(sums, voice.band[k], m, to, loss, tap, struck, contact, tap ? next(voice.seed) : 0);
        ringing = ringing || m.out;
    }
    // The cup's wall, lowered and damped by the drink in it. There is one cup,
    // whatever the chord: only the newest note's taps ring it.
    // A hard spoon rings the wall harder: its clink 0.25 to 1.75 times.
    const i32 clink = 8192 + static_cast<i32>(((static_cast<u32>(hard) * static_cast<u32>(hard) >> 16) * 49152u) >> 16);
    for (u32 k = 0; k < cup_modes; ++k) {
        Mode& m = voice.cup[k];
        if (!tap && m.out == 0 && m.ring < silent) {
            m.ring = 0;
            m.inc = voice.cup_inc[k];
            voice.band[liquid_modes + k].left = 0;
            continue;
        }
        const i32 log2_inc = voice.cup_log2[k];
        const u32 to = voice.cup_inc[k];
        const i32 loss = voice.cup_loss[k];
        i32 struck = 0;
        if (tap && shared.leader == owner) {
            const i32 level = (tables::cup_level[cup][k] * clink) >> 15;
            struck = strike_of(hit, level < 32767 ? level : 32767, spoon(log2_inc, corner), make_up);
        }
        play(sums, voice.band[liquid_modes + k], m, to, loss, tap, struck, contact, tap ? next(voice.seed) : 0);
        ringing = ringing || m.out;
    }

    // Pops: bubbles burst at the surface, from the foam a stir raised (dying
    // away in about 0.7 s) and as fast as the void leaves the column, each a
    // Minnaert ring at its foam bubble's size. Pops sets how often and how loud.
    const i32 popping = static_cast<i32>(drink::q16(parameters[POPS]) >> 1);
    const bool bubbly = void_now > 0;
    voice.foam -= (voice.foam >> 9) + 1;
    if (voice.foam < 64) voice.foam = 0;
    if (popping && (bubbly || voice.foam)) {
        u32 flux[drink::classes], total = 0;
        for (u32 i = 0; i < drink::classes; ++i) {
            flux[i] = voice.s[i] < drink::clear ? static_cast<u32>(voice.d[i] >> 12) * static_cast<u32>(rate[i] >> 12) : 0;
            total += flux[i];
        }
        // The foam is the mug's, like its wall: only the newest note's pops from it.
        const u32 frothing = shared.leader == owner ? (static_cast<u32>(voice.foam) * foam_pops) >> 15 : 0;
        const u32 base = (((total >> 8) * 750u) >> 9) + frothing;
        const u32 chance = (base * static_cast<u32>(popping)) >> 15;   // Q16 a block
        const u32 roll = next(voice.seed);
        if ((roll & 0xffff) < chance) {
            Pop* slot = 0;
            for (u32 k = 0; k < pops; ++k)
                if (voice.pop[k].amp < silent) { slot = &voice.pop[k]; break; }
            if (slot) {
                const u32 r2 = next(voice.seed);
                // Which class it came from: in proportion to their flux, or any, from the foam.
                u32 i = 0;
                if (total) {
                    u32 pick = total < 65536 ? ((r2 & 0xffff) * total) >> 16 : (r2 >> 16) * (total >> 16);
                    while (i + 1 < drink::classes && pick >= flux[i]) pick -= flux[i], ++i;
                } else {
                    i = r2 >> 29;
                }
                const i32 jitter = static_cast<i32>((r2 >> 4) & 0x7fff) - 16384;    // +-0.25 octave
                const i32 log2_hz = tables::log2_minnaert - (drink::log2_radius(parameters[FIZZ]) + tables::class_radius[i]) + jitter;
                u32 inc = drink::exp2_int(log2_hz + tables::log2_hertz);
                inc = inc < pop_low ? pop_low : inc > pop_high ? pop_high : inc;
                slot->inc = inc;
                slot->phase = 0;
                slot->shift = inc < 194783407u ? 7 : inc < 486958518u ? 6 : 5;   // under 2 kHz, 5 kHz, above
                const i32 loud = 13107 + ((popping * 19661) >> 15);   // 0.4 to 1 of the level, Q15
                slot->amp = multiply((pop_level >> 15) * loud, static_cast<i32>(0x40000000u + ((r2 & 0xffff) << 14)));   // 0.5 to 1
            }
        }
    }
    // The pops ring out at the full rate: each starts at once, at full strength.
    i32* const sum = sums.at[0];
    for (u32 k = 0; k < pops; ++k) {
        Pop& p = voice.pop[k];
        if (p.amp < silent) continue;
        clear_sum(sums, 0);
        u32 ph = p.phase, inc = p.inc;
        i32 a = p.amp;
        const u32 shift = p.shift, chirp = inc >> 13;
        for (unsigned i = 0; i < block_frames; ++i) {
            sum[i] += ((a >> 16) * tables::sine[ph >> 20]) >> 3;
            a -= a >> shift;
            ph += inc;
            inc += chirp;
        }
        p.phase = ph;
        p.inc = inc;
        p.amp = a < silent ? 0 : a;
        ringing = true;
    }

    // A DC blocker at 7 Hz (a decaying low ring has a little), then sixteen
    // times the eighth, with a soft knee from 0.2 to a ceiling of 0.325: it
    // rounds off only a tap's first peaks, where every mode starts at once,
    // so the ring can sit louder (README.md, Level).
    back_to_full_rate(voice, sums, full, half, quarter, output);
    if (!step && !ringing && (!popping || (!bubbly && !voice.foam))) voice.quiet = 1;   // a single tap, rung out
}
}
