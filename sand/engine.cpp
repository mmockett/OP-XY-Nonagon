#include "core.h"
#include "sine.h"
#include <opxy/q31.hpp>

// Sand: a bowed plate covered in sand. Each note bows a square plate in one of
// its Chladni figures (Figure): the bowed mode sounds the note, and six of
// the plate's modes above it ring faintly with it. Forty-eight grains of sand
// lie on the plate. A few hundred times a second each grain feels the plate's
// acceleration where it sits; where that beats gravity the grain is thrown,
// and lands a few milliseconds later with a click. The rattle is the sum of
// those clicks, so it is as dense as the sand still on moving ground. Thrown
// sand drifts to the quiet nodal lines, where it stops: the sizzle dies away
// and the figure forms, leaving the bowed tone. A new note pours fresh sand;
// a new figure finds the sand on moving ground again, and it crackles and
// re-forms. The sand also weighs on the plate where it gathers, flattening
// and damping the modes it sits on, so the tone clears and rises a little as
// the sand finds the nodes, and settles where the sand ends up.
//
// The modes are damped resonators solved in phasor form, as in Hexagon's
// Hollow: each mode's complex amplitude moves once a block exactly as a
// resonator's envelope does when driven near its own frequency, and a sine
// table sounds it, ramped across the block. The sand needs only those
// amplitudes: a mode's peak acceleration is its amplitude times its frequency.
// Landing clicks are impulses at random points of the block through one
// two-pole band-pass (Grain: from a bright tick of dust to a pebble's knock),
// and each landing also knocks the plate's modes where it lands.
//
// In stereo the plate is level and mono until Tilt tips it, and Tilt opens it
// as it tips: at Tilt 0 left and right are the same, word for word. Left plus
// right is always twice the mono voice. Opened, a landing is heard on the side its
// grain lies. The six upper modes are shared out between the two sides, the
// voice saying which goes where so a chord's notes spread. The bowed mode and its
// harmonics stay in the middle, while the twin, the same mode's other standing
// wave, lies across the plate: as the two beat, the tone turns slowly from side
// to side.
namespace sand {
namespace {
using opxy::q31::multiply;
const u32 kLowestIncrement = 779000u;  // about 8 Hz
const u32 kTopIncrement = 0x5c000000u;  // about 15.8 kHz: modes above it fall silent
const i32 kTopLog2 = 1990000;          // log2 of kTopIncrement, Q16 (30.36 octaves)
const i32 kHalf = 1 << 30;
const i32 kAmplitudeLimit = 4095 << 16;
const u32 kLooks = 12;                 // grains looked at a block: each one every four blocks

inline i32 clamp_amplitude(i32 x) {
    return x > kAmplitudeLimit ? kAmplitudeLimit : x < -kAmplitudeLimit ? -kAmplitudeLimit : x;
}

// A loss per block, 2^log2_loss of the amplitude (Q16 log), as the share kept (Q31).
inline i32 keep(i32 log2_loss) {
    const u32 loss = exp2_q16(log2_loss);
    const u32 e = loss > 0x7fffffu ? 0x7fffffu : loss;
    const i32 k = 15 * 65536 - static_cast<i32>(times(e, 94548u));  // log2 e^-x, + 31
    const u32 d = k < -16 * 65536 ? 0 : exp2_q16(k);
    return d > 0x7fffffffu ? 0x7fffffff : static_cast<i32>(d);
}

// The per-block push (amplitude units) that holds a mode losing 2^log2_loss a
// block at a stationary amplitude of target under uniform noise:
// target * sqrt(2x) / 0.577.
inline i32 stir(i32 log2_loss, i32 target) {
    i32 root = (log2_loss + 65536) >> 1;
    if (root > 0) root = 0;
    return static_cast<i32>(times(static_cast<u32>(target), exp2_q16(root + 50352)));
}

// A rough magnitude of (a, b), amplitude units: max + min / 2.
inline i32 swing(const Mode& m) {
    const i32 a = magnitude(m.a) >> 16, b = magnitude(m.b) >> 16;
    return a > b ? a + (b >> 1) : b + (a >> 1);
}

// The modes are sounded in eight steps a block: each amplitude holds for
// eight samples at its ramp's value there, so the ramp costs nothing a sample.
const u32 kStep = 8;

// Sounds a mode into acc over a block at 1 / 2^R of the sample rate, ramping
// from (a, b) by (da, db) a full-rate sample, and returns its phase: one table
// pointer, two 16 x 16 products. increment is the full rate's. A slower rate
// takes every 2^R-th sample of the full rate's phases, and ramps its amplitude
// sample by sample (at the middle of each, as the full rate's steps hold it on
// average): its sum, back at the full rate, is the full rate's without the
// steps' small edges.
template <u32 R>
__attribute__((noinline)) u32 sound(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    increment <<= R;
    a += da >> 1;
    b += db >> 1;
    const i32 ra = da << R, rb = db << R;
    for (i32* out = acc; out != acc + (opxy::block_frames >> R); ++out) {
        const opxy::i16* const at = kSine + (phase >> 22);
        *out += at[0] * static_cast<opxy::i16>(static_cast<u32>(a) >> 16) + at[256] * static_cast<opxy::i16>(static_cast<u32>(b) >> 16);
        phase += increment;
        a += ra;
        b += rb;
    }
    return phase;
}
template <>
__attribute__((noinline)) u32 sound<0>(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    a += da * (kStep / 2);
    b += db * (kStep / 2);
    for (i32* step = acc; step != acc + opxy::block_frames; step += kStep) {
        const opxy::i16 ah = static_cast<opxy::i16>(static_cast<u32>(a) >> 16), bh = static_cast<opxy::i16>(static_cast<u32>(b) >> 16);
        for (i32* out = step; out != step + kStep; ++out) {
            const opxy::i16* const at = kSine + (phase >> 22);
            *out += at[0] * ah + at[256] * bh;
            phase += increment;
        }
        a += da * kStep;
        b += db * kStep;
    }
    return phase;
}

// Sounds the bowed mode: its sine, ramped like any mode's, and the bow's
// harmonics, one waveform at the same phase, ramped from h by dh a sample.
template <u32 R>
__attribute__((noinline)) u32 sound_bowed(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db, i32 h, i32 dh) {
    increment <<= R;
    a += da >> 1;
    b += db >> 1;
    h += dh >> 1;
    const i32 ra = da << R, rb = db << R, rh = dh << R;
    for (i32* out = acc; out != acc + (opxy::block_frames >> R); ++out) {
        const opxy::i16* const at = kSine + (phase >> 22);
        *out += at[0] * static_cast<opxy::i16>(static_cast<u32>(a) >> 16) + at[256] * static_cast<opxy::i16>(static_cast<u32>(b) >> 16) +
                kHarmonicWave[phase >> 22] * static_cast<opxy::i16>(static_cast<u32>(h) >> 16);
        phase += increment;
        a += ra;
        b += rb;
        h += rh;
    }
    return phase;
}
template <>
__attribute__((noinline)) u32 sound_bowed<0>(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db, i32 h, i32 dh) {
    a += da * (kStep / 2);
    b += db * (kStep / 2);
    h += dh * (kStep / 2);
    for (i32* step = acc; step != acc + opxy::block_frames; step += kStep) {
        const opxy::i16 ah = static_cast<opxy::i16>(static_cast<u32>(a) >> 16), bh = static_cast<opxy::i16>(static_cast<u32>(b) >> 16), hh = static_cast<opxy::i16>(static_cast<u32>(h) >> 16);
        for (i32* out = step; out != step + kStep; ++out) {
            const opxy::i16* const at = kSine + (phase >> 22);
            *out += at[0] * ah + at[256] * bh + kHarmonicWave[phase >> 22] * hh;
            phase += increment;
        }
        a += da * kStep;
        b += db * kStep;
        h += dh * kStep;
    }
    return phase;
}

// The rates' sums for a block: at the full rate, a half, a quarter and an
// eighth (indexed by r, the rate 1 / 2^r).
struct Sums {
    i32* at[4];
    u32 written;     // bit r: something was sounded at rate r (its sum is cleared on the first)
};
inline void clear_sum(Sums& sums, u32 r) {
    if (sums.written & (1u << r)) return;
    sums.written |= 1u << r;
    i32* const sum = sums.at[r];
    for (u32 i = 0; i < (opxy::block_frames >> r); ++i) sum[i] = 0;
}
inline u32 sound_at(Sums& sums, u32 r, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    clear_sum(sums, r);
    if (r == 3) return sound<3>(sums.at[3], phase, increment, a, b, da, db);
    if (r == 2) return sound<2>(sums.at[2], phase, increment, a, b, da, db);
    if (r == 1) return sound<1>(sums.at[1], phase, increment, a, b, da, db);
    return sound<0>(sums.at[0], phase, increment, a, b, da, db);
}
inline u32 sound_bowed_at(Sums& sums, u32 r, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db, i32 h, i32 dh) {
    clear_sum(sums, r);
    if (r == 3) return sound_bowed<3>(sums.at[3], phase, increment, a, b, da, db, h, dh);
    if (r == 2) return sound_bowed<2>(sums.at[2], phase, increment, a, b, da, db, h, dh);
    if (r == 1) return sound_bowed<1>(sums.at[1], phase, increment, a, b, da, db, h, dh);
    return sound_bowed<0>(sums.at[0], phase, increment, a, b, da, db, h, dh);
}

// A mode's rate, from the highest frequency it sounds (a full-rate
// increment): an eighth below 1.385 kHz, a quarter below 2.076 kHz and a half
// below 4.153 kHz, where the half-band stages that double each rate pass it
// flat (within 0.0003 dB) and its images lie 72 dB down. It moves to a slower
// rate again only 8% below those, so a mode at a boundary does not hop to and fro.
const u32 kUp[3] = {404467102u, 202184855u, 134887295u};      // 4.153, 2.076, 1.385 kHz: rates 1, 2, 3
const u32 kDown[3] = {372109734u, 186010067u, 124096311u};    // 8% lower
inline u32 rate_for(u32 now, u32 top) {
    // (Counted, not branched: each comparison is a flag the Blackfin adds.)
    const u32 most = 3u - (top >= kUp[2]) - (top >= kUp[1]) - (top >= kUp[0]);   // the slowest that holds it
    const u32 least = 3u - (top >= kDown[2]) - (top >= kDown[1]) - (top >= kDown[0]);
    const u32 r = now > most ? most : now;
    return r < least ? least : r;
}

// The half-band stages that double a rate: from n inputs after 2K - 1 of
// history (low), 2n outputs, each input in turn (K of them late) and the
// value between it and the next, plus add (a faster path, waiting as long),
// clamped when they feed another stage. Equiripple half-bands, Q31 taps of the
// odd phase, doubled: images 72 dB down, flat within 0.0003 dB to 0.126 (K = 4)
// and 0.094 (K = 3) of the output rate. Inputs are within 2^28, so each pair's
// sum fits.
const i32 kTaps4[4] = {1311826555, -311214238, 88841655, -16251572};
const i32 kTaps3[3] = {1279783048, -244845092, 39343291};
const i32 kStageLimit = 1 << 28;
template <u32 K> inline i32 between(const i32* x);
template <> inline i32 between<4>(const i32* x) {
    return multiply(x[0] + x[1], kTaps4[0]) + multiply(x[-1] + x[2], kTaps4[1]) +
           multiply(x[-2] + x[3], kTaps4[2]) + multiply(x[-3] + x[4], kTaps4[3]);
}
template <> inline i32 between<3>(const i32* x) {
    return multiply(x[0] + x[1], kTaps3[0]) + multiply(x[-1] + x[2], kTaps3[1]) + multiply(x[-2] + x[3], kTaps3[2]);
}
// x within [low, high], as clamp() but written as a minimum and a maximum,
// which the Blackfin does without a branch.
inline i32 bound(i32 x, i32 low, i32 high) {
    x = x > high ? high : x;
    return x < low ? low : x;
}
// The side lies under the whole, so its stages can be plainer at the same
// delay: each value between is the mean of its two neighbours, no multiply at
// all. Its images lie at most 34, 39 and 39 dB under the side, at the top of each
// band; they are far lower for most modes, and the whole has none.
const i32 kSideLimit = 45093749;   // the side's clamp: half the tone's (kToneLimit)
template <u32 K, bool Clamp>
__attribute__((noinline)) void twice(const i32* low, const i32* add, i32* out, u32 n) {
    const i32* x = low + K - 1;
    for (u32 j = 0; j < n; ++j, ++x, add += 2, out += 2) {
        const i32 even = x[0] + add[0], odd = between<K>(x) + add[1];
        out[0] = Clamp ? clamp(even, -kStageLimit, kStageLimit) : even;
        out[1] = Clamp ? clamp(odd, -kStageLimit, kStageLimit) : odd;
    }
}
template <u32 K, bool Clamp>
__attribute__((noinline)) void twice(const i32* low, i32* out, u32 n) {
    const i32* x = low + K - 1;
    for (u32 j = 0; j < n; ++j, ++x, out += 2) {
        const i32 odd = between<K>(x);
        out[0] = x[0];
        out[1] = Clamp ? clamp(odd, -kStageLimit, kStageLimit) : odd;
    }
}
// The side's stages: from n inputs after their history, as twice. Means of
// inputs within the clamp stay within it, so only the sums are clamped.
template <u32 K>
__attribute__((noinline)) void side_twice(const i32* low, const i32* add, i32* out, u32 n) {
    const i32* x = low + K - 1;
    const i32 limit = kSideLimit, under = -limit;
    for (u32 j = 0; j < n; ++j, ++x, add += 2, out += 2) {
        out[0] = bound(x[0] + add[0], under, limit);
        out[1] = bound(((x[0] + x[1]) >> 1) + add[1], under, limit);
    }
}
template <u32 K>
__attribute__((noinline)) void side_twice(const i32* low, i32* out, u32 n) {
    const i32* x = low + K - 1;
    for (u32 j = 0; j < n; ++j, ++x, out += 2) {
        out[0] = x[0];
        out[1] = (x[0] + x[1]) >> 1;
    }
}

// The bow's own rosin hiss, a share of its amplitude (Q15), and the shake's
// stationary amplitude at full Shake (amplitude units).
const i32 kRosin = 650, kShakeAmplitude = 1000;
// How far the bow reaches into the plate's upper modes, over the material's
// own reach (Q2): a bare plate shimmers with them, and its sand damps them.
const i32 kHalo = 10;
// The bow's harmonics at full Bow: the second at 0.7 of the bowed level (-3 dB;
// the third and fourth follow at -7 and -12 dB in kHarmonicWave). They rise
// as Bow squared, from -24 dB at 0.3 to -12 dB at the default 0.6: pressing
// harder brightens a bowed tone.
const i32 kHarmonicShare = 23000;
// A landing's click (Q31 per unit of height at unit loudness, >> 8) and how hard
// it knocks the modes (a right shift of height * shape * ring).
const i32 kClickLevel = 2 << 21;
// The output gain (Q31): a held note sits about as loud as Hexagon's Hollow,
// and an eight-note chord peaks between 0.4 and 0.8 of full scale, as the
// set's synths do. It is applied to each mode's amplitude as it is sounded
// (sa and sb hold the sounded amplitude, gain applied), so the output needs
// no multiply a sample; the rattle is mixed at its own level.
const i32 kOutput = 1443000000;
const i32 kRattleLimit = 3 << 26;
const i32 kToneLimit = 90187499;  // the tone's clamp, (2^27 - 1) * kOutput
// The side's tone is within half the tone's clamp (kSideLimit), then at the
// tone's scale; with the rattle's clamp on its rattle, that leaves each channel
// inside 0.44 of full scale.
// Which side of the plate a voice's modes lean to, bit k for mode k (one bit:
// right; the bowed mode's says which way its twin lies), by the voice, so a
// chord's notes spread. Each pattern has three upper modes a side.
const u32 kLeans[4] = {0x55u, 0x2au, 0x35u, 0x4au};
// How far a landing leans, Q15 of its grain's place on the plate.
const i32 kLandingWidth = 26000;
// How far Tilt opens the plate, Q15: from none at 0, in proportion, to all of it
// at three quarters of Tilt, where all the sand is moving anyway.
inline i32 opening(i32 tilt) {
    const i32 width = tilt * 43691 >> 15;
    return width > 32767 ? 32767 : width;
}
inline i32 sounded(i32 amplitude) { return multiply(amplitude, kOutput); }
const u32 kKnock = 6;
const u32 kBounce = 29;  // samples from a landing to its bounce (0.7 ms)
}

// A grain's weight on each mode where it lies (its shape squared), added in
// one straight run.
inline void gather_shapes(i32* gather, const i16* shapes) {
    gather[0] += shapes[0] * shapes[0] >> 15;
    gather[1] += shapes[1] * shapes[1] >> 15;
    gather[2] += shapes[2] * shapes[2] >> 15;
    gather[3] += shapes[3] * shapes[3] >> 15;
    gather[4] += shapes[4] * shapes[4] >> 15;
    gather[5] += shapes[5] * shapes[5] >> 15;
    gather[6] += shapes[6] * shapes[6] >> 15;
}

inline void clear(Mode& m, u32 phase) {
    m.phase = phase;
    m.a = m.b = m.sa = m.sb = 0;
}

// Every grain's kept shapes, for a fresh pour, another note's sand or a new figure.
inline void forget_shapes(Voice& voice) {
    for (u32 i = 0; i < kGrains; ++i) voice.shapes[i][0] = kMoved;
}

inline void initialize(Voice& voice, Shared&, u32 owner) {
    for (u32 k = 0; k < kModes; ++k) {
        clear(voice.modes[k], k * 0x9e3779b9u);
        voice.load[k] = voice.gather[k] = 0;
    }
    clear(voice.twin, 0x5bd1e995u);
    clear(voice.harmonic, 0);
    for (u32 c = 0; c < 8; ++c) voice.controls[c] = 0;
    voice.primed = 0;
    voice.figure = 0;
    voice.cursor = 0;
    voice.seed = owner * 2654435761u | 1u;
    voice.struck = voice.jolted = voice.rounds = 0;
    voice.click1 = voice.click2 = 0;
    voice.seed = voice.seed * 1723622977u + 786704080u;   // start() replaces the grains: skip this pour's 144 draws
    for (u32 k = 0; k < kModes; ++k) voice.lost[k] = 0;
    for (u32 k = 0; k < kModes; ++k) voice.rate[k] = 3;
    voice.live = 0;
    voice.side_live = 0;
    voice.side1 = voice.side2 = 0;
    voice.width = 0;
    for (u32 i = 0; i < kHistory4 + kBlock / 8; ++i) voice.tone.eighth[i] = voice.side.eighth[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 4; ++i) voice.tone.quarter[i] = voice.side.quarter[i] = 0;
    for (u32 i = 0; i < kHistory3 + kBlock / 2; ++i) voice.tone.half[i] = voice.side.half[i] = 0;
}

namespace {
// The latest note's sand is live while that note still renders.
const u32 kStale = 24;
inline bool sounding(const Shared& shared) { return shared.latest && shared.clock - shared.fresh <= kStale; }
inline void publish(Shared& shared, const Voice& voice) {
    for (u32 i = 0; i < kGrains; ++i) shared.sand[i] = voice.grains[i];
}
}

// A note takes over the sand of the note still sounding, or pours fresh sand
// when none is; either way it strikes the plate as it starts (by how little it
// is bowed), and the plate itself rings on from the voice's last note.
inline void start(Voice& voice, Shared& shared, u32 owner) {
    if (sounding(shared)) {
        for (u32 i = 0; i < kGrains; ++i) voice.grains[i] = shared.sand[i];
    } else {
        pour(voice.grains, kGrains, voice.seed);
    }
    forget_shapes(voice);
    for (u32 k = 0; k < kModes; ++k) voice.gather[k] = 0;
    voice.cursor = 0;
    voice.struck = 1;
    shared.latest = owner;
    shared.fresh = shared.clock;
    publish(shared, voice);
}
inline void destroy(Voice&, Shared& shared, u32 owner) {
    if (shared.latest == owner) shared.latest = 0;
}

// The rattle: each landing's impulse through the band-pass, a function of
// its own so its loop is a hardware loop. False when there is none: no landing
// in the block and the last click rung out.
// limit is kHalf / 2, so c1 y1 cannot overflow; it is passed in, so that the
// compiler keeps both clamps as a minimum and a maximum rather than folding
// them into branches, which a Blackfin mispredicts at 8 cycles a sample.
__attribute__((noinline)) bool rattle_of(i32& state1, i32& state2, const i32* impulses, bool clicked, i32 c1, i32 c2, i32 limit, i32* rattle) {
    i32 y1 = state1, y2 = state2;
    if (!clicked && magnitude(y1) + magnitude(y2) < 4096) {
        state1 = state2 = 0;
        return false;
    }
    const i32 low = -limit;
    // Two samples a turn, so the filter's state needs no copying.
    for (u32 i = 0; i < opxy::block_frames; i += 2) {
        y2 = bound(impulses[i] + 2 * multiply(y1, c1) - multiply(y2, c2), low, limit);
        y1 = bound(impulses[i + 1] + 2 * multiply(y2, c1) - multiply(y1, c2), low, limit);
        // Each voice's rattle is clamped at a tenth of full scale, so coincident
        // heavy landings across a chord keep the set's headroom.
        rattle[i] = bound(y2, -kRattleLimit, kRattleLimit);
        rattle[i + 1] = bound(y1, -kRattleLimit, kRattleLimit);
    }
    state1 = y1;
    state2 = y2;
    return true;
}
// The output: the tone, within its clamp and scaled, and the rattle.
template <bool Rattle>
inline i32 output_of(i32 tone, const i32* rattle, u32 i) {
    const i32 t = bound(tone, -kToneLimit, kToneLimit) << 2;
    return Rattle ? bound(t + rattle[i], -kHalf, kHalf) : t;
}
// One frame: the whole and the side, which gives way to the channels' half
// scale, so that the whole is never touched.
inline void place(i32* frame, i32 whole, i32 side) {
    const i32 room = kHalf - (whole < 0 ? -whole : whole);
    const i32 s = bound(side, -room, room);
    frame[0] = whole + s;
    frame[1] = whole - s;
}
// The last half-band stage, to the full rate, writing the output: the half
// rate's sum doubled (K = 3), plus the full rate's (waiting as long), plus the rattle.
// (output is not declared __restrict here: the compiler then reads each input
// afresh rather than sliding them through registers, which costs it the
// hardware loop.)
// (With no side, each channel is simply the mono voice.)
template <bool Full, bool Rattle, bool Side>
__attribute__((noinline)) void last_twice(const i32* low, const i32* full, const i32* rattle, const i32* side, i32* output) {
    const i32* x = low + 2;
    for (u32 j = 0; j < opxy::block_frames; j += 2, ++x) {
        const i32 even = Full ? x[0] + full[j] : x[0], odd = Full ? between<3>(x) + full[j + 1] : between<3>(x);
        const i32 first = output_of<Rattle>(even, rattle, j), second = output_of<Rattle>(odd, rattle, j + 1);
        if (Side) {
            place(output + 2 * j, first, side[j]);
            place(output + 2 * j + 2, second, side[j + 1]);
        } else {
            output[2 * j] = output[2 * j + 1] = first;
            output[2 * j + 2] = output[2 * j + 3] = second;
        }
    }
}
template <bool Rattle, bool Side>
__attribute__((noinline)) void last_direct(const i32* tone, const i32* rattle, const i32* side, opxy::Output output) {
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const i32 m = output_of<Rattle>(tone[i], rattle, i);
        if (Side) place(output + 2 * i, m, side[i]);
        else output[2 * i] = output[2 * i + 1] = m;
    }
}
// The side, opened from one width to the next across the block (Q15), so that
// turning Tilt moves it smoothly. Its values are within 2^29.
__attribute__((noinline)) void open_side(i32* side, i32 from, i32 to) {
    i32 g = from << 16;
    const i32 step = (to - from) << 10;
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        side[i] = static_cast<opxy::i16>(side[i] >> 15) * static_cast<opxy::i16>(static_cast<u32>(g) >> 16);
        g += step;
    }
}

