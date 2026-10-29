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

// Tunes the pipe to a pitch increment: a delay of one period. The loop's
// filter delays a sample and the allpass the fraction left, 0.5 to 1.5.
inline void tune(Voice& voice, u32 key) {
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
    voice.ap_in = voice.ap_out = voice.f1 = voice.f2 = voice.e1 = voice.e2 = voice.e3 = 0;
    voice.tuned = 0;
    voice.crack_last = voice.hiss_last = 0;
    voice.dc_in = voice.dc_out = 0;
    for (u32 i = 0; i < pipe_length; ++i) voice.pipe[i] = 0;
}

// Each note drops a new drop, which lands on the next block. A pipe still
// ringing rings on.
inline void start(Voice& voice, Shared& shared, opxy::u32) {
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
    const i32 hiss_of_jet = (jet_noise * (8192 + (p.vapour * 3 >> 2)) >> 15) * static_cast<i32>(lobes + 2) / 10;

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

    // The pipe's ring: 60 dB in 0.9 s under thin vapour, a glass's singing
    // fundamental, to 0.09 s under thick, a breathy pipe; at least 48 periods,
    // so a low note's jet need not drive it hard (which would pull it flat).
    i32 loss = physics::exp2_q16(voice.period - 793744 + 9962 + p.vapour * 68 / 10 + 16 * physics::octave);
    if (loss > 13607) loss = 13607;   // 48 periods
    const i32 loop = static_cast<i32>(physics::exp2_q16(15 * physics::octave - loss));
    // The vapour's jet blows the pipe as a flute's does: it feeds the pipe's
    // own sound back through a soft limit, so a floating drop sings a steady
    // tone at the key whose strength follows the star's. Fed back at `blowing`
    // times the loop's loss it settles at a fixed level, whatever the key or ring.
    // The pipe's loop filter: thin vapour is dark, thick bright and breathy.
    const i32 b = 8192 - (p.vapour * 7537 >> 15);
    const i32 c = full - 2 * b;
    // The loop's loss at the key: its ring's, and its filter's, 4 b sin^2(pi f / fs).
    const i32 half_turn = static_cast<i32>((static_cast<u32>(voice.tuned) >> 10) * 804u >> 15);   // sin(pi f / fs), Q15
    const i32 lost = (full - loop) + (((b * (half_turn * half_turn >> 15) >> 15) * 4) * loop >> 15);
    const i32 blowing = lost * (strength * sing >> 15) >> 13;   // Q15
    const i32 jet_k = clamp(static_cast<i32>((static_cast<u32>(voice.tuned) >> 16) * 32170u >> 12), 64, 30000);   // 2 pi 2.5 f / fs, Q15
    const i32 under8 = static_cast<i32>((static_cast<u32>(voice.tuned) >> 12) * 25736u >> 20);   // 2 pi (f / 8) / fs, Q15
    const i32 breath = 150 + (p.vapour * 1800 >> 15);    // the jet heard directly
    // The jet into the pipe through two one-pole low-passes at 2 to 16 times
    // the key (thicker vapour blows brighter), less what lies under half the
    // key: the pipe's loop passes its low end too, and an open pipe has no
    // resonance there.
    const i32 key_log = physics::log2_q16(voice.tuned) - 17 * physics::octave;
    const i32 soft = clamp(static_cast<i32>(physics::exp2_q16(key_log + 239276 + p.vapour * 6)), 64, 20000);
    const i32 under = clamp(static_cast<i32>(physics::exp2_q16(key_log + 108232)), 16, 30000);
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
    const i32 lift = pipe_lift - opened / 20 * 17 + (key < 0 ? key * 14 / 64 : 0) - ((key >> 4) * (p.vapour >> 2) >> 11) -
                     (ring + 232781) / 2 + ((key < 0 ? -key : key / 4) >> 4) * ((full - p.vapour) >> 4) / 384 - p.vapour;
    const i32 drive = clamp(static_cast<i32>(physics::exp2_q16(lift + 10 * physics::octave)), 1, 1 << 16);
    // The jet heard directly is white: above A3 it falls an octave of level
    // for each octave of key, or it would hiss over high notes.
    const i32 breath_heard = key > 0 ? static_cast<i32>(physics::exp2_q16(physics::log2_q16(static_cast<u32>(breath)) - key)) : breath;
    delay(voice, lost);
    if (voice.kick == 0xffffffffu) voice.kick = static_cast<u32>(voice.whole / 2 + 1);
    const i32 eta = voice.eta;
    // The tone as heard, Q12: its level is the jet's, the same at every key,
    // but low notes sound weaker: 0.3 octave more for each octave under A3,
    // 0.09 less for each over.
    const i32 tone_gain = static_cast<i32>(physics::exp2_q16(12 * physics::octave + tone_level -
                                                              (key > 0 ? key * 6 / 64 : key * 19 / 64)));
    // A chord's drops land and pulse together, so its pulses add up: from
    // three sounding voices on, each plays as the square root of 2 / voices.
    const u32 many = shared.sounding < 8 ? shared.sounding : 8;
    const i32 fair = static_cast<i32>(shares[many]);
    voice.share = fair < voice.share ? fair : voice.share + (fair - voice.share) / 8;   // down at once, up gently
    const i32 share = voice.share;
    const u32 mask = pipe_length - 1, whole = static_cast<u32>(voice.whole);

    u32 phase = voice.phase, vent = voice.vent, vents = voice.vents, beats = voice.beats;
    u32 noise = voice.noise, dice = voice.dice, w = voice.write;
    u32 kick = voice.kick;
    i32 settled = voice.settled, hold = voice.hold, held = voice.held, lowest = voice.lowest, jet_lp = voice.jet_lp;
    i32 puff = voice.puff, jet = voice.jet, spark = voice.spark, pulse = voice.pulse, soft_crack = voice.soft_crack, softer_crack = voice.softer_crack;
    i32 ap_in = voice.ap_in, ap_out = voice.ap_out, f1 = voice.f1, f2 = voice.f2;
    i32 crack_last = voice.crack_last, hiss_last = voice.hiss_last;
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    i32 e1 = voice.e1, e2 = voice.e2, e3 = voice.e3;
    // The low-passes' rounding leaves them a unit or two off zero, which the
    // pipe's gain would hold as an offset: once the drop is gone, let it go.
    if (!alive && magnitude(e1) < 8 && magnitude(e2) < 8 && magnitude(e3) < 8) e1 = e2 = e3 = 0;
    i32 loudest = 0;
    for (unsigned i = 0; i < block_frames; ++i) {
        const i32 r1 = static_cast<i32>(lcg(noise)) >> 16;   // Q15
        const u32 r2 = lcg(dice);
        i32 x = 0;
        i32 push = 0;
        if (kick) { --kick; push = voice.push; }   // the landing sets the pipe going
        // A beat: the first vent opens at once, the rest follow.
        const u32 before = phase;
        phase += inc;
        if (phase < before) {
            ++beats;
            if (d.hop) {
                vents = 0;
                voice.accent = 1;
            } else {
                puff = base;
                if (voice.accent) { puff = puff * 3 / 2; voice.accent = 0; }
                vents = lobes - 1;
                vent = 0;
                if (puff << 15 > jet) jet = puff << 15;
                pulse = full;
                // Each vent's tick, of either sign: a vent pushes the gap's air
                // one way or the other, so chords' beats do not pile up in sign.
                x += (r2 & 0x40000000u ? puff : -puff) * vent_tick >> 15;
            }
        } else if (vents) {
            const u32 was = vent;
            vent += (top - vents) & 1u ? short_inc : long_inc;
            if (vent < was) {
                --vents;
                const i32 a = puff * 26000 >> 15;   // 0.8 of the first, which leads the beat
                if (a << 15 > jet) jet = a << 15;
                if (25000 > pulse) pulse = 25000;
                x += (r2 & 0x40000000u ? a : -a) * vent_tick >> 15;
            }
        }
        jet -= (jet >> 15) * decay;
        pulse -= pulse * decay >> 15;
        const i32 blow = ((jet >> 15) + steady) * r1 >> 15;
        x += blow * hiss_of_jet >> 15;
        // Crackle: a burst of noise at random moments, mostly small, now and then loud.
        if (r2 < chance) {
            const i32 u = static_cast<i32>(lcg(dice) >> 17);
            const i32 a = (u * u >> 15) * loudness >> 15;
            if (a << 15 > spark) spark = a << 15;
        }
        spark -= spark >> 6;   // each burst rings about 1.5 ms
        const i32 crack = (spark >> 15) * (static_cast<i32>(r2) >> 16) >> 15;
        x += crack * into_pipe >> 15;
        e1 += ((x - e1) * soft + 16384) >> 15;   // rounded: a floor would settle on an offset
        e2 += ((e1 - e2) * soft + 16384) >> 15;
        e3 += ((e2 - e3) * under + 16384) >> 15;
        // The pipe.
        const i32 delayed = voice.pipe[(w - whole) & mask];
        const i32 ap = ((eta * (delayed - ap_out) + 16384) >> 15) + ap_in;
        ap_in = delayed;
        ap_out = ap;
        const i32 filtered = (b * (ap + f2) + c * f1 + 16384) >> 15;
        f2 = f1;
        f1 = ap;
        // The jet's soft limit: u - u^3 / 3 of the pipe's sound over 0.25,
        // above an eighth of the key, so it blows the key and not the loop's
        // slow drift.
        lowest += ((filtered * 1024 - lowest) >> 7) * under8 >> 8;
        const i32 u = clamp((filtered - (lowest >> 10)) * 4, -32767, 32767);
        // The jet answers slowly, so it blows the fundamental, not the octave.
        jet_lp += ((u - ((u * u >> 15) * u >> 15) / 3) - jet_lp) * jet_k >> 15;
        const i32 limited = jet_lp >> 2;
        const i32 y = clamp(push + ((e2 - e3) * drive >> 10) + ((loop * filtered + 16384) >> 15) + ((blowing * (16384 + (pulse >> 1)) >> 15) * limited >> 15),
                            -ceiling, ceiling);
        // Into the delay without its offset: the loop would also ring at DC,
        // and, blown, latch there.
        hold += (y - held) * 1024 - (hold >> 13);
        held = y;
        voice.pipe[w & mask] = static_cast<i16>(clamp(hold >> 10, -ceiling, ceiling));
        ++w;
        // As heard: the pipe, the crackle through a difference (its clicks are
        // bright), the hiss and a little of the jet.
        // The frying hiss: white noise low-passed at about 4 kHz, not the
        // raw top end.
        hiss_last += (r1 - hiss_last) * 15000 >> 15;
        const i32 hiss = hiss_last * frying >> 14;
        // The tone sounds while the vapour vents, at 0.18 between pulses. The
        // pipe's offset is taken out first (a long ring holds the jet's
        // rounding as one), or the gate would pulse it, alike in every voice.
        settled += y * 16 - (settled >> 10) * 16;   // y's mean over 23 ms, Q10
        const i32 tone = y - (settled >> 10);
        const i32 gated = tone * (gate_floor + ((full - gate_floor) * pulse >> 15)) >> 15;
        // The crackle's clicks, through a difference, softened twice over 3 kHz.
        soft_crack += ((crack - crack_last) - soft_crack) * 11000 >> 15;
        softer_crack += (soft_crack - softer_crack) * 11000 >> 15;
        const i32 mixed = (gated * tone_gain >> 12) + (softer_crack * crackle_heard >> 13) + hiss + (blow * breath_heard >> 15);
        crack_last = crack;
        dc_out += (mixed - dc_in) * 1024 - (dc_out >> 10);   // DC blocker, 7 Hz, its state Q25
        dc_in = mixed;
        // Q15 one is half scale. A soft knee from 0.25 rounds a beat's loudest
        // onsets off toward 0.45, under the declared half scale.
        const i32 v = clamp(dc_out >> 10, -65535, 65535), a = magnitude(v);
        i32 o = a << 15;
        if (o > 536870912) {
            const i32 over = o - 536870912;
            o = over >= 858993459 ? 966367642 : o - (opxy::q31::multiply(over, over) >> 10) * 1280;
        }
        o = static_cast<i32>((static_cast<u32>(o) >> 15) * static_cast<u32>(share));
        output[i] = v < 0 ? -o : o;
        loudest = a > loudest ? a : loudest;
    }
    voice.phase = phase;
    voice.vent = vent;
    voice.vents = vents;
    voice.beats = beats;
    voice.noise = noise;
    voice.dice = dice;
    voice.write = w;
    voice.puff = puff;
    voice.settled = settled;
    voice.kick = kick;
    voice.hold = hold;
    voice.lowest = lowest;
    voice.jet_lp = jet_lp;
    voice.held = held;
    voice.pulse = pulse;
    voice.soft_crack = soft_crack;
    voice.softer_crack = softer_crack;
    voice.jet = jet;
    voice.spark = spark;
    voice.ap_in = ap_in;
    voice.ap_out = ap_out;
    voice.f1 = f1;
    voice.f2 = f2;
    voice.crack_last = crack_last;
    voice.hiss_last = hiss_last;
    voice.dc_in = dc_in;
    voice.dc_out = dc_out;
    voice.e1 = e1;
    voice.e2 = e2;
    voice.e3 = e3;
    // Gone and rung out (under -66 dBFS as heard): clear the pipe and stop.
    // (The pipe may hold an offset of a unit or two, which the DC blocker hides.)
    if (!alive && loudest < 32 && spark < (32 << 15)) {
        voice.quiet = 1;
        if (voice.counted && shared.sounding) --shared.sounding;
        voice.counted = 0;
        for (u32 i = 0; i < pipe_length; ++i) voice.pipe[i] = 0;
        voice.ap_in = voice.ap_out = voice.f1 = voice.f2 = voice.e1 = voice.e2 = voice.e3 = 0;
        voice.jet = voice.spark = voice.pulse = voice.soft_crack = voice.softer_crack = voice.settled = voice.hold = voice.held = voice.lowest = voice.jet_lp = 0;
        voice.dc_in = voice.dc_out = voice.crack_last = voice.hiss_last = 0;
    }
}
}
