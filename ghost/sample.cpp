// Ghost: apparitions between the ears (README, "How it works"). Each note is a
// pair of tuned resonators, one per ear, excited by breath, a hummed vowel or
// the loaded sample. A tap half a period back divides their harmonics between
// the ears, even to one and odd to the other, so the whole note exists only
// when the ears fuse it; the right resonator can be detuned to beat against
// the left; and a model of the head places the note round the listener.
#include "core.h"
#include <opxy/q31.hpp>

namespace ghost {
namespace {
const i32 one = 0x7fffffff;                 // 1.0 in Q31, near enough
const i32 minus_one = -one - 1;
const i32 pi_q29 = 1686629713;              // pi, Q29
const u32 lowest = 4380000u, highest = 268000000u;   // 44.1 Hz and 2.75 kHz: notes fold by octaves into the lines' reach
// The level follower's target: the resonators' power (of samples / 16, squared)
// that puts a voice at about -24 dBFS RMS, Hexagon's level, at middle distance.
const u32 target_power = 72900;             // (4320 / 16)^2
const i32 cent_q8 = 4845;                   // ln 2 / 1200 per Q8 cent, Q31
const i32 formant_damping = 3932;           // the vowel filters' 1 / Q, Q15 (Q about 8)
// The loop low-pass by vowel (Q15): breath bright, oo dark, brightening to ee.
const i32 loop_bright[vowel_count] = {27000, 13000, 16000, 20000, 23000, 26000};
// Its delay, (1 - k) / k samples, Q16: it comes off the period, so the note stays in tune.
const i32 loop_lag[vowel_count] = {14000, 99655, 68682, 41838, 27833, 17060};

inline i32 mul(i32 a, i32 b) { return opxy::q31::multiply(a, b); }
// What excites the resonators (breath, hum, bed, the vowel and the notch) runs
// at half rate, 32 samples a block: it lies well under 11 kHz, and the
// resonators run at the full rate.
const u32 half_frames = opxy::block_frames / 2;
inline i32 clip16(i32 x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }

// Every Ghost voice on every track shares the pool of lines. Sample engines
// get no call when a voice ends, so voices are counted, not tracked: a voice
// rendering a second time in a cycle starts the next, and a slot not rendered
// for two cycles is free again.
Lines lines[pool_size];
struct Slot { const Voice* owner; u32 last; };
Slot slots[pool_size];
u32 census;
u32 notes_started;

u32 next(u32& state) {
    u32 x = state ? state : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state = x;
}
u32 mix(u32 x) {
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    x ^= x >> 16;
    return x ? x : 1;
}

// a / b in 16-bit fixed point, by long division, without a 64-bit divide.
u32 ratio(u32 a, u32 b) {
    if (!b) return 0;
    u32 whole = a / b, remainder = a % b, fraction = 0;
    for (u32 bit = 0; bit < 16; ++bit) {
        const bool carry = remainder >= 0x80000000u;
        remainder <<= 1;
        fraction <<= 1;
        if (carry || remainder >= b) { remainder -= b; fraction |= 1; }
    }
    return whole << 16 | fraction;
}

// (a * b) >> 16 in 32-bit arithmetic, exact while the result fits.
u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}

u32 square_root(u32 x) {
    u32 r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// 1 - cos w for w (Q31 radians) up to about 0.4, Q31.
i32 one_minus_cos(i32 w) {
    const i32 w2 = mul(w, w), w4 = mul(w2, w2);
    return (w2 >> 1) - mul(w4, 89478485) + mul(mul(w4, w2), 2982616);   // w^2/2 - w^4/24 + w^6/720
}

// An allpass whose phase passes -180 degrees at w, over a band 2x wide (x:
// half the band, Q31 radians); (x + A x) / 2 is then a notch at w.
Coefficients flip(i32 w, i32 x) {
    const i32 t = x + mul(mul(mul(x, x), x), 715827883), t2 = mul(t, t);   // tan x
    const i32 g = one - t + t2 - mul(t2, t) + mul(t2, t2);                // 1 / (1 + t)
    Coefficients c;
    c.q = mul(t, g) << 1;
    const i32 m = one_minus_cos(w);
    c.a1h = minus_one + (c.q >> 1) + m - (mul(c.q, m) >> 1);
    return c;
}

// The notch through a block, mixed in by depth (Q15): the note's fundamental
// taken out, so the ears hear only harmonics that imply it.
void remove_fundamental(Section& s, const Coefficients& c, i32 depth, i32* buffer) {
    i32 x1 = s.x1, x2 = s.x2, y1 = s.y1, y2 = s.y2;
    const i32 q = c.q, a1h = c.a1h;
    for (u32 i = 0; i < half_frames; ++i) {
        const i32 x = buffer[i], d = x - y2;
        const i32 y = d - mul(q, d) + mul(a1h, x1 - y1) * 2 + x2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        buffer[i] = x - ((((x - y) >> 1) * depth) >> 15);
    }
    s.x1 = x1; s.x2 = x2; s.y1 = y1; s.y2 = y2;
}

// The bed into buffer (adding), at half rate: the sample at its own rate,
// mono, looping in [begin, end), times gain (Q15).
template <bool Stereo>
inline void read_bed(Voice& v, i32* buffer, i32 gain, u32 begin, u32 end) {
    const opxy::i16* data = v.sample.data;
    u32 frame = v.frame, fraction = v.fraction;
    const u32 step = v.step * 2;
    for (u32 i = 0; i < half_frames; ++i) {
        // The nearest frame: the bed is mostly whitened, so it needs no more.
        const i32 a = Stereo ? (i32(data[2 * frame]) + data[2 * frame + 1]) >> 1 : data[frame];
        buffer[i] += (a * gain) >> 15;
        fraction += step;
        frame += fraction >> 16;
        fraction &= 0xffff;
        if (frame >= end) frame -= end - begin;   // a step is under 6 frames; the region at least 8
    }
    v.frame = frame;
    v.fraction = fraction;
}

// What excites the resonators: breath (noise), a hummed voice (a glottal
// pulse at the note) and the bed, through the vowel's two formants.
struct Exciter {
    i32 breath, hum, bed;     // gains, Q15
    i32 whisper;              // without a sample, the bed's white breath in gusts, Q15
    i32 f[2];                 // the formants, 2 sin(pi f / fs), Q15
    i32 vowel;                // how much of the excitation passes the formants, Q15
};

// Breath and the bed, which both ears share, into common (half rate).
// Without a sample the bed is a whisper of Ghost's own: white breath in gusts.
template <bool Whisper>
void breathe(Voice& v, const Exciter& x, u32 begin, u32 end, i32* common) {
    for (u32 i = 0; i < half_frames; ++i) common[i] = 0;
    if (x.bed) {
        if (v.sample.channels == 2) read_bed<true>(v, common, x.bed, begin, end);
        else read_bed<false>(v, common, x.bed, begin, end);
    }
    u32 state = v.noise;
    i32 air = v.air;
    for (u32 i = 0; i < half_frames; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        air += mul((i32(state) >> 16) - air, 20840 << 16);               // breath, softened above 3 kHz
        // The bed, mostly whitened: its sign flipped at random, so it keeps its
        // envelope and grain but rings every harmonic of the note, not its own.
        const i32 bed = common[i];
        if (Whisper) common[i] = (air * x.breath + (i32(state) >> 16) * x.whisper) >> 15;
        else common[i] = (state & 1 ? bed : -((bed * 13107) >> 15)) + ((air * x.breath) >> 15);   // +1 or -0.4: 0.3 dry, 0.7 white
    }
    v.noise = state;
    v.air = air;
}

// One ear's excitation at half rate: the shared breath and bed, and this
// ear's hum (a sawtooth at its own pitch, which the formants and resonators
// shape), through the vowel's two formants.
void excite(const Exciter& x, u32& phase, i32* state, u32 increment, const i32* common, i32* half) {
    i32 low1 = state[0], band1 = state[1], low2 = state[2], band2 = state[3];
    // Q31 coefficients: each product is one Blackfin instruction.
    const i32 f1 = x.f[0] << 16, f2 = x.f[1] << 16, vowel = x.vowel << 16, hum = x.hum << 15;
    const i32 damping = formant_damping << 16, sing = formant_damping << 15;   // 1 / Q, and half of it: the formants' make-up
    for (u32 i = 0; i < half_frames; ++i) {
        const i32 saw = i32(phase >> 16) - 32768;
        phase += increment;
        const i32 in = common[i] + mul(saw, hum);
        // Two Chamberlin band-passes: the vowel's formants.
        low1 += mul(f1, band1);
        band1 += mul(f1, in - low1 - mul(damping, band1));
        low2 += mul(f2, band2);
        band2 += mul(f2, in - low2 - mul(damping, band2));
        const i32 sung = mul(band1 * 3 + band2 * 2, sing);                // near unity at each formant
        half[i] = in + mul(sung - in, vowel);
    }
    state[0] = low1; state[1] = band1; state[2] = low2; state[3] = band2;
}

// A resonator's loop state: its low-pass, and its very slow average (DC,
// to 15 more bits).
struct Loop { i32 lp, slow; };
inline i32 lp_from(i32 damp) { return damp; }

// One sample of a resonator (resonate below). A loop resonates at DC as at
// the note, and rounding leaves a small offset there: the loop's own very
// slow average (0.2 Hz) comes off what it feeds back, too slow to touch the note.
inline void pass(Loop& at, const opxy::i16* back_at, const opxy::i16* half_at, opxy::i16* now, opxy::i16* mirror,
                 i32* out, i32 i, i32 excitation, i32 frac, i32 half_frac, i32 k, i32 loop, i32 divide) {
    const i32 a = back_at[i], b = back_at[i - 1];
    at.lp += mul(a + mul(b - a, frac) - at.lp, k);
    at.slow += at.lp - (at.slow >> 15);
    const i32 y = clip16(excitation + mul(at.lp - (at.slow >> 15), loop));
    now[i] = mirror[i] = opxy::i16(y);
    const i32 c = half_at[i], d = half_at[i - 1];
    out[i] = (y + mul(c + mul(d - c, half_frac), divide)) >> 1;
}

// One ear's resonator through a block: y = e + g LP(y a period back),
// written to its line; out = (y + split y half a period back) / 2, which keeps
// the even harmonics as split nears +1 and the odd as it nears -1. e is at
// half rate, interpolated here; write is a multiple of the block, so a block
// never wraps the line.
void resonate(opxy::i16* line, u32 write, i32 delay, i32 half, i32 g, i32 bright, i32& damp, i32& level, i32& last,
              const i32* e, i32 split, i32* out) {
    opxy::i16* now = line + line_length + write;     // this block in the line's upper copy
    opxy::i16* mirror = line + write;                // and its lower
    // Q31 fractions and gains: each product is one Blackfin instruction.
    const i32 whole = i32(u32(delay) >> 16), frac = i32(u32(delay) << 16 >> 1);
    const i32 half_whole = i32(u32(half) >> 16), half_frac = i32(u32(half) << 16 >> 1);
    const i32 k = bright << 16, loop = g << 16, divide = split << 16;
    const opxy::i16* back_at = now - whole;
    const opxy::i16* half_at = now - half_whole;
    Loop at = {lp_from(damp), level};
    i32 previous = last;
    // Two samples a pass, one of the half-rate excitation: between it and
    // the last, then it. (Signed indices: i - 1 reads before the block.)
    for (i32 j = 0; j < i32(half_frames); ++j) {
        const i32 next = e[j];
        pass(at, back_at, half_at, now, mirror, out, 2 * j, (previous + next) >> 1, frac, half_frac, k, loop, divide);
        pass(at, back_at, half_at, now, mirror, out, 2 * j + 1, next, frac, half_frac, k, loop, divide);
        previous = next;
    }
    const i32 lp = at.lp, slow = at.slow;
    damp = lp;
    level = slow;
    last = previous;
}

// One ear's head: its delay (Q16 samples), a one-pole head shadow (Q15) and
// its gain (Q12), each ramped across the block from the last, into the
// output (every other word) at half scale for the lines' 32767. in has the
// ear's last ear_tail samples before it.
void hear(const i32* from, const i32* to, i32& lp, const i32* in, i32* out) {
    i32 delay = from[0], cut = from[1], gain = from[2];
    const i32 c_step = (to[1] - cut) >> 6, g_step = (to[2] - gain) >> 6;
    i32 low = lp;
    if (!delay && !to[0]) {   // the ear nearer the source: no delay to read
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            low += mul(in[i] - low, cut << 16);
            out[2 * i] = clip16((low * gain) >> 12) * 32768;
            cut += c_step; gain += g_step;
        }
    } else {
        const i32 d_step = (to[0] - delay) >> 6;
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            const i32 at = delay < 0 ? 0 : delay;                          // the ramp may round past 0
            const i32 whole = at >> 16, frac = i32(u32(at) << 16 >> 1);
            const i32 a = in[i32(i) - whole], b = in[i32(i) - whole - 1];
            low += mul(a + mul(b - a, frac) - low, cut << 16);
            out[2 * i] = clip16((low * gain) >> 12) * 32768;
            delay += d_step; cut += c_step; gain += g_step;
        }
    }
    lp = low;
}