// Back from the slower rates, on one path, as far as the half rate's stage: its
// input is left in chain.half, after its history. The eighth rate's sum, after
// its history, doubles to the quarter rate and joins the quarter rate's (waiting
// 8 of its samples, as late as the stage is); that doubles and joins the half
// rate's (waiting 22). The full rate's sum waits 50, for the caller's last stage.
// A path not written for a block is all zeros, and a stage whose input and
// history are all zeros is skipped. Returns the path's live bits: 0-3 and 4, 5.
template <bool Side>
inline u32 rise(Chain& chain, Sums& sums, i32* full, i32* half, i32* quarter, u32 last) {
    const u32 wrote = sums.written;
    u32 live = wrote & 15u;
    if (wrote & 4u) {
        if (last & 4u) for (u32 i = 0; i < kQuarterWait; ++i) quarter[i] = chain.quarter_wait[i];
        else for (u32 i = 0; i < kQuarterWait; ++i) quarter[i] = 0;
        for (u32 i = 0; i < kQuarterWait; ++i) chain.quarter_wait[i] = sums.at[2][opxy::block_frames / 4 - kQuarterWait + i];
    }
    if (wrote & 2u) {
        if (last & 2u) for (u32 i = 0; i < kHalfWait; ++i) half[i] = chain.half_wait[i];
        else for (u32 i = 0; i < kHalfWait; ++i) half[i] = 0;
        for (u32 i = 0; i < kHalfWait; ++i) chain.half_wait[i] = sums.at[1][opxy::block_frames / 2 - kHalfWait + i];
    }
    if (wrote & 1u) {
        if (last & 1u) for (u32 i = 0; i < kFullWait; ++i) full[i] = chain.full_wait[i];
        else for (u32 i = 0; i < kFullWait; ++i) full[i] = 0;
        for (u32 i = 0; i < kFullWait; ++i) chain.full_wait[i] = sums.at[0][opxy::block_frames - kFullWait + i];
    }
    // A tail with nothing after it: the wait line alone, then zeros.
    if ((last & 4u) && !(wrote & 4u)) {
        for (u32 i = 0; i < kQuarterWait; ++i) quarter[i] = chain.quarter_wait[i];
        for (u32 i = kQuarterWait; i < kQuarterWait + opxy::block_frames / 4; ++i) quarter[i] = 0;
    }
    if ((last & 2u) && !(wrote & 2u)) {
        for (u32 i = 0; i < kHalfWait; ++i) half[i] = chain.half_wait[i];
        for (u32 i = kHalfWait; i < kHalfWait + opxy::block_frames / 2; ++i) half[i] = 0;
    }
    if ((last & 1u) && !(wrote & 1u)) {
        for (u32 i = 0; i < kFullWait; ++i) full[i] = chain.full_wait[i];
        for (u32 i = kFullWait; i < kFullWait + opxy::block_frames; ++i) full[i] = 0;
    }
    const bool to_quarter = (wrote | last) & 4u, to_half = (wrote | last) & 2u;
    // Eighth to quarter.
    i32* const stage4 = chain.quarter + kHistory3;
    const bool eighth_on = (wrote | last) & 8u;
    if (eighth_on) {
        if (!(wrote & 8u)) for (u32 i = 0; i < opxy::block_frames / 8; ++i) sums.at[3][i] = 0;
        const i32 limit = Side ? kSideLimit : kStageLimit;
        for (u32 i = 0; i < opxy::block_frames / 8; ++i) sums.at[3][i] = clamp(sums.at[3][i], -limit, limit);
        if (Side) {
            if (to_quarter) side_twice<4>(chain.eighth, quarter, stage4, opxy::block_frames / 8);
            else side_twice<4>(chain.eighth, stage4, opxy::block_frames / 8);
        } else {
            if (to_quarter) twice<4, true>(chain.eighth, quarter, stage4, opxy::block_frames / 8);
            else twice<4, true>(chain.eighth, stage4, opxy::block_frames / 8);
        }
        for (u32 i = 0; i < kHistory4; ++i) chain.eighth[i] = chain.eighth[opxy::block_frames / 8 + i];
    } else if (to_quarter) {
        const i32 limit = Side ? kSideLimit : kStageLimit;
        for (u32 i = 0; i < opxy::block_frames / 4; ++i) stage4[i] = clamp(quarter[i], -limit, limit);
    }
    const bool quarter_in = eighth_on || to_quarter;
    if (quarter_in) live |= 16u;
    // Quarter to half.
    i32* const stage3 = chain.half + kHistory3;
    const bool quarter_on = quarter_in || (last & 16u);
    if (quarter_on) {
        if (!quarter_in) for (u32 i = 0; i < opxy::block_frames / 4; ++i) stage4[i] = 0;
        if (Side) {
            if (to_half) side_twice<3>(chain.quarter, half, stage3, opxy::block_frames / 4);
            else side_twice<3>(chain.quarter, stage3, opxy::block_frames / 4);
        } else {
            if (to_half) twice<3, true>(chain.quarter, half, stage3, opxy::block_frames / 4);
            else twice<3, true>(chain.quarter, stage3, opxy::block_frames / 4);
        }
        for (u32 i = 0; i < kHistory3; ++i) chain.quarter[i] = chain.quarter[opxy::block_frames / 4 + i];
    } else if (to_half) {
        const i32 limit = Side ? kSideLimit : kStageLimit;
        for (u32 i = 0; i < opxy::block_frames / 2; ++i) stage3[i] = clamp(half[i], -limit, limit);
    }
    const bool half_in = quarter_on || to_half;
    if (half_in) live |= 32u;
    if (!half_in && (last & 32u)) for (u32 i = 0; i < opxy::block_frames / 2; ++i) stage3[i] = 0;
    return live;
}

