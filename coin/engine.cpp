#include "core.h"
#include <opxy/q31.hpp>

// Coin: each note spins one to four Euler's disks on a base. The contact
// point's rolling strikes form a pulse train that rings the disk (its ring is
// the key) and the base's resonances, with rolling noise and slip. The disk
// settles from the moment it is thrown: its contact rate rises as
// (t0 - t)^(-n) (spin.h), through the ring and on to twice it, where the disk
// leaves the surface, slaps down flat and the whirr stops. Spin at its top
// ("hold") keeps the disks spinning, re-spun by hand at a slowly wobbling
// rate; turning spin down lets them go.
//
// A synth voice on this SDK hears its note start but not its release, so the
// spin cannot start settling at the note-off: see README.md.
//
// Each disk rings its own first mode, so many's disks sound as a chorus; the
// second and third modes (at about 1.73 and 2.33 times) are shared by the
// note's disks, tuned to the first, and struck by all of them. The first
// disk's ring has a partner a hair apart (no disk is quite round), and as the
// disk slowly turns, its strikes move between the pair: they beat, slowly and
// unevenly. (Several disks beat among themselves, so then it rests.) Once it lies flat, the disk rings on at its note for as long as
// ring sets.
namespace coin {
namespace sound {
using opxy::q31::multiply;
const i32 strike = 60000000;           // a strike at A4 and size 1:8 (55 Hz held), Q31 (0.028)
const i32 reference = 1464918;         // log2 of A4 as an increment, less 3 octaves, Q16
// Each disk material's strikes, Q15 x 4, so a short-ringing disk is as loud
// (wood's ring also has 1.75 times steel's level, tools/make_tables.py).
const i32 material_level[4] = {32768, 44564, 35389, 60000};
// One strike rings the disk at the same level at any key, so the throw and the
// slap that ends the whirr are fixed in size, not compensated as the whirr is.
const i32 throw_strike = 100000000;    // Q31 (0.047)
const i32 slap = 80000000;
const i32 limit = 1932735283;          // the fastest contact: 0.45 cycles a sample
const i32 ceiling = 1 << 29;           // a resonance's state stays within +-0.25
const i32 rung_out = 1 << 17;          // below this (-84 dB) a flat disk is silent
const u32 sway_step = 4362812;         // the hand's wobble, 0.7 Hz, Q32 cycles a block
const i32 share[spin::most] = {32767, 23593, 19661, 17039};   // each disk's level for the count, Q15
// The bases: the rolling noise's low-pass and its level (Q31).
const i32 rough_pole[3] = {322122547, 171798692, 536870912};
const i32 rough_level[3] = {12000000, 30000000, 22000000};   // of 2^(L/2) / 64 a disk
// How much each base damps a rolling disk through the contact, Q4: glass
// little, wood two and a half times as much, stone 1.3 times.
const i32 base_damping[3] = {16, 40, 21};
// Air: the breath pushed out from under the disk, its level (Q31, of 2^L / 256
// a disk) and the low-pass it opens the rolling noise to.
const i32 breath_level = 288000000;
const i32 breath_pole = 751619276;     // 0.35
const i32 warble_most = 3932;          // wobble's swing of the contact rate, at most 0.06 octave, Q16
const u32 eighth_most = 241591910u;    // 0.45 of the sample rate, as an eighth of an increment

// sin(pi x) for x = position / 2^31 in [0, 0.5], Q31.
inline i32 sine_pi(u32 position) {
    const i32 x = static_cast<i32>(position);
    const i32 z = multiply(x, x);
    i32 p = tables::sine_series[5];
    for (int k = 4; k >= 0; --k) p = tables::sine_series[k] + multiply(p, z);
    const i32 s = multiply(p, x);   // Q28
    return s >= (1 << 28) ? opxy::q31::maximum : s << 3;
}
// A parabolic sine of a phase (Q32 cycles), Q15.
inline i32 wave(u32 phase) {
    const i32 x = static_cast<i32>(phase) >> 16;          // -32768..32767
    const i32 a = x < 0 ? -x : x;
    return (4 * x * (32768 - a)) >> 15;
}
// v x 2^(e / 65536) for e from -30 to 3 octaves, held under `limit`.
inline i32 scale(u32 v, i32 e) {
    if (e < -30 * spin::octave) return 0;
    const i32 k = spin::whole(e) + 2;
    const i32 m = multiply(static_cast<i32>(v >> 1), static_cast<i32>(spin::exp2_mantissa(e)));   // v x 2^frac / 4
    if (k <= 0) return m >> -k;
    return m > (limit >> k) ? limit : m << k;
}
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }

// A mode's increment over 8: `ratio` (/4, Q31) of a ring's.
inline u32 eighth_of(u32 ring, i32 ratio) { return static_cast<u32>(multiply(static_cast<i32>(ring >> 1), ratio)); }
// Tunes a resonance to an increment (over 8) at a level (Q31), keeping its
// cos w for damp(). Modes at or above 0.45 of the sample rate are silent.
inline void tune(Mode& mode, i32& cosine, u32 eighth, i32 level) {
    if (eighth >= eighth_most) {
        mode.gain = 0;
        cosine = 0;
        return;
    }
    const i32 half = sine_pi(eighth << 2);      // sin(w / 2)
    const i32 square = multiply(half, half);
    cosine = opxy::q31::maximum - square - square;   // 1 - 2 sin^2(w / 2)
    mode.gain = multiply(level, sine_pi(eighth << 3));
}
// Damps a resonance by its loss a sample (1 - r^2, Q31, at most 1/32): r is
// about 1 - loss / 2.
inline void damp(Mode& mode, i32 cosine, i32 loss) {
    if (loss > 1 << 26) loss = 1 << 26;
    mode.c2 = opxy::q31::maximum - loss;
    mode.c1 = multiply(opxy::q31::maximum - (loss >> 1), cosine);
}
// A disk's block, as its loop reads it.
struct Lane {
    Disk* disk;
    i32 inc, glide;       // the contact's increment, and its change a sample
    i32 hit, bounce;      // a strike, and the bounce wobble adds half a turn later
    i32 slip;             // the slip noise into the ring, Q31
    u32 cut_at;           // where its strikes end: the sample it leaves the surface at, 0 lying flat
    i32 slap;             // the strike as it falls flat, or 0
    u32 shift, reciprocal;   // to find where a strike falls between samples
};
// One resonance's next sample from its last two, held within the ceiling:
// three Q31 multiplies, one instruction each on the Blackfin.
inline i32 next(i32 c1, i32 c2, i32 g, i32 y1, i32 y2, i32 in) {
    return clamp(multiply(c1, y1) * 2 - multiply(c2, y2) + multiply(g, in), -ceiling, ceiling);
}
// Rolls one disk through a block: its contact strikes, each placed between
// two samples, until it leaves the surface; then the slap, and its ring alone.
// Its ring adds a quarter of itself to sum; its strikes add to struck. With
// Slip, noise also drives the ring. One disk at a time keeps the Blackfin's
// few registers enough for the loop.
template <bool Slip>
inline void roll(const Lane& lane, i32* sum, i32* struck, const i32* noise) {
    Disk& d = *lane.disk;
    Mode& m = d.ring;
    const i32 c1 = m.c1, c2 = m.c2, g = m.gain, slip = lane.slip >> 15;
    const i32 hit = lane.hit, bounce = lane.bounce, glide = lane.glide;
    const u32 shift = lane.shift, reciprocal = lane.reciprocal, cut = lane.cut_at;
    i32 y1 = m.y1, y2 = m.y2, carry = d.carry, inc = lane.inc;
    u32 phase = d.phase, strikes = d.strikes;
    // The strike, if one falls in sample i: where it falls between samples.
    struct Contact {
        u32& phase; u32& strikes; i32& carry; i32& inc;
        i32 glide, hit, bounce;
        u32 shift, reciprocal;
        i32 at(unsigned i, i32* struck) {
            i32 x = carry;
            carry = 0;
            const u32 before = phase;
            phase += static_cast<u32>(inc);
            inc += glide;
            if ((before ^ phase) & 0x80000000u) {
                const u32 over = phase & 0x7fffffffu;
                const bool wrapped = !(phase & 0x80000000u);
                const i32 amount = wrapped ? hit : bounce;
                strikes += wrapped;
                u32 f = over > (0xffffffffu >> shift) ? 0x7fffffffu : ((over << shift) >> 15) * reciprocal;
                if (f > 0x7fffffffu) f = 0x7fffffffu;
                const i32 now = multiply(amount, static_cast<i32>(f));
                x += now;
                carry = amount - now;
                // The strike's two parts, for the voice's shared modes: the
                // next sample's is written now (the block's last carries over).
                struck[i] += now;
                if (i + 1 < opxy::block_frames) struck[i + 1] += carry;
            }
            return x;
        }
    } contact = {phase, strikes, carry, inc, glide, hit, bounce, shift, reciprocal};
    struck[0] += carry;   // the last block's last strike, carried over
    // Two samples a pass, so the ring's last two samples trade places.
    unsigned i = 0;
    for (; i + 1 < cut; i += 2) {
        const i32 x0 = contact.at(i, struck), x1 = contact.at(i + 1, struck);
        y2 = next(c1, c2, g, y1, y2, Slip ? x0 + (noise[i] >> 16) * slip : x0);
        sum[i] += y2 >> 2;   // a quarter of it, so every voice's parts fit
        y1 = next(c1, c2, g, y2, y1, Slip ? x1 + (noise[i + 1] >> 16) * slip : x1);
        sum[i + 1] += y1 >> 2;
    }
    if (i < cut) {   // an odd sample before it leaves the surface
        const i32 x = contact.at(i, struck);
        const i32 y = next(c1, c2, g, y1, y2, Slip ? x + (noise[i] >> 16) * slip : x);
        y2 = y1;
        y1 = y;
        sum[i] += y >> 2;
        ++i;
    }
    if (i < opxy::block_frames) {   // the slap, then its ring alone
        const i32 x = carry + lane.slap;
        struck[i] += lane.slap;   // its carry is written already
        carry = 0;
        const i32 y = next(c1, c2, g, y1, y2, x);
        y2 = y1;
        y1 = y;
        sum[i] += y >> 2;
        ++i;
        if ((opxy::block_frames - i) & 1) {
            const i32 z = next(c1, c2, g, y1, y2, 0);
            y2 = y1;
            y1 = z;
            sum[i] += z >> 2;
            ++i;
        }
        for (; i < opxy::block_frames; i += 2) {
            y2 = next(c1, c2, g, y1, y2, 0);
            sum[i] += y2 >> 2;
            y1 = next(c1, c2, g, y2, y1, 0);
            sum[i + 1] += y1 >> 2;
        }
    }
    m.y1 = y1;
    m.y2 = y2;
    d.carry = carry;
    d.phase = phase;
    d.strikes = strikes;
}
// One resonance, adding a quarter of itself to sum. Two samples a pass, so
// the last two samples trade places instead of moving.
inline void one(Mode& a, const i32* in, i32* sum) {
    const i32 c1 = a.c1, c2 = a.c2, g = a.gain;
    i32 y1 = a.y1, y2 = a.y2;
    for (unsigned i = 0; i < opxy::block_frames; i += 2) {
        y2 = next(c1, c2, g, y1, y2, in[i]);
        sum[i] += y2 >> 2;
        y1 = next(c1, c2, g, y2, y1, in[i + 1]);
        sum[i + 1] += y1 >> 2;
    }
    a.y1 = y1; a.y2 = y2;
}
// The voice's last pass: the base rings with the strikes and the rolling
// noise (`roll`) and the slip hisses, all added to sum (a quarter of the
// voice); then a DC blocker and a soft knee to the output. Returns the
// largest sample of the quarter mix.
inline i32 ring_out(Voice& voice, const i32* sum, const i32* struck, const i32* noise, opxy::Output output,
                    i32 roll, i32 kr, i32 slipping) {
    Mode& b = voice.base;
    const i32 c1 = b.c1, c2 = b.c2, g = b.gain;
    const i32 kr_ = kr >> 15, roll_ = roll >> 15, slip_ = slipping >> 15;
    i32 rough = voice.rough, last = voice.last;
    i32 dc_in = voice.dc_in, dc_out = voice.dc_out;
    i32 y1 = b.y1, y2 = b.y2;
    i32 loudest = 0;
    // One sample, from the base's last two (ya, yb): it returns the new one.
    struct Out {
        const i32 *sum, *struck, *noise;
        opxy::Output output;
        i32 c1, c2, g, kr, roll, slip;
        i32 &rough, &last, &dc_in, &dc_out, &loudest;
        i32 at(unsigned i, i32 ya, i32 yb) {
            const i32 s = struck[i], n = noise[i] >> 1;
            rough += ((n - rough) >> 16) * kr;
            const i32 r = (rough >> 16) * roll;
            const i32 y = next(c1, c2, g, ya, yb, s + r);
            const i32 hiss = ((n - last) >> 16) * slip;
            last = n;
            // The base at a quarter.
            const i32 mixed = sum[i] + (y >> 4) + ((r + hiss) >> 2);
            dc_out = mixed - dc_in + dc_out - (dc_out >> 10);   // DC blocker, 7 Hz
            dc_in = mixed;
            // Six times the quarter, with a soft knee from 0.375 to a ceiling of 0.5.
            const i32 v = clamp(dc_out, -(5 << 25), 5 << 25), a = v < 0 ? -v : v;
            i32 o = a * 6;
            if (o > 805306368) {   // 0.375
                const i32 over = o - 805306368;
                o = over >= 536870912 ? 1073741824 : o - (multiply(over, over) << 1);
            }
            output[i] = v < 0 ? -o : o;
            loudest = a > loudest ? a : loudest;
            return y;
        }
    } out = {sum, struck, noise, output, c1, c2, g, kr_, roll_, slip_, rough, last, dc_in, dc_out, loudest};
    for (unsigned i = 0; i < opxy::block_frames; i += 2) {
        y2 = out.at(i, y1, y2);
        y1 = out.at(i + 1, y2, y1);
    }
    voice.rough = rough;
    voice.last = last;
    voice.dc_in = dc_in;
    voice.dc_out = dc_out;
    b.y1 = y1;
    b.y2 = y2;
    return loudest;
}
}

