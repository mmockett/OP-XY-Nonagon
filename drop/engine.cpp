#include "core.h"
#include <opxy/q31.hpp>

// Drop: each note drops a drop of liquid on a hot skillet (leidenfrost.h).
//
// Above the liquid's Leidenfrost point it floats on its vapour as a star and
// beats: at each beat the vapour vents under its lobes, one puff a lobe a
// sixteenth of a beat apart, and blows a pipe tuned to the key, as the
// vapour gap sounds its tone inside each beat (Singla & Rivera 2020). The
// beats come at twice the star's oscillation, about 23 Hz for water. As the
// drop evaporates it steps down through fewer lobes, each step a new rhythm
// and colour, and ends as a sphere whose beats quicken into a chirp before it
// is gone. Brief contacts crackle, densest near the Leidenfrost point.
//
// Under the point the drop boils on the plate: a fierce, broadband sizzle of
// bursting bubbles, gone in a fraction of a second. On the ratchet's teeth
// a floating drop is pushed along: its beats drift and wobble, and it hops a
// tooth now and then, missing a beat.
//
// A synth voice on this SDK hears its note start but not its release, so a
// note is the drop's whole life, from the note-on: see README.md.
namespace drop {
namespace sound {
using physics::full;
const i32 beat_level = 15000;     // the first vent of a full beat, Q15
const i32 vent_tick = 6000;      // each vent's tick into the pipe, against its puff, Q15
const i32 jet_noise = 10000;      // the jet's noise into the pipe, Q15
const i32 crackle_level = 30000;  // a floating drop's contacts, Q15
const i32 boil_level = 24000;     // a boiling drop's bubbles, Q15
const i32 boil_hiss = 3000;       // its frying hiss, Q15
const i32 vapour_hiss = 220;      // the vapour's own hiss at full vapour, Q15
const i32 into_pipe = 5000;       // the crackle's share into the pipe, Q15
const i32 tone_level = 15000;    // log2 of the tone as heard at A3, Q16 (x 1.17)
const i32 crackle_heard = 32767;  // the crackle as heard, Q15
const i32 ceiling = 32767;
const i32 full_share = 32768;
const u32 shares[9] = {32768, 32768, 32768, 26755, 23170, 20724, 18919, 17515, 16384};   // sqrt(2 / voices), Q15
const i32 sing = 24576;           // the jet's feedback over the loop's loss, Q13 (3: it sings at 0.4)
const i32 kick_level = 10000;     // the landing's push into the pipe, Q15
const i32 sustain = 9800;         // the steady blowing, against a pulse, Q15 (0.3)
const i32 gate_floor = 5900;      // the tone between pulses, Q15 (0.18)
// A beat's strength by the lobes venting it, Q4: fewer vents blow harder, so
// a shrinking drop fades only 2 dB from eight lobes to two.
const i32 vents_level[physics::most_lobes + 1] = {0, 42, 42, 35, 29, 23, 20, 17, 16};
const i32 a3 = 1595990;           // log2 of A3's increment, Q16
const i32 pipe_lift = 344000;     // the pipe's input at A3 and no vapour, octaves Q16

inline u32 lcg(u32& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }
inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
// v / 3, truncating as C does, for |v| < 32768, without the Blackfin's divide call.
inline i32 third(i32 v) { return ((v * 21846) >> 16) - (v >> 31); }
// Clears the pipe's line two samples a store, not one (the line is 4-byte
// aligned: Voice holds only 4-byte fields before it).
typedef u32 __attribute__((may_alias)) pair_of_samples;
inline void clear(i16* line) {
    pair_of_samples* pairs = reinterpret_cast<pair_of_samples*>(line);
    for (u32 i = 0; i < pipe_length / 2; ++i) pairs[i] = 0;
}
// a * b for values that fit 16 bits: the Blackfin's single-cycle 16 x 16
// multiply, where GCC would otherwise emit a 32-bit one. The same product.
inline i32 mul16(i32 a, i32 b) { return static_cast<i32>(static_cast<i16>(a)) * static_cast<i16>(b); }

// A block runs in passes over block buffers, each small enough for the
// Blackfin's registers: the vapour's noise, the crackle, the beats, the jet
// into the pipe, the pipe, then what is heard. Each keeps its state in locals
// and hands it back. They are kept out of line, so each loop stays its own.
#define DROP_PASS static __attribute__((noinline))
const unsigned frames = opxy::block_frames;

// The vapour's noise, Q15.
DROP_PASS u32 noise_pass(u32 noise, i16* r1) {
    for (unsigned i = 0; i < frames; ++i) r1[i] = static_cast<i16>(static_cast<i32>(lcg(noise)) >> 16);
    return noise;
}

// The crackle: a burst of noise at random moments, mostly small, now and then
// loud. Hands on each sample's random draw (its top half, whose sign bit 14
// signs the vents' ticks) and the crackle. The spark stays under 2^30.
// `silent` is 1 when every sample's crackle was surely 0: no burst, and the
// spark already too small to be heard.
DROP_PASS u32 crackle_pass(u32 dice, i32& spark_io, u32 chance, i32 loudness, i16* draw, i16* crack, u32& silent) {
    i32 spark = spark_io;
    u32 still = spark < 32768;
    for (unsigned i = 0; i < frames; ++i) {
        const u32 r2 = lcg(dice);
        const i32 high = static_cast<i32>(r2) >> 16;
        draw[i] = static_cast<i16>(high);
        if (r2 < chance) {
            const i32 u = static_cast<i32>(lcg(dice) >> 17);
            const i32 a = mul16(mul16(u, u) >> 15, loudness) >> 15;
            if (a << 15 > spark) spark = a << 15;
            still = 0;
        }
        spark -= spark >> 6;   // each burst rings about 1.5 ms
        crack[i] = static_cast<i16>(mul16(spark >> 15, high) >> 15);
    }
    spark_io = spark;
    silent = still;
    return dice;
}

// The beats: the vents' ticks into the pipe, and the jet's and the pulses'
// envelopes. Hands on each sample's tick, the jet's envelope (Q15) and the pulses.
struct Beats {
    u32 phase, vent, vents, beats, accent;
    i32 puff, jet, pulse;
    u32 inc, short_inc, long_inc, top, lobes, hop;
    i32 base, decay;
};
DROP_PASS void beats_pass(Beats& s, const i16* draw, i16* tick_out, i32* jet_out, i16* pulse_out) {
    u32 phase = s.phase, vent = s.vent, vents = s.vents, beats = s.beats, accent = s.accent;
    i32 puff = s.puff, jet = s.jet, pulse = s.pulse;
    const u32 inc = s.inc, short_inc = s.short_inc, long_inc = s.long_inc, top = s.top, lobes = s.lobes, hop = s.hop;
    const i32 base = s.base, decay = s.decay;
    for (unsigned i = 0; i < frames; ++i) {
        i32 x = 0;
        // A beat: the first vent opens at once, the rest follow.
        const u32 before = phase;
        phase += inc;
        if (phase < before) {
            ++beats;
            if (hop) {
                vents = 0;
                accent = 1;
            } else {
                puff = base;
                if (accent) { puff = puff * 3 / 2; accent = 0; }
                vents = lobes - 1;
                vent = 0;
                if (puff << 15 > jet) jet = puff << 15;
                pulse = full;
                // Each vent's tick, of either sign: a vent pushes the gap's air
                // one way or the other, so chords' beats do not pile up in sign.
                x = (draw[i] & 0x4000 ? puff : -puff) * vent_tick >> 15;
            }
        } else if (vents) {
            const u32 was = vent;
            vent += (top - vents) & 1u ? short_inc : long_inc;
            if (vent < was) {
                --vents;
                const i32 a = puff * 26000 >> 15;   // 0.8 of the first, which leads the beat
                if (a << 15 > jet) jet = a << 15;
                if (25000 > pulse) pulse = 25000;
                x = (draw[i] & 0x4000 ? a : -a) * vent_tick >> 15;
            }
        }
        jet -= (jet >> 15) * decay;
        pulse -= mul16(pulse, decay) >> 15;
        tick_out[i] = static_cast<i16>(x);   // within +-10,900: puff is at most 1.5 x 39,400
        jet_out[i] = jet >> 15;
        pulse_out[i] = static_cast<i16>(pulse);
    }
    s.phase = phase;
    s.vent = vent;
    s.vents = vents;
    s.beats = beats;
    s.accent = accent;
    s.puff = puff;
    s.jet = jet;
    s.pulse = pulse;
}

// The jet's noise blown: the vapour's noise at the jet's envelope over its
// steady blowing. With the ticks and the crackle it excites the pipe. In
// place: the jet's envelope becomes the excitation; blow is kept for the ear.
DROP_PASS void blow_pass(const i16* r1, const i16* tick, const i16* crack, i32* jet_in, i32* blow_out, i32 steady, i32 hiss_of_jet) {
    for (unsigned i = 0; i < frames; ++i) {
        const i32 blow = (jet_in[i] + steady) * r1[i] >> 15;
        jet_in[i] = tick[i] + (blow * hiss_of_jet >> 15) + (mul16(crack[i], into_pipe) >> 15);
        blow_out[i] = blow;
    }
}

// The jet into the pipe: the jet's noise, the ticks and the crackle through
// two low-passes less its low end, at the pipe's input gain, and the landing's
// push. The low-passes run at half the rate, on each pair's mean, and are
// drawn between at the full. In place: x becomes the pipe's input. With it,
// the jet's feedback gain, which the pulses swell.
struct Jet {
    u32 kick;
    i32 push, e1, e2, e3, soft, under, drive, blowing;
};
// A one-pole's coefficient for two samples at once: 1 - (1 - k)^2, Q15.
inline i32 twice(i32 k) { return 2 * k - (k * k >> 15); }
DROP_PASS void jet_pass(Jet& s, i32* x_in, const i16* pulse, i32* gain) {
    u32 kick = s.kick;
    i32 e1 = s.e1, e2 = s.e2, e3 = s.e3;
    const i32 landing = s.push, soft = twice(s.soft), under = twice(s.under), drive = s.drive, blowing = s.blowing;
    i32 was = (e2 - e3) * drive >> 10;
    i32* xp = x_in;
    i32* gp = gain;
    const i16* pp = pulse;
    for (unsigned i = 0; i < frames / 2; ++i) {
        const i32 x = (xp[0] + xp[1]) >> 1;
        e1 += ((x - e1) * soft + 16384) >> 15;   // rounded: a floor would settle on an offset
        e2 += ((e1 - e2) * soft + 16384) >> 15;
        e3 += ((e2 - e3) * under + 16384) >> 15;
        const i32 now = (e2 - e3) * drive >> 10;
        xp[0] = (was + now) >> 1;
        xp[1] = now;
        was = now;
        gp[0] = blowing * (16384 + (pp[0] >> 1)) >> 15;
        gp[1] = blowing * (16384 + (pp[1] >> 1)) >> 15;
        xp += 2;
        gp += 2;
        pp += 2;
    }
    // The landing sets the pipe going: a push over its first samples.
    for (unsigned i = 0; kick && i < frames; ++i, --kick) x_in[i] += landing;
    s.kick = kick;
    s.e1 = e1;
    s.e2 = e2;
    s.e3 = e3;
}

// The pipe: its delay, the allpass for the fraction, the loop filter, the
// jet's soft limit and the loop's DC blocker. Hands on y, within +-ceiling.
struct Pipe {
    i16* line;
    u32 write, whole;
    i32 ap_in, f1, f2, lowest, jet_lp, hold, held;   // the allpass's last output is f1
    i32 eta, b, c, loop, under8, jet_k;
};
// One stretch of the block over which neither the delay's read nor its write
// wraps round the line: both walk it.
DROP_PASS void pipe_run(Pipe& s, const i16* from, i16* into, unsigned count, const i32* input, const i32* gain, i16* y_out) {
    i32 ap_in = s.ap_in, f1 = s.f1, f2 = s.f2, lowest = s.lowest, jet_lp = s.jet_lp, hold = s.hold, held = s.held;
    const i32 eta = s.eta, b = s.b, c = s.c, loop = s.loop, under8 = s.under8, jet_k = s.jet_k;
    for (unsigned i = 0; i < count; ++i) {
        const i32 delayed = *from++;
        const i32 ap = ((eta * (delayed - f1) + 16384) >> 15) + ap_in;   // f1 is the allpass's last output
        ap_in = delayed;
        const i32 filtered = (b * (ap + f2) + c * f1 + 16384) >> 15;
        f2 = f1;
        f1 = ap;
        // The jet's soft limit: u - u^3 / 3 of the pipe's sound over 0.25,
        // above an eighth of the key, so it blows the key and not the loop's
        // slow drift.
        lowest += ((filtered * 1024 - lowest) >> 7) * under8 >> 8;
        const i32 u = clamp((filtered - (lowest >> 10)) * 4, -32767, 32767);
        // The jet answers slowly, so it blows the fundamental, not the octave.
        const i32 cube = mul16(mul16(u, u) >> 15, u) >> 15;
        jet_lp += ((u - ((mul16(cube, 21846) >> 16) - (cube >> 31))) - jet_lp) * jet_k >> 15;
        const i32 limited = jet_lp >> 2;
        const i32 y = clamp(input[i] + ((loop * filtered + 16384) >> 15) + (gain[i] * limited >> 15), -ceiling, ceiling);
        // Into the delay without its offset: the loop would also ring at DC,
        // and, blown, latch there.
        hold += (y - held) * 1024 - (hold >> 13);
        held = y;
        *into++ = static_cast<i16>(clamp(hold >> 10, -ceiling, ceiling));
        y_out[i] = static_cast<i16>(y);
    }
    s.ap_in = ap_in;
    s.f1 = f1;
    s.f2 = f2;
    s.lowest = lowest;
    s.jet_lp = jet_lp;
    s.hold = hold;
    s.held = held;
}
inline void pipe_pass(Pipe& s, const i32* input, const i32* gain, i16* y_out) {
    const u32 mask = pipe_length - 1;
    for (unsigned done = 0; done < frames;) {
        const u32 from = (s.write - s.whole) & mask, into = s.write & mask;
        unsigned count = frames - done;
        if (count > pipe_length - from) count = pipe_length - from;
        if (count > pipe_length - into) count = pipe_length - into;
        pipe_run(s, s.line + from, s.line + into, count, input + done, gain + done, y_out + done);
        done += count;
        s.write += count;
    }
}

// What is heard beside the tone: the frying hiss (white noise low-passed at
// about 4 kHz, not the raw top end), the crackle's clicks through a difference
// (they are bright), softened twice over 3 kHz, and a little of the jet. In
// place: blow becomes this.
struct Side {
    i32 hiss_last, soft_crack, softer_crack, crack_last;
    i32 frying, breath_heard;
};
DROP_PASS void side_pass(Side& s, const i16* r1, const i16* crack, i32* blow_in) {
    i32 hiss_last = s.hiss_last, soft_crack = s.soft_crack, softer_crack = s.softer_crack, crack_last = s.crack_last;
    const i32 frying = s.frying, breath_heard = s.breath_heard;
    for (unsigned i = 0; i < frames; ++i) {
        hiss_last += (r1[i] - hiss_last) * 15000 >> 15;
        const i32 hiss = mul16(hiss_last, frying) >> 14;
        const i32 c = crack[i];
        soft_crack += ((c - crack_last) - soft_crack) * 11000 >> 15;
        softer_crack += (soft_crack - softer_crack) * 11000 >> 15;
        crack_last = c;
        blow_in[i] = (softer_crack * crackle_heard >> 13) + hiss + (blow_in[i] * breath_heard >> 15);
    }
    s.hiss_last = hiss_last;
    s.soft_crack = soft_crack;
    s.softer_crack = softer_crack;
    s.crack_last = crack_last;
}
// The same with the crackle silent and its low-passes settled: their sum is
// one value the whole block.
DROP_PASS i32 hiss_pass(i32 hiss_last, const i16* r1, i32* blow_in, i32 frying, i32 breath_heard, i32 crackled) {
    for (unsigned i = 0; i < frames; ++i) {
        hiss_last += (r1[i] - hiss_last) * 15000 >> 15;
        blow_in[i] = crackled + (mul16(hiss_last, frying) >> 14) + (blow_in[i] * breath_heard >> 15);
    }
    return hiss_last;
}

// The tone, gated by the pulses, with the rest, through the output's DC
// blocker. In place: side becomes the blocked sum, Q15.
struct Heard {
    i32 settled, dc_in, dc_out, tone_gain;
};
DROP_PASS void heard_pass(Heard& s, const i16* y, const i16* pulse, i32* side) {
    i32 settled = s.settled, dc_in = s.dc_in, dc_out = s.dc_out;
    const i32 tone_gain = s.tone_gain;
    for (unsigned i = 0; i < frames; ++i) {
        // The tone sounds while the vapour vents, at 0.18 between pulses. The
        // pipe's offset is taken out first (a long ring holds the jet's
        // rounding as one), or the gate would pulse it, alike in every voice.
        const i32 yi = y[i];
        settled += yi * 16 - (settled >> 10) * 16;   // y's mean over 23 ms, Q10
        const i32 tone = yi - (settled >> 10);
        const i32 gated = tone * (gate_floor + (mul16(full - gate_floor, pulse[i]) >> 15)) >> 15;
        const i32 mixed = (gated * tone_gain >> 12) + side[i];
        dc_out += (mixed - dc_in) * 1024 - (dc_out >> 10);   // DC blocker, 7 Hz, its state Q25
        dc_in = mixed;
        side[i] = clamp(dc_out >> 10, -65535, 65535);
    }
    s.settled = settled;
    s.dc_in = dc_in;
    s.dc_out = dc_out;
}

// Q15 one is half scale. A soft knee from 0.25 rounds a beat's loudest
// onsets off toward 0.45; then the level is lifted 1.7 dB to the suites'
// shared loudness, held within the declared half scale (only the knee's flat
// top, past 0.41, reaches it). Returns the loudest sample's magnitude.
DROP_PASS i32 knee_pass(const i32* heard, opxy::Output output, i32 share) {
    i32 loudest = 0;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 v = heard[i], a = magnitude(v);
        i32 o = a << 15;
        if (o > 536870912) {
            const i32 over = o - 536870912;
            o = over >= 858993459 ? 966367642 : o - (opxy::q31::multiply(over, over) >> 10) * 1280;
        }
        o += opxy::q31::multiply(o, 464255901);   // x 1.216
        o = o > 1073741824 ? 1073741824 : o;
        o = static_cast<i32>((static_cast<u32>(o) >> 15) * static_cast<u32>(share));
        output[i] = v < 0 ? -o : o;
        loudest = a > loudest ? a : loudest;
    }
    return loudest;
}

// Tunes the pipe to a pitch increment: a delay of one period. The loop's
// filter delays a sample and the allpass the fraction left, 0.5 to 1.5.
DROP_PASS void tune(Voice& voice, u32 key) {
    if (!key) key = 1;
    i32 period = static_cast<i32>(physics::exp2_q16(48 * physics::octave - physics::log2_q16(key)));   // Q16 samples
    while (period > 2040 * 65536) period >>= 1;   // an octave up
    if (period < 4 * 65536) period = 4 * 65536;
    // The loop's DC blocker (0.86 Hz) leads the phase a little: fs fc / (2 pi f^2)
    // samples, which the delay makes up, 2.3 at A1.
    const i32 samples = period >> 16;
    period += samples * samples * 208 >> 10;
    voice.length = period;
    voice.period = physics::log2_q16(static_cast<u32>(period)) - 16 * physics::octave;
    voice.tuned = key;
    voice.figured = -1;
}
// Sets the delay each block: the period less what the jet's answer lags,
// which grows with the loop's loss (lost, Q15): its slow, low-passed push
// makes up the loss late, about lost P / (8 pi) samples.
inline void delay(Voice& voice, i32 lost) {
    const i32 period = voice.length - (voice.length >> 8) * lost / 3216;
    voice.whole = (period - 98304) >> 16;
    const i32 fraction = period - 65536 - (voice.whole << 16);
    voice.eta = (65536 - fraction) * 32768 / (65536 + fraction);   // (1 - f) / (1 + f), Q15
}
// Works out what the key and the vapour set, which no block's life changes:
// the pipe's ring, its filter, the delay and the gains in and out of it.
DROP_PASS void figure(Voice& voice, i32 vapour) {
    // The pipe's ring: 60 dB in 0.9 s under thin vapour, a glass's singing
    // fundamental, to 0.09 s under thick, a breathy pipe; at least 48 periods,
    // so a low note's jet need not drive it hard (which would pull it flat).
    i32 loss = physics::exp2_q16(voice.period - 793744 + 9962 + vapour * 68 / 10 + 16 * physics::octave);
    if (loss > 13607) loss = 13607;   // 48 periods
    const i32 loop = static_cast<i32>(physics::exp2_q16(15 * physics::octave - loss));
    // The vapour's jet blows the pipe as a flute's does: it feeds the pipe's
    // own sound back through a soft limit, so a floating drop sings a steady
    // tone at the key whose strength follows the star's.
    // The pipe's loop filter: thin vapour is dark, thick bright and breathy.
    const i32 b = 8192 - (vapour * 7537 >> 15);
    voice.b = b;
    voice.c = full - 2 * b;
    voice.loop = loop;
    // The loop's loss at the key: its ring's, and its filter's, 4 b sin^2(pi f / fs).
    const i32 half_turn = static_cast<i32>((static_cast<u32>(voice.tuned) >> 10) * 804u >> 15);   // sin(pi f / fs), Q15
    const i32 lost = (full - loop) + (((b * (half_turn * half_turn >> 15) >> 15) * 4) * loop >> 15);
    voice.lost = lost;
    voice.jet_k = clamp(static_cast<i32>((static_cast<u32>(voice.tuned) >> 16) * 32170u >> 12), 64, 30000);   // 2 pi 2.5 f / fs, Q15
    voice.under8 = static_cast<i32>((static_cast<u32>(voice.tuned) >> 12) * 25736u >> 20);   // 2 pi (f / 8) / fs, Q15
    const i32 breath = 150 + (vapour * 1800 >> 15);    // the jet heard directly
    // The jet into the pipe through two one-pole low-passes at 2 to 16 times
    // the key (thicker vapour blows brighter), less what lies under half the
    // key: the pipe's loop passes its low end too, and an open pipe has no
    // resonance there.
    const i32 key_log = physics::log2_q16(voice.tuned) - 17 * physics::octave;
    const i32 soft = clamp(static_cast<i32>(physics::exp2_q16(key_log + 239276 + vapour * 6)), 64, 20000);
    voice.soft = soft;
    voice.under = clamp(static_cast<i32>(physics::exp2_q16(key_log + 108232)), 16, 30000);
    // The pipe's input gain, Q10: the low-passes pass more of the jet as they
    // open, so it falls 0.85 of an octave for each octave they open over A3's
    // with no vapour; under A3 the loop rings 8 periods, so a little less.
    const i32 key = physics::log2_q16(voice.tuned) - a3;
    const i32 opened = physics::log2_q16(static_cast<u32>(soft)) - (a3 - 17 * physics::octave + 239276);
    // Thick vapour's bright loop rings high notes longer: a quarter octave
    // less an octave of key at full vapour.
    // A longer ring holds more of the jet: less input as the square root of
    // the ring's length, against thick vapour's 0.09 s.
    const i32 ring = voice.period - 793744 - (physics::log2_q16(static_cast<u32>(loss)) - 16 * physics::octave);   // log2 T60, s Q16
    const i32 lift = pipe_lift - opened / 20 * 17 + (key < 0 ? key * 14 / 64 : 0) - ((key >> 4) * (vapour >> 2) >> 11) -
                     (ring + 232781) / 2 + ((key < 0 ? -key : key / 4) >> 4) * ((full - vapour) >> 4) / 384 - vapour;
    voice.drive = clamp(static_cast<i32>(physics::exp2_q16(lift + 10 * physics::octave)), 1, 1 << 16);
    // The jet heard directly is white: above A3 it falls an octave of level
    // for each octave of key, or it would hiss over high notes.
    voice.breath_heard = key > 0 ? static_cast<i32>(physics::exp2_q16(physics::log2_q16(static_cast<u32>(breath)) - key)) : breath;
    delay(voice, lost);
    // The tone as heard, Q12: its level is the jet's, the same at every key,
    // but low notes sound weaker: 0.3 octave more for each octave under A3,
    // 0.09 less for each over.
    voice.tone_gain = static_cast<i32>(physics::exp2_q16(12 * physics::octave + tone_level -
                                                          (key > 0 ? key * 6 / 64 : key * 19 / 64)));
    voice.figured = vapour;
}
}

inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    voice.counted = 0;
    voice.share = sound::full_share;
    voice.drop.lobes = 0;
    voice.drop.life = 0;
    voice.fresh = 0;
    voice.quiet = 1;
    voice.phase = voice.vent = voice.vents = voice.beats = voice.accent = 0;
    voice.kick = 0;
    voice.puff = voice.jet = voice.spark = voice.pulse = voice.soft_crack = voice.softer_crack = voice.settled = voice.hold = voice.held = voice.lowest = voice.jet_lp = 0;
    voice.splash = 0;
    voice.noise = 0x2545f491u + owner * 0x9e3779b9u;
    voice.dice = 0x6c8e9cf5u ^ owner * 0x85ebca6bu;
    voice.write = 0;
    voice.ap_in = voice.stale = voice.f1 = voice.f2 = voice.e1 = voice.e2 = voice.e3 = 0;
    voice.tuned = 0;
    voice.figured = -1;
    voice.crack_last = voice.hiss_last = 0;
    voice.dc_in = voice.dc_out = 0;
    sound::clear(voice.pipe);
}

// Each note drops a new drop, which lands on the next block. A pipe still
// ringing rings on.
inline void start(Voice& voice, Shared& shared, opxy::u32) {
    // A drop dropped into a voice whose last drop is gone and rung out: its
    // pipe is cleared now, not in the block it went quiet.
    if (voice.stale) {
        sound::clear(voice.pipe);
        voice.stale = 0;
    }
    voice.fresh = 1;
    voice.quiet = 0;
    if (!voice.counted) { voice.counted = 1; ++shared.sounding; }
}