// Controls: Figure, Bow, Sand, Grain; Plate, Settle, Tilt, Shake.
inline void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (u32 c = 1; c < 8; ++c) {
        // Plate picks metal, glass or wood; between blocks it glides from one to the next.
        const u32 plate_index = c == 4 ? choice(parameters[4], 3) : 0;
        const i32 target = c != 4 ? control(parameters[c]) : plate_index == 2 ? 32767 : static_cast<i32>(plate_index) * 16384;
        voice.controls[c] = voice.primed ? voice.controls[c] + ((target - voice.controls[c]) >> 2) : target;
    }
    voice.primed = 1;
    const i32 bow = voice.controls[1], sand = voice.controls[2], grain = voice.controls[3],
              plate = voice.controls[4], settle = voice.controls[5], tilt = voice.controls[6],
              shake = voice.controls[7];
    // How far the plate is open: while it is shut all the work below stays on
    // the mono path.
    const i32 width = opening(tilt), width_was = voice.width;
    const bool wide = width || width_was;
    voice.width = width;
    // A new figure bows another mode at the same pitch: the amplitudes carry
    // on into the new modes, and the sand, lying on the old figure's nodal
    // lines, finds itself on moving ground.
    const u32 figure = choice(parameters[0], kFigures);
    if (figure != voice.figure) forget_shapes(voice);
    voice.figure = figure;
    ++shared.clock;
    if (shared.latest == owner) shared.fresh = shared.clock;
    const PlateMode* const modes = kFigureModes[voice.figure];
    const Material mat = material(plate);
    const u32 increment = pitch[0] < kLowestIncrement ? kLowestIncrement : pitch[0];
    const i32 octave = log2_q16(increment);
    const u32 grains = static_cast<u32>(sand * static_cast<i32>(kGrains) + 16384) >> 15;
    const i32 mass = 96 + (grain * 288 >> 15);   // Q8: dust 0.375 to pebbles 1.5
    const i32 loud = 154 + (grain * 153 >> 15);  // Q8: a click's loudness, the root of its mass

    // How the plate throws sand this block, from what each mode is doing now.
    // The twin and the bow's harmonics move in the figure's own shape, so
    // they throw the sand with it, from the same antinodes (the harmonics, like
    // the plate's other modes, at a quarter); together they lift it 5/8 as
    // hard per unit as the figure's mode alone, so a bowed note's sand still
    // rests within a tenth of its swing of the lines.
    Throw t;
    for (u32 k = 1; k < kModes; ++k) t.weight[k] = lift_other(swing(voice.modes[k]), modes[k].ratio);
    const i32 figure_lift = lift(swing(voice.modes[0]) + swing(voice.twin), modes[0].ratio) +
                            (lift(magnitude(voice.harmonic.a) >> 16, modes[0].ratio * 3) >> 2);
    t.weight[0] = (figure_lift > 65535 ? 65535 : figure_lift) * 5 >> 3;
    plan_throw(t, settle, tilt, shake);
    // A strike jolts every grain on the plate for one round of looks.
    const i32 strike = strike_share(bow);
    if (voice.struck) voice.jolted = kGrains;
    if (voice.jolted) {
        t.jitter += strike * kJolt >> 15;
        t.jolts = 32767;
        voice.jolted = voice.jolted > kLooks ? voice.jolted - kLooks : 0;
    }

    // The rattle's band-pass, by Grain: each landing's impulse goes in through
    // its gain, g, as it is made.
    const u32 step = static_cast<u32>(grain) * (kClickSteps - 1);
    const u32 low = step >> 15;
    const i32 toward = static_cast<i32>(step & 32767u);
    const u32 high = low + 1 < kClickSteps ? low + 1 : low;
    const i32 g = mix(kClick[low][0] >> 1, kClick[high][0] >> 1, toward) << 1;
    const i32 c1 = mix(kClick[low][1] >> 1, kClick[high][1] >> 1, toward) << 1;
    const i32 c2 = mix(kClick[low][2] >> 1, kClick[high][2] >> 1, toward) << 1;
    // Look at the next grains: gather their weight on the modes, and click
    // and knock the plate for each that lands.
    // The landings wait for the slow paths: this block's are written kFullWait on,
    // after the last block's that are still to come.
    i32 waiting[kFullWait + opxy::block_frames];
    if (voice.live & 64u) {
        for (u32 i = 0; i < kFullWait; ++i) waiting[i] = voice.click_wait[i];
    } else {
        for (u32 i = 0; i < kFullWait; ++i) waiting[i] = 0;
    }
    i32* const impulses = waiting + kFullWait;
    for (u32 i = 0; i < opxy::block_frames; ++i) impulses[i] = 0;
    // What the same landings add to the side, each by where its grain lies;
    // left untouched until a landing writes to it, as most blocks have none.
    i32 side_waiting[kFullWait + opxy::block_frames];
    i32* const side_impulses = side_waiting + kFullWait;
    bool clicked = false;
    i32 knock_a[kModes], knock_b[kModes];
    for (u32 k = 0; k < kModes; ++k) knock_a[k] = knock_b[k] = 0;
    u32 seed = voice.seed;
    const i32 ring = mat.ring * mass >> 8;
    // Settle in the rattle: slippery sand lands hard and bounces, sticky sand
    // lands dead and stays.
    const i32 dead = 384 - (settle * 256 >> 15);  // Q8: 1.5 to 0.5
    const i32 slip = 32767 - settle;
    const i32 bounce = (slip * slip >> 15) * 19661 >> 15;  // Q15: up to 0.6
    for (u32 n = 0; n < kLooks; ++n) {
        const u32 at = voice.cursor;
        if (++voice.cursor == kGrains) {
            voice.cursor = 0;
            for (u32 k = 0; k < kModes; ++k) {
                voice.load[k] = voice.gather[k];
                voice.gather[k] = 0;
            }
            // Every fourth round (23 ms), the plate's sand, for the next note.
            if (shared.latest == owner && !(++voice.rounds & 3)) publish(shared, voice);
        }
        if (at >= grains) continue;
        const i16* const shapes = voice.shapes[at];
        const u32 seen = look(voice.grains[at], t, modes, seed, voice.shapes[at]);
        if (!(seen & on_plate)) continue;
        const bool placed = shapes[0] != kMoved;  // not run off the plate this look
        if (placed)
            gather_shapes(voice.gather, shapes);
        const i32 height = static_cast<i32>(seen & 255u);
        if (!height) continue;
        seed = seed * 1664525u + 1013904223u;
        const i32 sign = static_cast<i32>(seed) < 0 ? -1 : 1;
        const i32 click = (multiply(sign * height * loud * (kClickLevel >> 8), g) >> 8) * dead;
        i32& impulse = impulses[seed >> 26];
        impulse = clamp(impulse + click, -kHalf, kHalf);
        // Slippery sand bounces: a second, softer click a moment later.
        i32& again = impulses[((seed >> 26) + kBounce) & (opxy::block_frames - 1)];
        again = clamp(again - (click >> 15) * bounce, -kHalf, kHalf);
        // The same, on the side, by which way its grain lies across the plate.
        if (wide) {
            if (!clicked) for (u32 i = 0; i < opxy::block_frames; ++i) side_impulses[i] = 0;
            const i32 across = ((static_cast<i32>(voice.grains[at].u) - 32768) * kLandingWidth) >> 15;
            const i32 leaning = (click >> 15) * across;
            i32& side_impulse = side_impulses[seed >> 26];
            side_impulse = clamp(side_impulse + leaning, -kHalf, kHalf);
            i32& side_again = side_impulses[((seed >> 26) + kBounce) & (opxy::block_frames - 1)];
            side_again = clamp(side_again - (leaning >> 15) * bounce, -kHalf, kHalf);
        }
        clicked = true;
        if (!placed) continue;
        const i32 hit = height * ring;  // at most 255 * 49152
        // The landing knocks every mode by its shape where it lands, in phase
        // or in quadrature at random.
        i32* const knocked = seed & 256u ? knock_a : knock_b;
        const i32 struck = sign * (hit >> 6);
        for (u32 k = 0; k < kModes; ++k) knocked[k] += struck * (shapes[k] >> 6) >> kKnock;
    }

    // The modes. The bow drives the figure's mode to its level and the upper
    // modes faintly, and damps each toward its level in proportion to Bow; a
    // new note strikes them all at once, as hard as it is not bowed. The
    // sand's weight flattens, damps and quietens each mode it lies on; rosin
    // and Shake stir them; landings knock them.
    // The sums at each rate. The faster rates' sums wait for the eighth's:
    // each starts with the last block's tail still to come.
    i32 full[kFullWait + opxy::block_frames], half[kHalfWait + opxy::block_frames / 2],
        quarter[kQuarterWait + opxy::block_frames / 4];
    Sums sums;
    sums.at[0] = full + kFullWait;
    sums.at[1] = half + kHalfWait;
    sums.at[2] = quarter + kQuarterWait;
    sums.at[3] = voice.tone.eighth + kHistory4;
    sums.written = 0;
    // The upper modes go in two buses, sums (left) and side_sums (right), which
    // become the whole and the difference once they have all sounded: so the
    // bowed mode, which stays in the middle, waits until then.
    i32 side_full[kFullWait + opxy::block_frames], side_half[kHalfWait + opxy::block_frames / 2],
        side_quarter[kQuarterWait + opxy::block_frames / 4];
    Sums side_sums;
    side_sums.at[0] = side_full + kFullWait;
    side_sums.at[1] = side_half + kHalfWait;
    side_sums.at[2] = side_quarter + kQuarterWait;
    side_sums.at[3] = voice.side.eighth + kHistory4;
    side_sums.written = 0;
    const u32 leans = kLeans[owner & 3u];
    // The bowed mode's block, as worked out in its turn below.
    bool bowed_fades = false;
    i32 ca = 0, cb = 0, ha = 0, tsa = 0, tsb = 0;
    u32 bowed_inc = 0;
    const i32 drive = bow_level(bow);
    const i32 rosin = drive * kRosin >> 15;
    const i32 shaken = (((shake * shake >> 15) + shake) >> 1) * kShakeAmplitude >> 15;  // halfway between Shake and its square
    const i32 hit = voice.struck ? strike * kStrikeAmplitude >> 15 : 0;
    const i32 held = multiply(mat.rise, bow << 16);  // the bow's pull, by how much it bows
    for (u32 k = 0; k < kModes; ++k) {
        const PlateMode& pm = modes[k];
        Mode& m = voice.modes[k];
        Sums& bus = wide && (leans >> k & 1u) ? side_sums : sums;
        if (octave + pm.log2_ratio > kTopLog2) {  // above the top: fade out, then rest
            if (!k) {
                bowed_fades = true;
                continue;
            }
            if (m.sa || m.sb) sound_at(bus, 0, m.phase, kTopIncrement, m.sa, m.sb, -(m.sa >> 6), -(m.sb >> 6));
            m.a = m.b = m.sa = m.sb = 0;
            continue;
        }
        const i32 weight = voice.load[k] * mass >> 8;  // 48 grains at a mode's antinodes: about 1.6M * mass
        u32 inc = times(increment, pm.ratio);
        const i32 flatten = weight >> 11 > 1311 ? 1311 : weight >> 11;  // Q16, at most 2%
        inc -= times(inc, static_cast<u32>(flatten));
        const i32 log2_loss = mat.log2_loss + ((mat.tilt >> 4) * (pm.log2_ratio >> 4) >> 8);
        // The sand on a mode's moving ground damps it; settled on the figure's
        // lines, it lies on the upper modes' moving ground, so more or heavier
        // sand leaves a purer, darker tone.
        if (log2_loss != voice.lost[k]) {
            voice.lost[k] = log2_loss;
            voice.kept[k] = keep(log2_loss);
            i32 root = (log2_loss + 65536) >> 1;
            if (root > 0) root = 0;
            voice.stirs[k] = exp2_q16(root + 50352);
        }
        i32 decay = voice.kept[k] - (k ? weight << 8 : weight << 4);
        decay = decay < 0 ? 0 : decay;
        const i32 quiet = weight >> 4 > 16384 ? 16384 : weight >> 4;
        const i32 reach = k ? (mat.halo * kHalo >> 2) * pm.bow >> 15 : 32767 - quiet;
        const i32 target = (drive * reach >> 15) << 16;
        const i32 rate = k ? held >> 2 : held;
        const i32 noise = static_cast<i32>(times(static_cast<u32>(shaken + (rosin * pm.bow >> 15)), voice.stirs[k]));
        seed = seed * 1664525u + 1013904223u;
        const i32 na = (static_cast<i32>(seed) >> 16) * noise * 2;
        seed = seed * 1664525u + 1013904223u;
        const i32 nb = (static_cast<i32>(seed) >> 16) * noise * 2;
        // The strike: the bowed mode fully, the others by how strongly the
        // struck spot (the bow's) reaches them, as a bell's partials.
        const i32 struck = (k ? hit * (pm.bow >> 1) >> 15 : hit) << 16;
        const i32 a1 = clamp_amplitude(multiply(m.a, decay) + multiply(target - m.a, rate) + na + knock_a[k] +
                                       (static_cast<i32>(seed) < 0 ? -struck : struck));
        const i32 b1 = clamp_amplitude(multiply(m.b, decay) - multiply(m.b, rate) + nb + knock_b[k]);
        m.a = a1;
        m.b = b1;
        if (k) {
            const i32 sa = sounded(a1), sb = sounded(b1);
            // A mode moving to another rate fades out at the old one as it
            // fades in at the new, the two ramps summing to its own.
            const u32 was = voice.rate[k], r = rate_for(was, inc);
            if (r == was) {
                m.phase = sound_at(bus, r, m.phase, inc, m.sa, m.sb, (sa - m.sa) >> 6, (sb - m.sb) >> 6);
            } else {
                if (m.sa || m.sb) sound_at(bus, was, m.phase, inc, m.sa, m.sb, (-m.sa) >> 6, (-m.sb) >> 6);
                m.phase = sound_at(bus, r, m.phase, inc, 0, 0, sa >> 6, sb >> 6);
                voice.rate[k] = static_cast<u8>(r);
            }
            m.sa = sa;
            m.sb = sb;
            continue;
        }
        // The bowed mode's twin, its near-degenerate partner: the same shape
        // and loss, a fraction of a hertz above it, so the tone beats gently.
        // Over a block it barely turns against the bowed mode, so it is sounded
        // as part of it: its amplitude, turned by the beat's phase (the twin's
        // phase field), added to the mode's. Being the weaker, it leaves the
        // pair's average frequency at the note.
        Mode& twin = voice.twin;
        const u32 beat = static_cast<u32>((kBeat[voice.figure] * 97391u >> 8) * static_cast<u32>(mat.beat) >> 12);
        const i32 twin_target = ((drive * reach >> 15) * mat.twin >> 15) << 16;
        seed = seed * 1664525u + 1013904223u;
        const i32 tn = (static_cast<i32>(seed) >> 16) * (noise * mat.twin >> 15) * 2;
        twin.a = clamp_amplitude(multiply(twin.a, decay) + multiply(twin_target - twin.a, held) + tn + ((hit * mat.twin >> 15) << 16));
        twin.b = clamp_amplitude(multiply(twin.b, decay) - multiply(twin.b, held));
        twin.phase += beat << 6;
        const opxy::i16* const turn = kSine + (twin.phase >> 22);
        const i32 ta = (twin.a >> 15) * turn[256] - (twin.b >> 15) * turn[0], tb = (twin.a >> 15) * turn[0] + (twin.b >> 15) * turn[256];
        ca = sounded(clamp_amplitude(a1 + ta));
        cb = sounded(clamp_amplitude(b1 + tb));
        // The twin's other standing wave lies across the plate: on the side,
        // it swings the tone from one side to the other as the pair beat.
        tsa = sounded(clamp_amplitude(ta));
        tsb = sounded(clamp_amplitude(tb));
        if (leans & 1u) {
            tsa = -tsa;
            tsb = -tsb;
        }
        // The bow's stick and slip: the note's 2nd to 4th harmonics, in tune
        // with it, rising with Bow (pressure brightens a bowed tone); faded
        // out where the 4th would pass the top.
        Mode& harm = voice.harmonic;
        const i32 level = inc > kTopIncrement / 4 ? 0 : (bow * bow >> 15) * kHarmonicShare >> 15;  // Q15 of the bowed level
        const i32 harm_target = static_cast<i32>(times(static_cast<u32>(drive * (level * (mat.harmonic >> 3) >> 9) >> 15),
                                                       static_cast<u32>(kHarmonicPeak) << 4)) << 16;
        harm.a = clamp_amplitude(multiply(harm.a, decay) + multiply(harm_target - harm.a, held));
        ha = sounded(harm.a);
        bowed_inc = inc;
    }
    // Left and right become the whole and the difference, at each rate.
    if (wide) for (u32 r = 0; r < 4; ++r) {
        if (!((sums.written | side_sums.written) & (1u << r))) continue;
        clear_sum(sums, r);
        clear_sum(side_sums, r);
        i32* const whole = sums.at[r];
        i32* const apart = side_sums.at[r];
        for (u32 i = 0; i < (opxy::block_frames >> r); ++i) {
            const i32 a = whole[i], b = apart[i];
            whole[i] = a + b;
            apart[i] = a - b;
        }
    }
    // The bowed mode, its harmonics and, on the side alone, its twin.
    {
        Mode& m = voice.modes[0];
        Mode& harm = voice.harmonic;
        Mode& twin = voice.twin;
        if (!wide) tsa = tsb = 0;   // shut, the twin has nothing on the side, and starts from nothing
        if (bowed_fades) {
            if (m.sa || m.sb) sound_at(sums, 0, m.phase, kTopIncrement, m.sa, m.sb, -(m.sa >> 6), -(m.sb >> 6));
            if (twin.sa || twin.sb) sound_at(side_sums, 0, m.phase, kTopIncrement, twin.sa, twin.sb, -(twin.sa >> 6), -(twin.sb >> 6));
            m.a = m.b = m.sa = m.sb = 0;
            twin.a = twin.b = twin.sa = twin.sb = harm.a = harm.sa = 0;
        } else {
            // Its highest sound is the 4th harmonic while the harmonics sound.
            const u32 inc = bowed_inc;
            const u32 top = !harm.sa && !ha ? inc : inc > 0x3fffffffu ? 0xffffffffu : inc << 2;
            const u32 was = voice.rate[0], r = rate_for(was, top);
            const u32 phase = m.phase;
            if (r == was) {
                m.phase = sound_bowed_at(sums, r, phase, inc, m.sa, m.sb, (ca - m.sa) >> 6, (cb - m.sb) >> 6, harm.sa, (ha - harm.sa) >> 6);
                if (twin.sa || twin.sb || tsa || tsb) sound_at(side_sums, r, phase, inc, twin.sa, twin.sb, (tsa - twin.sa) >> 6, (tsb - twin.sb) >> 6);
            } else {
                if (m.sa || m.sb || harm.sa)
                    sound_bowed_at(sums, was, phase, inc, m.sa, m.sb, (-m.sa) >> 6, (-m.sb) >> 6, harm.sa, (-harm.sa) >> 6);
                if (twin.sa || twin.sb) sound_at(side_sums, was, phase, inc, twin.sa, twin.sb, (-twin.sa) >> 6, (-twin.sb) >> 6);
                m.phase = sound_bowed_at(sums, r, phase, inc, 0, 0, ca >> 6, cb >> 6, 0, ha >> 6);
                if (tsa || tsb) sound_at(side_sums, r, phase, inc, 0, 0, tsa >> 6, tsb >> 6);
                voice.rate[0] = static_cast<u8>(r);
            }
            m.sa = ca;
            m.sb = cb;
            harm.sa = ha;
            twin.sa = tsa;
            twin.sb = tsb;
        }
    }
    voice.struck = 0;
    voice.seed = seed;

    // Back to the full rate, the whole and the side alike.
    const u32 last = voice.live, side_last = voice.side_live;
    u32 live = rise<false>(voice.tone, sums, full, half, quarter, last);
    const bool to_full = (sums.written | last) & 1u;
    // (Shut, the side only plays out what it held, which nothing will hear.)
    const u32 side_up = wide || (side_last & 63u) ? rise<true>(voice.side, side_sums, side_full, side_half, side_quarter, side_last & 63u) : 0;
    const bool side_to_full = (side_sums.written | side_last) & 1u;
    const bool side_waited = side_last & 64u;
    // The rattle, and half to full: the output.
    const bool waited = last & 64u;
    for (u32 i = 0; i < kFullWait; ++i) voice.click_wait[i] = impulses[opxy::block_frames - kFullWait + i];
    // A block with no landing has only the last block's waiting landings; with
    // none of those either, the side's filter (if it still rings) reads zeros.
    const bool side_filter = wide && (clicked || side_waited || voice.side1 || voice.side2);
    if (side_filter) {
        if (side_waited) for (u32 i = 0; i < kFullWait; ++i) side_waiting[i] = voice.side_wait[i];
        else for (u32 i = 0; i < kFullWait; ++i) side_waiting[i] = 0;
        if (!clicked) for (u32 i = 0; i < opxy::block_frames; ++i) side_impulses[i] = 0;
    }
    if (wide && clicked) for (u32 i = 0; i < kFullWait; ++i) voice.side_wait[i] = side_impulses[opxy::block_frames - kFullWait + i];
    if (!wide) voice.side1 = voice.side2 = 0;
    i32 rattle[opxy::block_frames];
    const bool rattling = rattle_of(voice.click1, voice.click2, waiting, clicked || waited, c1, c2, kHalf >> 1, rattle);
    // The side: its tone, at the tone's scale, then its rattle, then opened.
    i32 apart[opxy::block_frames];
    const bool side_half_on = (side_up | side_last) & 32u;
    if (side_half_on) {
        if (wide) {
            const i32* x = voice.side.half + kHistory3 - 1;
            for (u32 j = 0; j < opxy::block_frames; j += 2, ++x) {
                apart[j] = x[0] << 2;
                apart[j + 1] = (x[0] + x[1]) << 1;
            }
            if (side_to_full)
                for (u32 i = 0; i < opxy::block_frames; ++i) apart[i] += bound(side_full[i], -kSideLimit, kSideLimit) << 2;
        }
        for (u32 i = 0; i < kHistory3; ++i) voice.side.half[i] = voice.side.half[opxy::block_frames / 2 + i];
    } else if (wide) {
        if (side_to_full) for (u32 i = 0; i < opxy::block_frames; ++i) apart[i] = bound(side_full[i], -kSideLimit, kSideLimit) << 2;
        else for (u32 i = 0; i < opxy::block_frames; ++i) apart[i] = 0;
    }
    if (side_filter) {
        i32 side_rattle[opxy::block_frames];
        if (rattle_of(voice.side1, voice.side2, side_waiting, clicked || side_waited, c1, c2, kHalf >> 1, side_rattle))
            for (u32 i = 0; i < opxy::block_frames; ++i) apart[i] += side_rattle[i];
    }
    if (wide) open_side(apart, width_was, width);
    const bool half_on = (live | last) & 32u;
    if (half_on) {
        if (wide) {
            if (to_full && rattling) last_twice<true, true, true>(voice.tone.half, full, rattle, apart, output);
            else if (to_full) last_twice<true, false, true>(voice.tone.half, full, rattle, apart, output);
            else if (rattling) last_twice<false, true, true>(voice.tone.half, full, rattle, apart, output);
            else last_twice<false, false, true>(voice.tone.half, full, rattle, apart, output);
        } else {
            if (to_full && rattling) last_twice<true, true, false>(voice.tone.half, full, rattle, apart, output);
            else if (to_full) last_twice<true, false, false>(voice.tone.half, full, rattle, apart, output);
            else if (rattling) last_twice<false, true, false>(voice.tone.half, full, rattle, apart, output);
            else last_twice<false, false, false>(voice.tone.half, full, rattle, apart, output);
        }
        for (u32 i = 0; i < kHistory3; ++i) voice.tone.half[i] = voice.tone.half[opxy::block_frames / 2 + i];
    } else {
        i32 tone[opxy::block_frames];
        for (u32 i = 0; i < opxy::block_frames; ++i) tone[i] = to_full ? full[i] : 0;
        if (wide) {
            if (rattling) last_direct<true, true>(tone, rattle, apart, output);
            else last_direct<false, true>(tone, rattle, apart, output);
        } else {
            if (rattling) last_direct<true, false>(tone, rattle, apart, output);
            else last_direct<false, false>(tone, rattle, apart, output);
        }
    }
    voice.side_live = static_cast<u8>(side_up | (wide && clicked ? 64u : 0u));
    if (clicked) live |= 64u;
    voice.live = live;
}
}