// Holds this voice's pool slot for the cycle, taking a free one if it has
// none; null while every slot is sounding.
Lines* hold_lines(Voice& voice) {
    if (voice.cycle == census) ++census;
    voice.cycle = census;
    if (voice.slot && slots[voice.slot - 1].owner == &voice) {
        slots[voice.slot - 1].last = census;
        return &lines[voice.slot - 1];
    }
    voice.slot = 0;
    for (u32 s = 0; s < pool_size; ++s) {
        if (slots[s].owner && census - slots[s].last < 2) continue;
        slots[s].owner = &voice;
        slots[s].last = census;
        voice.slot = s + 1;
        voice.fresh = true;
        return &lines[s];
    }
    return 0;
}

// An ear's head-shadow coefficient for a cutoff in Q16 octaves above 250 Hz.
i32 shadow_cut(i32 octaves) {
    i32 index = octaves * 16 / 3;                          // 32 steps over 6 octaves, Q16
    index = index < 0 ? 0 : index > 32 * 65536 - 1 ? 32 * 65536 - 1 : index;
    const i32 k = index >> 16, f = (index & 0xffff) >> 1;
    return shadow_table[k] + (((shadow_table[k + 1] - shadow_table[k]) * f) >> 15);
}

// Where the head puts one ear's sound: facing is +1 (Q15) when the source is
// on its side, -1 opposite; behind is how far behind (Q15, 0 in front); near
// 0..65535, and far_drop how much darker distance makes both ears (Q16
// octaves). Fills delay (Q16 samples), shadow (Q15) and gain (Q12).
void ear_for(i32 facing, i32 behind, i32 itd, u32 near, i32 far_drop, i32* out) {
    out[0] = itd;
    // The far side, behind and far all darken: from 14 kHz down by octaves.
    const i32 shade = (32768 - facing) >> 1;                                   // 0 facing .. 32767 opposite
    i32 drop = ((shade >> 1) * ((163840 + i32((near * 98304u) >> 16)) >> 4)) >> 10;    // up to 2.5 to 4 octaves
    drop += (behind * (104858 >> 2)) >> 13;                                    // 1.6 octaves behind
    drop += far_drop;
    out[1] = shadow_cut(380568 - drop);                                        // 5.8 octaves: 14 kHz
    // Level: louder near (4.2 dB from 3 m to the ear, with the darkening
    // more), and the facing ear louder (up to 12 dB between them), the two
    // ears' power kept: (1 + a f) / sqrt(1 + a^2 f^2).
    const i32 distance = 3277 + i32((near * 2048u) >> 16);                     // Q12, 0.8 .. 1.3
    const i32 spread = 3277 + i32((near * 16384u) >> 16);                      // Q15, 0.1 .. 0.6
    const i32 af = (spread * facing) >> 15;                                    // Q15, |a f| <= 0.6
    const i32 x2 = (af * af) >> 15;
    const i32 inverse = 32768 - (x2 >> 1) + ((3 * x2 * x2) >> 18);            // 1 / sqrt(1 + x^2), to 1.5%
    out[2] = (((distance * (32768 + af)) >> 15) * inverse) >> 15;
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    voice.step = ratio(sample.rate ? sample.rate : 44100, 44100);
    voice.slot = 0;
    voice.cycle = census - 1;   // not yet counted in this cycle
    start(voice);
}