inline void destroy(Voice& voice, Shared& shared, opxy::u32) {
    if (voice.counted && shared.sounding) --shared.sounding;
    voice.counted = 0;
}

inline void render(Voice& voice, Shared& shared, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace sound;
    using opxy::block_frames;
    if (voice.quiet) {
        for (unsigned i = 0; i < block_frames; ++i) output[i] = 0;
        return;
    }
    const physics::Plate p = physics::plate(parameters);
    physics::Drop& d = voice.drop;

    // The drop lands, or lives another block.
    u32 events = 0;
    if (voice.fresh) {
        voice.fresh = 0;
        physics::land(d, p);
        voice.splash = 8;   // 12 ms of spray
        voice.kick = 0xffffffffu;   // and a push into the pipe, half a period long, once it is tuned
        voice.push = (lcg(voice.noise) & 0x10000u ? kick_level : -kick_level) * p.floating >> 15;   // of either sign
        voice.phase = 0xffffffffu - 4096u;  // the first beat at once: a chord's drops beat together
        voice.vents = 0;
        voice.accent = 0;
        voice.noise ^= p.seed * 0x9e3779b9u;
        const i32 thump = 5000 + (p.floating >> 4);
        voice.spark = voice.spark > thump << 15 ? voice.spark : thump << 15;
    } else {
        events = physics::step(d, p);
    }
    const bool alive = d.lobes != 0;
    // A lobe given up, a hop landing and the drop's end each crackle once.
    i32 click = 0;
    if (events & physics::stepped) click = 9000;
    if (events & physics::landed) { click = 13000; voice.accent = 1; }
    if (events & physics::gone) click = 5000;
    if (click << 15 > voice.spark) voice.spark = click << 15;

    if (pitch[0] != voice.tuned) tune(voice, pitch[0]);

    // The beats: their rate, the vents and the jet's decay, a sixth of a
    // beat. The vapour escapes under each lobe in turn, a sixteenth of a beat
    // apart, so a beat lasts as many sixteenths as the star has lobes: an
    // eight-lobed star's beats are long and breathy, a two-lobed one's short
    // pecks. Fewer vents blow a little harder, and less of the jet's noise.
    const i32 rate = alive ? physics::beat_rate(d, p) : 0;
    const u32 inc = alive ? physics::exp2_q16(rate + tables::increment) : 0;
    const u32 lobes = d.lobes;
    // Slow beats (4 Hz and under) spread their vents evenly over the whole
    // beat, so each beat is a bar of as many pulses as lobes, and every lobe
    // given up slows the pulse a step: 8, 7, ... 2 to the beat. Fast ones
    // (16 Hz and over, the papers' drops) crowd them into lobes/16 of it.
    const i32 fast = clamp((rate - 2 * physics::octave) / 2, 0, physics::octave);                // Q16
    const i32 crowd = ((physics::log2_q16(lobes ? lobes : 1) - 4 * physics::octave) >> 4) * (fast >> 4) >> 8;   // log2 of the share, <= 0
    const i32 vent_log = physics::log2_q16(inc ? inc : 1) + physics::log2_q16(lobes ? lobes : 1) - crowd;
    // The ratchet swings them: the gap after the beat and every other vent
    // lasts 1 + swing of the spacing, the others 1 - swing.
    const u32 long_inc = alive ? physics::exp2_q16(vent_log + 15 * physics::octave - physics::log2_q16(static_cast<u32>(32768 + p.swing))) : 0;
    const u32 short_inc = alive ? physics::exp2_q16(vent_log + 15 * physics::octave - physics::log2_q16(static_cast<u32>(32768 - p.swing))) : 0;
    const u32 top = lobes ? lobes - 1 : 0;
    // Each pulse dies in a third of its spacing at slow beats (so they can be
    // counted) and in 2.7 at fast ones (the long, breathy beats of a star).
    const i32 decay = alive ? clamp(static_cast<i32>(physics::exp2_q16(vent_log - 17 * physics::octave + 104858 - (fast * 197 >> 6))), 1, 12000)
                            : 2000;   // Q15 a sample
    const i32 strength = alive && !d.hop ? (d.star * p.floating >> 15) : 0;
    // Fast beats' fewer vents blow harder (their beats are shorter); slow
    // beats' pulses are each as strong, however many.
    const i32 harder = 16 + ((vents_level[lobes] - 16) * (fast >> 4) >> 12);   // Q4
    const i32 base = (strength * beat_level >> 15) * harder >> 4;
    const i32 steady = base * sustain >> 15;   // the vapour's steady blowing under the pulses
    // Thin vapour blows a quarter of the noise thick vapour does: a purer tone.
    // ... / 10, exactly, without the Blackfin's divide call: the product is
    // 0 to 100,000, and x / 10 is (x / 2) / 5, which (y * 52429) >> 18 gives
    // exactly for y under 2^18.
    const i32 hiss_of_jet = static_cast<i32>((static_cast<u32>((jet_noise * (8192 + (p.vapour * 3 >> 2)) >> 15) * static_cast<i32>(lobes + 2)) >> 1) * 52429u >> 18);

    // Crackle: floating, sparse contacts, densest near the point (an octave
    // fewer every 12 degrees over it); boiling, thousands of bubbles a second.
    const i32 contacts = physics::contacts(p);   // a second
    i32 boil = 0;   // the boiling's envelope, Q15: the square root of the life left
    if (alive && p.floating < full)
        boil = static_cast<i32>(physics::exp2_q16((physics::log2_q16(static_cast<u32>(d.life)) - 30 * physics::octave) / 2 + 15 * physics::octave));
    const i32 sinking = full - p.floating;
    const i32 bubbles = (1500 + (2500 * p.crackle >> 15)) * (boil * sinking >> 15) >> 15;
    i32 per_second = alive ? (contacts * p.floating >> 15) + bubbles : 0;
    if (voice.splash) { --voice.splash; per_second += 2500; }
    const u32 chance = static_cast<u32>(per_second) * 97391u;   // a sample, Q32
    // The contacts grow louder as well as denser with crackle: half as loud at a quarter.
    const i32 contact_level = crackle_level * (12000 + (p.crackle * 20767 >> 15)) >> 15;
    const i32 loudness = ((contact_level * p.floating >> 15) + (boil_level * (boil * sinking >> 15) >> 15));
    const i32 frying = (boil_hiss * (boil * sinking >> 15) >> 15) +
                       (alive ? vapour_hiss * (p.vapour * p.floating >> 15) >> 15 : 0);

    // What the key and the vapour set: unchanged until either moves.
    if (p.vapour != voice.figured) figure(voice, p.vapour);
    const i32 loop = voice.loop, b = voice.b, c = voice.c;
    // Fed back at `blowing` times the loop's loss the jet settles at a fixed level, whatever the key or ring.
    const i32 blowing = voice.lost * (strength * sing >> 15) >> 13;   // Q15
    const i32 breath_heard = voice.breath_heard, tone_gain = voice.tone_gain;
    if (voice.kick == 0xffffffffu) voice.kick = static_cast<u32>(voice.whole / 2 + 1);
    const i32 eta = voice.eta;
    // A chord's drops land and pulse together, so its pulses add up: from
    // three sounding voices on, each plays as the square root of 2 / voices.
    const u32 many = shared.sounding < 8 ? shared.sounding : 8;
    const i32 fair = static_cast<i32>(shares[many]);
    voice.share = fair < voice.share ? fair : voice.share + (fair - voice.share) / 8;   // down at once, up gently
    const i32 share = voice.share;
    // The block, in passes (see DROP_PASS).
    i16 r1[frames], draw[frames], crack[frames], pulse[frames], y[frames];
    i32 x[frames], gain[frames], side[frames];
    voice.noise = noise_pass(voice.noise, r1);
    u32 crackle_silent = 0;
    voice.dice = crackle_pass(voice.dice, voice.spark, chance, loudness, draw, crack, crackle_silent);
    Beats beat_state = {voice.phase, voice.vent, voice.vents, voice.beats, voice.accent,
                        voice.puff, voice.jet, voice.pulse,
                        inc, short_inc, long_inc, top, lobes, d.hop,
                        base, decay};
    beats_pass(beat_state, draw, y, x, pulse);   // y holds the ticks until the pipe
    voice.phase = beat_state.phase;
    voice.vent = beat_state.vent;
    voice.vents = beat_state.vents;
    voice.beats = beat_state.beats;
    voice.accent = beat_state.accent;
    voice.puff = beat_state.puff;
    voice.jet = beat_state.jet;
    voice.pulse = beat_state.pulse;
    blow_pass(r1, y, crack, x, side, steady, hiss_of_jet);
    // The low-passes' rounding leaves them a unit or two off zero, which the
    // pipe's gain would hold as an offset: once the drop is gone, let it go.
    if (!alive && magnitude(voice.e1) < 8 && magnitude(voice.e2) < 8 && magnitude(voice.e3) < 8) voice.e1 = voice.e2 = voice.e3 = 0;
    Jet jet_state = {voice.kick, voice.push, voice.e1, voice.e2, voice.e3, voice.soft, voice.under, voice.drive, blowing};
    jet_pass(jet_state, x, pulse, gain);
    voice.kick = jet_state.kick;
    voice.e1 = jet_state.e1;
    voice.e2 = jet_state.e2;
    voice.e3 = jet_state.e3;
    Pipe pipe_state = {voice.pipe, voice.write, static_cast<u32>(voice.whole),
                       voice.ap_in, voice.f1, voice.f2, voice.lowest, voice.jet_lp, voice.hold, voice.held,
                       eta, b, c, loop, voice.under8, voice.jet_k};
    pipe_pass(pipe_state, x, gain, y);
    voice.write = pipe_state.write;
    voice.ap_in = pipe_state.ap_in;
    voice.f1 = pipe_state.f1;
    voice.f2 = pipe_state.f2;
    voice.lowest = pipe_state.lowest;
    voice.jet_lp = pipe_state.jet_lp;
    voice.hold = pipe_state.hold;
    voice.held = pipe_state.held;
    // A silent crackle leaves its low-passes where they are once they have
    // settled (a unit or two under zero, by their rounding).
    const i32 soft_crack = voice.soft_crack, softer_crack = voice.softer_crack;
    if (crackle_silent && !voice.crack_last && !((-soft_crack * 11000) >> 15) && !(((soft_crack - softer_crack) * 11000) >> 15)) {
        voice.hiss_last = hiss_pass(voice.hiss_last, r1, side, frying, breath_heard, softer_crack * crackle_heard >> 13);
    } else {
        Side side_state = {voice.hiss_last, soft_crack, softer_crack, voice.crack_last, frying, breath_heard};
        side_pass(side_state, r1, crack, side);
        voice.hiss_last = side_state.hiss_last;
        voice.soft_crack = side_state.soft_crack;
        voice.softer_crack = side_state.softer_crack;
        voice.crack_last = side_state.crack_last;
    }
    Heard heard_state = {voice.settled, voice.dc_in, voice.dc_out, tone_gain};
    heard_pass(heard_state, y, pulse, side);
    voice.settled = heard_state.settled;
    voice.dc_in = heard_state.dc_in;
    voice.dc_out = heard_state.dc_out;
    const i32 loudest = knee_pass(side, output, share);
    const i32 spark = voice.spark;
    // Gone and rung out (under -66 dBFS as heard): stop. The pipe may hold an
    // offset of a unit or two, which the DC blocker hides; it is cleared only
    // if a note starts in this voice again (start), since a voice given to a
    // new note starts zeroed.
    if (!alive && loudest < 32 && spark < (32 << 15)) {
        voice.quiet = 1;
        if (voice.counted && shared.sounding) --shared.sounding;
        voice.counted = 0;
        voice.stale = 1;
        voice.ap_in = voice.f1 = voice.f2 = voice.e1 = voice.e2 = voice.e3 = 0;
        voice.jet = voice.spark = voice.pulse = voice.soft_crack = voice.softer_crack = voice.settled = voice.hold = voice.held = voice.lowest = voice.jet_lp = 0;
        voice.dc_in = voice.dc_out = voice.crack_last = voice.hiss_last = 0;
    }
}
}