inline void initialize(Voice& voice, Shared&, opxy::u32) {
    for (u32 k = 0; k < spin::most; ++k) {
        Disk& d = voice.disk[k];
        d.rolling = 0;
        d.silent = 1;
        d.life = 0;
        d.carry = 0;
        d.ring.y1 = d.ring.y2 = 0;
    }
    for (u32 m = 0; m < 2; ++m) voice.upper[m].y1 = voice.upper[m].y2 = 0;
    voice.partner.y1 = voice.partner.y2 = 0;
    voice.base.y1 = voice.base.y2 = 0;
    voice.upper_flat = 1;
    voice.seed = 0x13579bdfu;
    voice.count = 1;
    voice.rough = voice.last = voice.dc_in = voice.dc_out = 0;
    voice.tuned = 0;
    voice.ground = 0xffffffffu;
    voice.quiet = 1;
    voice.hand = 0;
    voice.sway = 0;
}

// Each note throws its disks down spinning, with all their life left. Their
// count is fixed on the first block, from many. Disks still ringing ring on.
inline void start(Voice& voice, Shared&, opxy::u32) {
    voice.count = 0;   // render sets it from many on its first block
    voice.quiet = 0;
}

inline void destroy(Voice&, Shared&, opxy::u32) {}

inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace sound;
    using opxy::block_frames;
    if (voice.quiet) {
        for (unsigned i = 0; i < block_frames; ++i) output[i] = 0;
        return;
    }

    // The controls.
    const bool keep = spin::held(parameters[SPIN]);
    const i32 step = spin::life_step(parameters[SPIN]);
    const i32 n = spin::exponent(parameters[AIR]);
    const i32 below = spin::below(parameters[SIZE]);
    const i32 cut = spin::top(parameters[SIZE]);
    const i32 cut_life = spin::life_at_cut(cut, n);
    const u32 material = spin::choice(parameters[DISK], 4);
    const i32 after = spin::after_loss(parameters[RING], material);
    const u32 ground = spin::choice(parameters[BASE], 3);
    const i32 wobble = parameters[WOBBLE] < 0 ? 0 : parameters[WOBBLE];
    // How eccentric the disk is, felt early: w (2 - w), Q15.
    const i32 w15 = wobble >> 16;
    const i32 eccentric = clamp((w15 * (65536 - w15)) >> 15, 0, 32767);
    const u32 airy = spin::q16(parameters[AIR]);
    // Wobble's top half also slips: the eccentric disk skids as it rolls.
    const i32 skid = wobble > 0x40000000 ? (wobble - 0x40000000) * 2 : 0;
    const i32 skid2 = multiply(skid, skid);

    // A new note: its disks, thrown.
    if (voice.count == 0) {
        voice.count = spin::count(parameters[MANY]);
        for (u32 k = 0; k < voice.count; ++k) {
            Disk& d = voice.disk[k];
            d.rolling = 1;
            d.silent = 0;
            d.hush = 0;
            d.life = spin::one;
            d.phase = spin::offset[k];
            d.strikes = 0;
            d.turn = spin::offset[k] * 3u;
            d.carry = k == 0 ? throw_strike : throw_strike >> 1;   // the throw
        }
        for (u32 k = voice.count; k < spin::most; ++k) {
            Disk& d = voice.disk[k];
            if (!d.rolling) continue;
            d.rolling = 0;   // a disk the new note has no place for falls flat
            d.carry = 0;
        }
        voice.upper_flat = 0;
        voice.tuned = 0;
        voice.hand = keep ? 32767 : 0;
    }
    const u32 count = voice.count;
    const i32 level = static_cast<i32>(share[count - 1]) << 16;

    // The disks' modes follow the key (and their material), the base its own.
    const u32 key = pitch[0] ? pitch[0] : 1;
    if (key != voice.tuned || material != voice.material) {
        for (u32 k = 0; k < count; ++k) {
            Disk& d = voice.disk[k];
            const u32 ring = static_cast<u32>(multiply(static_cast<i32>(key >> 1), spin::detune[count - 1][k])) << 2;
            tune(d.ring, d.cosine, eighth_of(ring, tables::disk_ratio[material][0]),
                 multiply(tables::disk_level[material][0], level));
        }
        for (u32 m = 0; m < 2; ++m) {
            const u32 eighth = eighth_of(key, tables::disk_ratio[material][m + 1]);
            tune(voice.upper[m], voice.upper_cosine[m], eighth, multiply(tables::disk_level[material][m + 1], level));
        }
        // The partner: the first disk's ring split by the material's few
        // tenths of a hertz, at most 8 cents.
        {
            const u32 eighth = eighth_of(static_cast<u32>(multiply(static_cast<i32>(key >> 1), spin::detune[count - 1][0])) << 2,
                                         tables::disk_ratio[material][0]);
            const u32 apart = tables::split[material] >> 3, most = eighth / 216;
            tune(voice.partner, voice.partner_cosine, eighth + (apart < most ? apart : most),
                 multiply(tables::disk_level[material][0], level));
            voice.partner_level = voice.partner.gain;
        }
        voice.tuned = key;
        voice.material = material;
    }
    // Each block, the modes' damping: as they roll, by the material; once
    // flat, by ring, the upper modes two and three times as fast.
    bool any = false;
    for (u32 k = 0; k < spin::most; ++k) {
        Disk& d = voice.disk[k];
        if (d.silent) continue;
        damp(d.ring, d.cosine, d.rolling ? tables::disk_loss[material][0] * base_damping[ground] / 16 : after);
        any = any || d.rolling;
    }
    for (u32 m = 0; m < 2; ++m)
        damp(voice.upper[m], voice.upper_cosine[m],
             any ? tables::disk_loss[material][m + 1] * base_damping[ground] / 16 : after * static_cast<i32>(m + 2));
    damp(voice.partner, voice.partner_cosine,
         voice.disk[0].rolling ? tables::disk_loss[material][0] * base_damping[ground] / 16 : after);
    // The disk's turn moves its strikes between its ring and the partner.
    const i32 share_ = 11469 + ((8192 * wave(voice.disk[0].turn * 2u + 0x40000000u)) >> 15);   // 0.1 to 0.6, Q15
    voice.partner.gain = multiply(voice.partner_level, share_ << 16);
    if (ground != voice.ground) {
        voice.base.c1 = tables::base_c1[ground];
        voice.base.c2 = opxy::q31::maximum - tables::base_loss[ground];
        voice.base.gain = tables::base_gain[ground];
        voice.ground = ground;
    }

    // The block's noise, shared by the disks' slip and the base's rolling.
    i32 noise[block_frames];
    u32 seed = voice.seed;
    for (unsigned i = 0; i < block_frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        noise[i] = static_cast<i32>(seed);
    }
    voice.seed = seed;

    // The hand: it keeps the disks spinning at a slowly wobbling rate.
    voice.hand += ((keep ? 32767 : 0) - voice.hand) / 16;
    voice.sway += sway_step;
    const i32 depth = 197 + (multiply(wobble, 786 << 16) >> 16);   // 0.003 to 0.015 octave
    const i32 sway = ((depth * voice.hand >> 15) * wave(voice.sway)) >> 15;

    // Strikes louder as the held rate is sparser, so every key and size plays
    // at about the same level: by 3/4 of an octave for each octave lower in
    // key and 0.9 for each step of size (sparser strikes ring less together).
    const i32 lower = (reference + 3 * spin::octave - spin::log2_q16(key)) * 3 / 4
                    + (below - 3 * spin::octave) / 16 * 14745 / 1024;
    i32 loudness = scale(static_cast<u32>(multiply(strike, material_level[material] << 14) << 2),
                         clamp(lower, -3 * spin::octave, 3 * spin::octave));
    // Kept spinning, the strikes fall in step with the ring and it swells, so
    // the hand strikes 5 dB softer.
    loudness -= multiply(loudness, voice.hand * 28000);
    if (loudness > 1 << 28) loudness = 1 << 28;   // a strike (with its swing) stays under 0.21,
                                                  // so four disks' strikes and carries fit

    i32 sum[block_frames], struck[block_frames];
    for (unsigned i = 0; i < block_frames; ++i) sum[i] = struck[i] = 0;
    i32 rolling_noise = 0, slipping = 0, breath = 0;
    u32 sounding = 0, rolling = 0;

    Lane lanes[spin::most];
    u32 active = 0;
    const u32 per_cut = 0x40000000u / static_cast<u32>(cut >> 2);   // for slip's growth, once a block
    for (u32 k = 0; k < spin::most; ++k) {
        Disk& d = voice.disk[k];
        if (d.silent) continue;
        ++sounding;
        const u32 ring = static_cast<u32>(multiply(static_cast<i32>(key >> 1), spin::detune[count - 1][k])) << 2;

        // Where the rate starts and ends this block, and whether the disk
        // leaves the surface in it (at sample `cut_at`).
        Lane& lane = lanes[active++];
        lane.disk = &d;
        i32 inc = 0, glide = 0, hit = 0, bounce = 0, slip = 0;
        u32 cut_at = 0, shift = 0, reciprocal = 0;   // lying flat, it only rings
        if (d.rolling) {
            cut_at = block_frames;
            const i32 own = spin::disk_step(step, k);
            const i32 next = spin::next_life(d.life, own, keep);
            const i32 from = spin::rise(d.life, n) + sway;
            i32 to;
            u32 span = block_frames;
            if (from >= cut) {
                cut_at = 0;
                to = from;
            } else if (!keep && next <= cut_life) {
                const i32 left = d.life > cut_life ? d.life - cut_life : 0;
                cut_at = static_cast<u32>(left / (own / static_cast<i32>(block_frames) + 1));
                if (cut_at > block_frames - 1) cut_at = block_frames - 1;
                span = cut_at ? cut_at : 1;
                to = cut;
            } else {
                to = spin::rise(next, n) + sway;
            }
            d.life = cut_at < block_frames ? 0 : next;

            // The eccentric weight, carried round by the disk's turn, swings
            // the contact rate (a warble) and the strikes' strength.
            const i32 swing = wave(d.turn + 0x40000000u);  // cos of the weight's angle, Q15
            const i32 warble = ((eccentric * swing) >> 15) * warble_most >> 15;
            // Slip jitters the strikes' timing a little, a block at a time.
            const i32 start_inc = scale(ring, from + warble - below);
            inc = start_inc + multiply(multiply(start_inc, skid >> 3), noise[k]);
            const i32 end = scale(ring, to + warble - below);
            glide = span == block_frames ? (end - start_inc) / static_cast<i32>(block_frames)
                                         : (end - start_inc) / static_cast<i32>(span);

            // Where a strike falls between samples: its overshoot over the
            // block's mean increment, by a reciprocal taken once a block.
            u32 mean = (static_cast<u32>(start_inc) >> 1) + (static_cast<u32>(end) >> 1);
            if (mean < 64) mean = 64;
            shift = spin::leading_zeros(mean) - 1;
            reciprocal = 0x7fffffffu / ((mean << shift) >> 15);

            // The strikes: softer as the rate rises (2^(-L/4)), and with
            // wobble, the weight swings them (up to 0.375 to 1.625 times) and
            // a bounce follows.
            const i32 rise = from < 0 ? 0 : from;
            const i32 soft = scale(1u << 30, -rise / 4);   // 2^(-L/4) / 2, Q31
            const i32 base_hit = multiply(loudness, soft) << 1;
            const i32 felt = (base_hit >> 15) * eccentric >> 15;   // base_hit x eccentric, Q31 / 2^15
            hit = base_hit + ((felt * swing) >> 15) * 20480;
            bounce = (felt * ((swing + 32768) >> 1) >> 15) * 24576;
            // The disk's own turn: the contact rate x tilt^2 / 2, the tilt
            // falling as 2^(-2L) from 0.4 rad: 5.12 x 2^(-4L) a block.
            d.turn += static_cast<u32>(scale(static_cast<u32>(start_inc), 154400 - 4 * rise));

            // Slip: noise into the disk's ring, growing as it speeds up.
            const i32 near = clamp(static_cast<i32>((static_cast<u32>(rise) >> 6) * per_cut >> 11), 0, 32767);   // rise / cut, Q15
            const i32 grow = 4915 + (27852 * (near * near >> 15) >> 15);   // 0.15 to 1, Q15
            slip = (multiply(skid2, 30000000) >> 15) * grow;
            slipping += slip >> 3;
            // The rolling noise, 2^(L/2) / 64, swung by the weight's pressing
            // (0.1 to 1.9 times): it chuffs as the disk turns.
            const i32 rumble = scale(1u << 25, rise / 2);
            rolling_noise += rumble + ((rumble >> 15) * ((eccentric * swing) >> 15) >> 15) * 29491;
            breath += scale(1u << 23, rise);              // 2^L / 256, Q31
            ++rolling;
        }

        lane.inc = inc;
        lane.glide = glide;
        lane.hit = hit;
        lane.bounce = bounce;
        // Only the first disk's ring takes slip's noise: in every disk it
        // would cost the Blackfin 5 instructions a sample each (its hiss and
        // jittered strikes are every disk's).
        lane.slip = k == 0 ? slip : 0;
        lane.cut_at = cut_at;
        lane.slap = d.rolling && cut_at < block_frames ? slap : 0;
        lane.shift = shift;
        lane.reciprocal = reciprocal;
    }
    // Each disk in turn.
    for (u32 l = 0; l < active; ++l) {
        const Lane& lane = lanes[l];
        if (lane.slip) roll<true>(lane, sum, struck, noise);
        else roll<false>(lane, sum, struck, noise);
        Disk& d = *lane.disk;
        if (d.rolling && lane.cut_at < block_frames) {
            d.rolling = 0;
            d.carry = 0;
            d.hush = 0;
        } else if (!d.rolling) {
            // Silent once it has ended four blocks running under -84 dB: two
            // samples alone can both be small at a zero crossing, but not
            // four blocks' ends in a row while it still rings.
            const i32 a = d.ring.y1 < 0 ? -d.ring.y1 : d.ring.y1, b = d.ring.y2 < 0 ? -d.ring.y2 : d.ring.y2;
            d.hush = a < rung_out && b < rung_out ? d.hush + 1 : 0;
            if (d.hush >= 4) {
                d.silent = 1;
                d.ring.y1 = d.ring.y2 = 0;
            }
        }
    }
    voice.upper_flat = !rolling;

    // The voice: the disks' upper modes and the partner ring with every
    // strike; then the base, the slip, the DC blocker and the knee.
    one(voice.upper[0], struck, sum);
    one(voice.upper[1], struck, sum);
    if (count == 1) one(voice.partner, struck, sum);   // several disks beat among themselves
    // Air pushed from under the disk breathes, louder and brighter as air rises.
    const i32 roll = multiply(rough_level[ground], rolling_noise)
                   + multiply(static_cast<i32>((breath_level >> 16) * airy), breath);
    const i32 pole = rough_pole[ground] + static_cast<i32>(((breath_pole - rough_pole[ground]) >> 16) * airy);
    const i32 loudest = ring_out(voice, sum, struck, noise, output, roll, pole, slipping);
    if (!sounding && loudest < rung_out) {   // rung out: clear what is left, under -84 dB
        voice.quiet = 1;
        for (u32 m = 0; m < 2; ++m) voice.upper[m].y1 = voice.upper[m].y2 = 0;
        voice.partner.y1 = voice.partner.y2 = 0;
        voice.base.y1 = voice.base.y2 = 0;
        voice.rough = voice.dc_in = voice.dc_out = 0;
    }
}
}