// A note: its own breath, detune and place round the head, its lines cleared.
void start(Voice& voice) {
    voice.noise = mix(3 * notes_started);
    voice.random = mix(3 * notes_started + 1);
    voice.start_angle = mix(3 * notes_started++ + 2);
    voice.cents = i32(next(voice.random) % 2049) - 1024;   // +-4 cents, Q8
    voice.frame = voice.sample.frames ? next(voice.random) % voice.sample.frames : 0;
    voice.fraction = 0;
    voice.write = 0;
    voice.damp[0] = voice.damp[1] = 0;
    voice.slow[0] = voice.slow[1] = 0;
    voice.air = 0;
    voice.gust = 0;
    voice.gust_next = 1;
    voice.last_e[0] = voice.last_e[1] = 0;
    voice.cached_increment = 0;
    voice.follow = 0;
    for (u32 i = 0; i < 4; ++i) voice.formant[0][i] = voice.formant[1][i] = 0;
    voice.fundamental[0] = voice.fundamental[1] = Section();
    voice.pulse[0] = voice.pulse[1] = next(voice.random);
    voice.trade = 0;
    voice.circled = 0;
    voice.shadow[0] = voice.shadow[1] = 0;
    voice.fresh = true;
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const u32 frames = opxy::block_frames;
    const bool stereo = block.output_channels == 2;
    Lines* own = hold_lines(voice);
    if (!own) {   // every line is sounding: this note stays silent until one frees
        for (u32 i = 0; i < (stereo ? 2 * frames : frames); ++i) block.output[i] = 0;
        return;
    }
    if (voice.fresh) {
        for (u32 c = 0; c < 2; ++c) {
            for (u32 i = 0; i < 2 * line_length; ++i) own->comb[c][i] = 0;
            for (u32 i = 0; i < ear_tail; ++i) own->tail[c][i] = 0;
        }
        voice.write = 0;
    }
    const i32* p = block.parameters;
    const u32 ring = level(p[kRing]), sung = level(p[kVoice]), split = level(p[kSplit]), trade = level(p[kTrade]);
    const u32 beat = level(p[kBeat]), orbit = level(p[kOrbit]), near = level(p[kNear]), bed = level(p[kBed]);

    // The note, with this voice's own few cents, folded into the lines' reach.
    u32 increment = block.pitch[0];
    increment += u32(mul(i32(increment), voice.cents * cent_q8));
    if (!increment) increment = lowest;
    while (increment > highest) increment >>= 1;
    while (increment < lowest) increment <<= 1;
    // The period, and below the loop's response at the note, only when the
    // note or the vowel moves: each costs a long division.
    const bool same = increment == voice.cached_increment;
    const u32 period = same ? voice.cached_period : ratio(0xffffffffu, increment);   // samples, Q16

    // The vowel: between two of breath, oo, oh, ah, eh and ee.
    const u32 position = vowel_position(sung), index = position >> 16, part = position & 0xffff;
    const u32 upper = index + 1 < vowel_count ? index + 1 : index;
    const i32 bright = loop_bright[index] + (((loop_bright[upper] - loop_bright[index]) * i32(part >> 1)) >> 15);
    const i32 lp_delay = loop_lag[index] + (((loop_lag[upper] - loop_lag[index]) * i32(part >> 4)) >> 12);

    // beat: the right resonator sharp by beat Hz: its period divided by
    // 1 + beat / f0, i.e. times 1 - x + x^2 for x = beat * period / fs.
    const u32 x = (beat_mhz(beat) * (period >> 10)) / 43066u;          // Q16: mHz * samples(Q6) / (fs * 1000 / 1024)
    const u32 right_period = period - times(period, x) + times(period, times(x, x));
    const i32 delays[2] = {i32(period) - lp_delay, i32(right_period) - lp_delay};
    const i32 halves[2] = {i32(period >> 1), i32(right_period >> 1)};

    // ring: the loop's gain for a 60 dB fall over ring's time: 2^(-9.97 period / (T60 fs)).
    const u32 decay = ((period >> 8) * 58u) / ring_ms(ring);           // Q16 octaves a pass
    i32 g = i32(exp2_q16(-i32(decay > 0 ? decay : 1)) >> 1);           // Q15
    if (g > 32767) g = 32767;
    // What goes in, so the note keeps its level whatever the ring, and the
    // line never clips: the loop's gain at the note, G = g |H|, |H| the loop
    // low-pass's response there, k / sqrt(k^2 + 2 (1 - k)(1 - cos w)); breath
    // goes in by sqrt(1 - G^2), the hum's coherent pulses by (1 - G) / 2.
    const i32 wn = mul(i32(increment), pi_q29) << 2;                   // the note, radians a sample (Q31)
    if (!same || bright != voice.cached_bright) {
        const u32 k2 = u32(bright * bright);                           // Q30
        const u32 spread = 2 * u32(32768 - bright) * (u32(one_minus_cos(wn)) >> 16);   // Q30
        voice.cached_response = i32(square_root(ratio(k2 >> 2, (k2 + spread) >> 2) << 14));   // Q15
        voice.cached_increment = increment;
        voice.cached_period = period;
        voice.cached_bright = bright;
    }
    const i32 response = voice.cached_response;
    const i32 loop = (g * response) >> 15;
    const u32 lost = u32(32768 - loop);
    const i32 noise_in = i32(square_root(lost * (65536 - lost)));      // Q15
    const i32 pulse_in = i32(lost >> 1);
    Exciter ex;
    const i32 w = position > 65535 ? 32767 : i32(position >> 1);       // how sung, Q15: 0 at breath, 1 from oo on
    const i32 own_share = bed ? cosine(bed << 14) : 32767;   // bed takes over from breath and hum
    ex.breath = (((noise_in * (32767 - ((w * 22938) >> 15))) >> 15) * own_share) >> 16;   // 1 - 0.7 w, halved
    ex.hum = (((pulse_in * ((w * 27852) >> 15)) >> 15) * own_share) >> 15;             // 0.85 w
    const u32 begin = block.region.begin;
    const u32 end = block.region.end < voice.sample.frames ? block.region.end : voice.sample.frames;
    ex.bed = ex.whisper = 0;
    const bool sampled = voice.sample.data && end >= begin + 8;
    if (bed && !sampled) {
        // No sample: a whisper of its own, white breath in gusts, a syllable
        // every 60 to 250 ms at its own strength, a quarter of them silent.
        if (!(--voice.gust_next & 0xffff)) {
            const u32 r = next(voice.random);
            const u32 target = (r & 0x300) ? 11469 + ((r >> 16) * 21299 >> 16) : 0;   // 0.35 .. 1, or silence
            voice.gust_next = target << 16 | (40 + (r & 127));
        }
        voice.gust += (i32(voice.gust_next >> 16) - voice.gust) >> 3;
        ex.whisper = ((((sine(bed << 14) * noise_in) >> 15) * voice.gust) >> 15) * 3 >> 1;   // white is brighter than breath
    }
    // And the note itself comes in those gusts: up to 70% of its level rides
    // them, made up so its average holds.
    const i32 gusting = bed && !sampled ? i32((bed * 22938u) >> 16) : 0;     // Q15
    const i32 gusts = 32768 - gusting + ((gusting * voice.gust) >> 15) + (gusting >> 1);   // Q15
    if (bed && sampled) {
        const u32 volume = block.region.level >> 12;                   // Q15, unity 32768
        ex.bed = i32(((u32((sine(bed << 14) * noise_in) >> 15) * (volume < 65536 ? volume : 65535)) >> 15)) << 4;   // recordings run quieter than breath
        if (voice.frame < begin || voice.frame >= end) voice.frame = begin + (voice.frame - begin) % (end - begin);
    }
    const u32 vowel_at = index ? index : 1, vowel_to = index ? upper : 1;
    const u32 blend = index ? part >> 1 : 0;
    for (u32 k = 0; k < 2; ++k)
        ex.f[k] = formant_table[2 * vowel_at + k] + i32(((formant_table[2 * vowel_to + k] - formant_table[2 * vowel_at + k]) * i32(blend)) >> 15);
    ex.vowel = w;

    // split and trade: +-split, swinging between the ears at trade's pace.
    const i32 divide = ((i32(split >> 1)) * cosine(voice.trade)) >> 15;   // Q15: + even to the left
    voice.trade += trade_mhz(trade) * orbit_block;

    // Where the note's apparition is, and what each ear hears of it.
    const Place place_now = place(voice.start_angle, voice.circled, orbit, near);
    voice.circled += orbit_mhz(orbit) * orbit_block;
    const i32 side = sine(place_now.angle), front = cosine(place_now.angle);
    const i32 behind = front < 0 ? -front : 0;
    const i32 itd = side * 24;                                         // Q16 samples: 12 at the side
    i32 left_ear[3], right_ear[3];
    const u32 far = 65535 - place_now.near;
    const i32 far_drop = i32(((((far * far) >> 16) * far) >> 16) * 4);   // far away: 4 octaves at 3 m, under 1 nearer than 1 m
    ear_for(-side, behind, itd > 0 ? itd : 0, place_now.near, far_drop, left_ear);
    ear_for(side, behind, itd < 0 ? -itd : 0, place_now.near, far_drop, right_ear);
    if (voice.fresh) {
        voice.fresh = false;
        voice.fresh_ears = true;
        voice.fresh_power = true;
    }

    // split's top quarter takes the fundamental out of what excites the
    // resonator, so neither ear has it: a notch a band 60% of the note wide
    // (wide enough that the resonator's peak, a few cents off, still falls
    // deep in it), at the ear's own pitch, at half rate.
    const i32 depth = split > 49152 ? i32(split - 49152) * 2 : 0;    // Q15
    const u32 right_increment = increment + times(increment, x);
    Coefficients notch[2] = {{0, 0}, {0, 0}};
    if (depth) {
        const i32 wl = wn << 1, wr = (mul(i32(right_increment), pi_q29) << 2) << 1;   // radians a half-rate sample
        notch[0] = flip(wl, mul(wl, 644245094));
        notch[1] = flip(wr, mul(wr, 644245094));
    }

    // The block, a stage at a time.
    i32 common[half_frames], e[half_frames], left_ear_in[ear_tail + frames], right_ear_in[ear_tail + frames];
    i32* left = left_ear_in + ear_tail;
    i32* right = right_ear_in + ear_tail;
    for (u32 i = 0; i < ear_tail; ++i) { left_ear_in[i] = own->tail[0][i]; right_ear_in[i] = own->tail[1][i]; }
    if (ex.whisper) breathe<true>(voice, ex, begin, end, common);
    else breathe<false>(voice, ex, begin, end, common);
    // The ear with the odd harmonics (divide's sign) is the only one with a
    // fundamental to take out: the other's half-period tap leaves none.
    const u32 odd = divide >= 0 ? 1 : 0;
    excite(ex, voice.pulse[0], voice.formant[0], increment * 2, common, e);
    if (depth && odd == 0) remove_fundamental(voice.fundamental[0], notch[0], depth, e);
    resonate(own->comb[0], voice.write, delays[0], halves[0], g, bright, voice.damp[0], voice.slow[0], voice.last_e[0], e, divide, left);
    if (x || depth) {
        excite(ex, voice.pulse[1], voice.formant[1], right_increment * 2, common, e);
        if (depth && odd == 1) remove_fundamental(voice.fundamental[0], notch[1], depth, e);
    } else {   // both ears' excitation is the same: the left's serves
        voice.pulse[1] = voice.pulse[0];
        for (u32 k = 0; k < 4; ++k) voice.formant[1][k] = voice.formant[0][k];
    }
    resonate(own->comb[1], voice.write, delays[1], halves[1], g, bright, voice.damp[1], voice.slow[1], voice.last_e[1], e, -divide, right);
    voice.write = (voice.write + frames) & (line_length - 1);
    // The level follower: whatever ring, voice, pitch and bed do to the
    // resonators' level, the note comes out at Hexagon's level (within 18 dB
    // either way), falling fast and rising slowly. Distance still tells.
    u32 power = 0;
    for (u32 i = 0; i < frames; i += 4) {   // every fourth sample is plenty
        const i32 a = left[i] >> 4, b = right[i] >> 4;
        power += u32(a * a + b * b) >> 1;
    }
    power >>= 4;
    if (voice.fresh_power) voice.power = power;
    voice.fresh_power = false;
    voice.power += power > voice.power ? (power - voice.power) >> 2 : -((voice.power - power) >> 5);
    // Its gain, sqrt(target / power), every fourth block (a long division and
    // a square root), or at once while the level rises past it.
    if (!(voice.write & 255) || !voice.follow || power > voice.power) {
        // Breath peaks higher than song and sounds louder for its power: it
        // sits up to 2.6 dB lower (0.55 of the power at breath, all of it sung).
        const u32 target = (target_power >> 6) * (36045 + u32(w) * 29491 / 32767) >> 10;
        const u32 follow = square_root(ratio(target, voice.power ? voice.power : 1) << 8);   // Q12
        voice.follow = follow < 1024 ? 1024 : follow > 32767 ? 32767 : follow;
    }
    left_ear[2] = (left_ear[2] * i32(voice.follow)) >> 12;
    right_ear[2] = (right_ear[2] * i32(voice.follow)) >> 12;
    if (gusting) {
        left_ear[2] = (left_ear[2] * (gusts >> 3)) >> 12;
        right_ear[2] = (right_ear[2] * (gusts >> 3)) >> 12;
    }
    if (voice.fresh_ears) {
        voice.delay[0] = left_ear[0]; voice.cut[0] = left_ear[1]; voice.gain[0] = left_ear[2];
        voice.delay[1] = right_ear[0]; voice.cut[1] = right_ear[1]; voice.gain[1] = right_ear[2];
        voice.fresh_ears = false;
    }
    for (u32 i = 0; i < ear_tail; ++i) { own->tail[0][i] = left[frames - ear_tail + i]; own->tail[1][i] = right[frames - ear_tail + i]; }
    const i32 from_left[3] = {voice.delay[0], voice.cut[0], voice.gain[0]};
    const i32 from_right[3] = {voice.delay[1], voice.cut[1], voice.gain[1]};
    if (stereo) {
        hear(from_left, left_ear, voice.shadow[0], left, block.output);
        hear(from_right, right_ear, voice.shadow[1], right, block.output + 1);
    } else {
        // Mono: both ears heard, then summed.
        i32 both[2 * frames];
        hear(from_left, left_ear, voice.shadow[0], left, both);
        hear(from_right, right_ear, voice.shadow[1], right, both + 1);
        for (u32 i = 0; i < frames; ++i) block.output[i] = (both[2 * i] >> 1) + (both[2 * i + 1] >> 1);
    }
    voice.delay[0] = left_ear[0]; voice.cut[0] = left_ear[1]; voice.gain[0] = left_ear[2];
    voice.delay[1] = right_ear[0]; voice.cut[1] = right_ear[1]; voice.gain[1] = right_ear[2];

}
}
