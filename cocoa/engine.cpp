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
// A mode's block: decays it, or on a tap strikes it by `hit` over the
// spoon's contact (2^contact samples), and sings it gliding from its
// increment to `to`.
inline void play(i32* sum, Mode& m, u32 to, i32 loss, bool tap, i32 hit, u32 contact, u32 phase) {
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
    if (start == 0 && m.out == 0) return;
    const i32 glide = static_cast<i32>(to - from) >> 6;
    if (tap) sing(sum, m, from, glide, start, (m.out - start) >> contact, 0, 1u << contact);
    else sing(sum, m, from, glide, start, (m.out - start) >> 6, 0, opxy::block_frames);
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
__attribute__((noinline)) void finish(Voice& voice, const i32* sum, opxy::Output output) {
    using namespace sound;
    using opxy::block_frames;
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    for (unsigned i = 0; i < block_frames; ++i) {
        dc_out = sum[i] - dc_in + dc_out - (dc_out >> 10);
        dc_in = sum[i];
        const i32 hi = (1 << 27) - 1;
        const i32 c = dc_out > hi ? hi : dc_out;
        const i32 v = c < -hi ? -hi : c;
        const i32 sign = v >> 31;               // 0 or -1: |v| and the sign restored without a branch
        i32 o = ((v ^ sign) - sign) << 4;
        if (o > knee) {
            const i32 over = o - knee;
            o = over >= 536870912 ? knee + 268435456 : o - (multiply(over, over) << 1);
        }
        output[i] = (o ^ sign) - sign;
    }
    voice.dc_in = dc_in;
    voice.dc_out = dc_out;
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
        for (unsigned i = 0; i < block_frames; ++i) output[i] = 0;
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

    i32 sum[block_frames];
    for (unsigned i = 0; i < block_frames; ++i) sum[i] = 0;

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
    for (u32 k = 0; k < liquid_modes; ++k) {
        Mode& m = voice.liquid[k];
        const i32 ratio = drink::along(tables::liquid_ratio[k], fill);
        const u32 to = drink::scale(key, ratio + shift, ceiling);
        const i32 loss = static_cast<i32>((static_cast<u32>(drink::scale(static_cast<u32>(tables::liquid_decay[cup]), ratio / 2 + depth, ceiling)) * static_cast<u32>(damping)) >> 16);
        i32 struck = 0;
        const i32 level = k ? (tables::liquid_level[k] * upper) >> 15 : tables::liquid_level[0];
        if (tap) struck = strike_of(hit, level, spoon(log2_key + ratio + shift, tone), make_up);
        play(sum, m, to, loss, tap, struck, contact, tap ? next(voice.seed) : 0);
        ringing = ringing || m.out;
    }
    // The cup's wall, lowered and damped by the drink in it. There is one cup,
    // whatever the chord: only the newest note's taps ring it.
    // A hard spoon rings the wall harder: its clink 0.25 to 1.75 times.
    const i32 clink = 8192 + static_cast<i32>(((static_cast<u32>(hard) * static_cast<u32>(hard) >> 16) * 49152u) >> 16);
    const i32 lowered = drink::along(tables::cup_lowering, fill);
    const i32 damped = drink::along(tables::cup_damping, fill);
    for (u32 k = 0; k < cup_modes; ++k) {
        Mode& m = voice.cup[k];
        const i32 log2_inc = tables::cup_pitch[cup][k] + lowered;
        const u32 to = drink::exp2_int(log2_inc);
        const i32 loss = static_cast<i32>(drink::scale(static_cast<u32>(tables::cup_decay[cup][k]), damped, ceiling));
        i32 struck = 0;
        if (tap && shared.leader == owner) {
            const i32 level = (tables::cup_level[cup][k] * clink) >> 15;
            struck = strike_of(hit, level < 32767 ? level : 32767, spoon(log2_inc, corner), make_up);
        }
        play(sum, m, to, loss, tap, struck, contact, tap ? next(voice.seed) : 0);
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
    for (u32 k = 0; k < pops; ++k) {
        Pop& p = voice.pop[k];
        if (p.amp < silent) continue;
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
    finish(voice, sum, output);
    if (!step && !ringing && (!popping || (!bubbly && !voice.foam))) voice.quiet = 1;   // a single tap, rung out
}
}
