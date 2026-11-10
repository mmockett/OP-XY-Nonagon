#include "core.h"
#include <opxy/q31.hpp>

// Fermi: each note is a Fermi-Pasta-Ulam-Tsingou chain (chain.h), struck in
// one mode, and the sound is its modes' energies, voiced. Mode k is a partial
// at the note's pitch times w_k / w_m (the chain's own dispersion, slightly
// flat of harmonic, blended toward k / m by dispersion), with the square root
// of the mode's share of the energy as its amplitude. Springs, moving toward
// the cubic law that never feeds them, also fades the partials at even
// multiples of the struck mode. Two touches make it an
// instrument: each partial carries a little body (its own second and third
// harmonics, while they fit under Nyquist), and the modes above the struck
// one are tilted down by sqrt(m / k), the note's loudness held, so a
// thermalized chain sounds lush rather than white.
//
// The chain runs on its own clock, decoupled from pitch (time), a whole
// number of leapfrog steps a block. Low energy: a pure tone. Middle: the
// energy flows up the modes and back, so the timbre breathes on the chain's
// recurrence. High: it thermalizes, a bright spectrum that never settles.
//
// Stereo is the player's, on energy: up to and just past its middle a note is
// one channel twice, and as energy climbs from breathing into chaos the modes
// fly apart. The struck mode stands in the middle, where the note's pitch is, and
// the other modes are shared between two sides, so the width follows the energy
// that has left the struck mode. Left plus right is the mono note.
//
// A synth voice on this SDK hears its note start but not its release:
// every note starts the gesture and the amp envelope ends it (README.md).
namespace fermi {
namespace sound {
using opxy::q31::multiply;
const u32 level = 1311;                     // a mode's amplitude (Q12) to its partial's (Q16): 0.08 x 16 / 1024 x 1024
const u32 fade_from = 1717986918u;          // partials fade out from 0.40 of the sample rate...
const u32 fade_to = 1932735283u;            // ...to silence at 0.45
const i32 ceiling = 1 << 30;                // half scale
// Which modes (0-based) ring together, a struck mode to a row: the first two
// are the middle, the struck mode and the quietest company (the lowest mode,
// under the pitch; with mode 1 struck, the top), the next four one side and
// the last four the other. The odd multiples of the struck mode, all that the
// cubic law feeds, take turns between the sides, as do the low modes, so
// neither side ends up empty.
const opxy::u8 order[4][chain::voiced] = {
    {0, 9, 1, 4, 5, 8, 2, 3, 6, 7},
    {1, 0, 2, 5, 7, 8, 3, 4, 6, 9},
    {2, 0, 1, 4, 7, 8, 3, 5, 6, 9},
    {3, 0, 1, 4, 6, 9, 2, 5, 7, 8},
};
}

// How far energy opens the width, Q16: none up to a little past its default
// (0.52), then all of it at the top, quickly at first: 1 - (1 - u)^2.
const i32 width_from = 17039;   // Q15 of the control
inline i32 width_of(i32 control) {
    const i32 k = chain::amount(control);
    if (k <= width_from) return 0;
    i32 closed = 65536 - (((k - width_from) * 17066) >> 12);   // 17066: 2^28 / (32768 - width_from)
    closed = closed < 0 ? 0 : closed;
    return 65536 - (((closed >> 1) * (closed >> 1)) >> 14);
}

inline void initialize(Voice& voice, Shared& shared, opxy::u32) {
    shared.key = 0;
    shared.configured = 0;
    for (u32 k = 0; k < chain::voiced; ++k) {
        voice.phase[k] = 0;
        voice.amp[k] = 0;
        voice.level[k] = 0;
        voice.plain[k] = 0;
        voice.inc[k] = 0;
        voice.fade[k] = 0;
    }
    voice.key = 0;
    voice.dispersion = -1;
    voice.springs = -1;
    voice.turn = 0;
    voice.balance = 1 << 15;
    voice.width = 0;
    voice.chain.n = 0;
    voice.fresh = 1;
}

// Each note loads its chain afresh on its first block, from the controls then.
inline void start(Voice& voice, Shared&, opxy::u32) { voice.fresh = 1; }

inline void destroy(Voice&, Shared&, opxy::u32) {}

// Two partials side by side, so their loads and sums share one pass. Each
// reads its own wave: the body, or a plain sine once the body's third
// harmonic would pass Nyquist. Levels step four times a block (every 16
// samples) toward their targets: a ramp a sample cost the loop four
// registers it could not spare on the Blackfin.
inline void pair(i32* out, const i16* w1, u32& p1, u32 i1, const i32* g1, const i16* w2, u32& p2, u32 i2, const i32* g2) {
    u32 q1 = p1, q2 = p2;
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        const i32 a1 = g1[quarter], a2 = g2[quarter];
        i32* o = out + 16 * quarter;
        for (unsigned i = 0; i < 16; ++i) {
            o[i] += w1[q1 >> 20] * a1 + w2[q2 >> 20] * a2;
            q1 += i1;
            q2 += i2;
        }
    }
    p1 = q1;
    p2 = q2;
}
// One partial alone, the last of an odd number.
inline void single(i32* out, const i16* w, u32& p, u32 inc, const i32* g) {
    u32 q = p;
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        const i32 a = g[quarter];
        i32* o = out + 16 * quarter;
        for (unsigned i = 0; i < 16; ++i) {
            o[i] += w[q >> 20] * a;
            q += inc;
        }
    }
    p = q;
}
// The same pair with levels ramped every sample, for a note's first block,
// where a level rises from silence and quartered steps would click.
inline void pair_ramped(i32* out, const i16* w1, u32& p1, u32 i1, i32 from1, i32 to1,
                        const i16* w2, u32& p2, u32 i2, i32 from2, i32 to2) {
    u32 q1 = p1, q2 = p2;
    i32 a1 = from1 << 15, a2 = from2 << 15;
    const i32 d1 = ((to1 - from1) << 15) >> 6, d2 = ((to2 - from2) << 15) >> 6;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        out[i] += w1[q1 >> 20] * (a1 >> 15) + w2[q2 >> 20] * (a2 >> 15);
        q1 += i1;
        q2 += i2;
        a1 += d1;
        a2 += d2;
    }
    p1 = q1;
    p2 = q2;
}

// The output with no width, one channel twice, held within half scale: a
// function of its own so its loop is a hardware loop. The ceiling is passed
// in, so the compiler keeps the clamp as a minimum and a maximum.
__attribute__((noinline)) void finish(const i32* sum, opxy::Output output, i32 most) {
    const i32 least = -most;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 x = sum[i] > most ? most : sum[i];
        output[2 * i] = output[2 * i + 1] = x < least ? least : x;
    }
}

inline void render(Voice& voice, Shared& shared, opxy::u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace sound;
    // The controls' figures change only with the controls, and every voice
    // playing the same ones shares them.
    chain::Setup& s = shared.setup;
    u32 changed = !shared.configured;
    for (u32 k = 0; k < 8; ++k) changed |= static_cast<u32>(parameters[k] ^ shared.controls[k]);
    if (changed) {
        chain::configure_sound(s, parameters);
        for (u32 k = 0; k < 8; ++k) shared.controls[k] = parameters[k];
        shared.configured = 1;
    }
    chain::Chain& c = voice.chain;

    // A new note, or a new chain: masses and the struck mode restart it.
    const bool load = voice.fresh || s.n != c.n || s.m != c.m;
    if (load) {
        // The starting shape is made once for all the notes that share it:
        // the seed's nudge costs about 1,400 instructions at 32 masses.
        const u32 key = ((s.seed << 8) | (s.size << 4) | s.m) + 1;
        if (shared.key != key) {
            chain::shape(shared.start, s);
            shared.key = key;
        }
        chain::init(c, s, shared.start);
        voice.fresh = 0;
    }
    const u32 steps = chain::advance(c, s);
    const u32 count = c.n < chain::voiced ? c.n : chain::voiced;
    if (steps || load || s.loss) {
        chain::Folded f;
        chain::fold(c, f);
        // Each level eases an eighth of the way (a 35 ms lag): at a fast clock
        // a mode's energy flickers within a block, and taking it raw would
        // roughen the partial; eased, chaos shimmers instead. A third of the
        // modes are measured a block, in turn.
        voice.turn = load ? 0 : voice.turn == 2 ? 0 : voice.turn + 1;
        for (u32 k = load ? 0 : voice.turn; k < chain::voiced; k += load ? 1 : 3) {
            const u32 now = k < count ? chain::root(chain::energy(c, f, k + 1, s.shift)) << 4 : 0;   // Q16
            voice.level[k] = load ? now : voice.level[k] + ((static_cast<i32>(now - voice.level[k])) >> 3);
        }
    }

    // Each partial's increment, from the mode's ratio to the struck mode's:
    // the chain's own, blended toward harmonic. Partials near Nyquist fade,
    // and those at even multiples of the struck mode fade with springs.
    const u32 key = pitch[0];
    const u32* chain_ratio = &tables::ratio[(c.size * 4 + c.m - 1) * 17];
    const u32* harmonic = &tables::harmonic[(c.m - 1) * 17];
    // The tilt, and the gain that keeps the note's energy where the chain has it.
    const u16* tilt = &tables::tilt[(c.m - 1) * 17];
    u32 tilted[chain::voiced];
    u32 plain = 0, leaned = 0;
    for (u32 k = 0; k < chain::voiced; ++k) {
        tilted[k] = (voice.level[k] * tilt[k + 1]) >> 15;   // Q16
        const u32 a = voice.level[k] >> 4, b = tilted[k] >> 4;
        plain += a * a;
        leaned += b * b;
    }
    const u32 lean = chain::root(leaned);
    u32 hold = lean ? (chain::root(plain) << 12) / lean : 4096;   // Q12, once a block
    hold = hold > 16384 ? 16384 : hold;
    // The increments (and their fades) change only with the pitch, the
    // dispersion, the springs or the chain.
    const u32 third = fade_to / 3;   // the body's third harmonic stays under 0.45 of the rate
    if (load || key != voice.key || s.stretch != voice.dispersion || s.springs != voice.springs) {
        const u32 even = static_cast<u32>(32768 - s.springs);   // Q15: gone at the cubic law
        u32 next = 2 * c.m;                                      // the next even multiple, 1-based
        for (u32 k = 0; k < chain::voiced; ++k) {
            const bool doubled = k + 1 == next;   // an even multiple of the struck mode
            if (doubled) next += 2 * c.m;
            voice.inc[k] = 0;
            voice.fade[k] = 0;
            if (k >= count) continue;
            const i32 h = static_cast<i32>(harmonic[k + 1]), r = static_cast<i32>(chain_ratio[k + 1]);
            const u32 ratio = static_cast<u32>(h + ((((r - h) >> 4) * s.stretch) >> 11));   // Q16
            const u32 scaled = static_cast<u32>(multiply(static_cast<i32>(key >> 1), static_cast<i32>(ratio << 10)));
            if (scaled >= (fade_to >> 6)) continue;
            voice.inc[k] = scaled << 6;   // key x ratio / 64, back to Q32
            voice.fade[k] = voice.inc[k] > fade_from ? (((fade_to - voice.inc[k]) >> 13) * 1280) >> 10 : 32768;
            if (doubled) voice.fade[k] = (voice.fade[k] * even) >> 15;
        }
        voice.key = key;
        voice.dispersion = s.stretch;
        voice.springs = s.springs;
    }
    const i16* wave[chain::voiced];
    i32 gains[chain::voiced][4];   // each partial's level over the block's four quarters, Q16
    bool moving[chain::voiced];
    i32 was[chain::voiced];
    for (u32 k = 0; k < chain::voiced; ++k) {
        u32 target = 0;
        if (voice.inc[k]) {
            target = ((((tilted[k] * hold) >> 12) >> 4) * level) >> 10;   // Q16
            // The body while its third harmonic fits, else a plain sine. A
            // partial changes wave only once it is silent: it fades out over a
            // block, changes, and comes back.
            const u32 wanted = voice.inc[k] >= third;
            if (wanted != voice.plain[k]) {
                if (!(voice.amp[k] >> 15)) voice.plain[k] = wanted;
                else target = 0;
            }
            if (!voice.plain[k] && target) target = (target * tables::body_gain) >> 15;
            target = (target * voice.fade[k]) >> 15;
        }
        was[k] = voice.amp[k] >> 15;
        const i32 delta = static_cast<i32>(target << 15) - voice.amp[k];
        for (u32 q = 0; q < 4; ++q) gains[k][q] = (voice.amp[k] + (delta >> 2) * static_cast<i32>(q + 1)) >> 15;
        moving[k] = delta != 0;
        voice.amp[k] = static_cast<i32>(target << 15);
        wave[k] = voice.plain[k] ? tables::sine : tables::body;
    }

    // Three buses whose sum is the mono voice: the middle and the two sides,
    // the voice saying which side is which, so a chord's notes spread. With no
    // width, here or still closing, every partial rings into the middle.
    const i32 width = width_of(parameters[chain::ENERGY]);
    const bool opening = width || voice.width;
    const opxy::u8* const row = order[c.m - 1];
    i32 bus[3][opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) bus[0][i] = 0;
    // The partials in row[from, to) into one bus. A note's first block ramps
    // them two by two; after it, silent partials only keep time and the rest
    // sound two by two, whichever they are.
    const auto ring = [&](i32* into, u32 from, u32 to) {
        if (load) {
            for (u32 slot = from; slot < to; slot += 2) {
                const u32 k = row[slot], j = row[slot + 1];
                pair_ramped(into, wave[k], voice.phase[k], voice.inc[k], was[k], gains[k][3],
                            wave[j], voice.phase[j], voice.inc[j], was[j], gains[j][3]);
            }
            return;
        }
        u32 waiting = chain::voiced;
        for (u32 slot = from; slot < to; ++slot) {
            const u32 k = row[slot];
            if (!gains[k][3] && !moving[k]) {
                voice.phase[k] += voice.inc[k] << 6;
            } else if (waiting == chain::voiced) {
                waiting = k;
            } else {
                pair(into, wave[waiting], voice.phase[waiting], voice.inc[waiting], gains[waiting], wave[k], voice.phase[k], voice.inc[k], gains[k]);
                waiting = chain::voiced;
            }
        }
        if (waiting != chain::voiced) single(into, wave[waiting], voice.phase[waiting], voice.inc[waiting], gains[waiting]);
    };
    // The sides sound only while the width is open and a partial on them does.
    bool spread = false;
    u32 power[2] = {0, 0};   // each side's, this block
    if (opening)
        for (u32 slot = 2; slot < chain::voiced; ++slot) {
            const u32 k = row[slot], a = static_cast<u32>(gains[k][3]) >> 2;
            spread = spread || gains[k][3] || moving[k];
            power[slot < 6 ? 0 : 1] += a * a;
        }
    if (!spread) {
        ring(bus[0], 0, chain::voiced);
        voice.width = width;
        finish(bus[0], output, ceiling);
        return;
    }
    for (unsigned i = 0; i < opxy::block_frames; ++i) bus[1][i] = bus[2][i] = 0;
    ring(bus[0], 0, 2);
    ring(bus[1], 2, 6);
    ring(bus[2], 6, chain::voiced);
    // Each side leans in inverse proportion to its power, so the side stays
    // uncorrelated with the note and the channels level, but for a floor of an
    // eighth of the two sides' power: a lone side still leans a little. At full
    // width the leans add up to 1.25: 0.625 each when the sides balance. A channel
    // stops at half scale, as the note does; its peaks stay far from it, so left
    // plus right stays the note.
    const u32 first = owner & 1, floor = (power[0] + power[1]) >> 3;
    const u32 weigh = power[first ^ 1u] + floor, weigh_all = power[0] + power[1] + 2 * floor;
    i32 balance = voice.balance;
    if (weigh_all) {
        const u32 bits = 32 - chain::leading_zeros(weigh_all), down = bits > 15 ? bits - 15 : 0;
        balance = static_cast<i32>(((weigh >> down) << 16) / (weigh_all >> down));   // Q16
    }
    // Both leans ramp across the block, so turning energy never steps the width.
    i32 one_lean = ((voice.balance >> 1) * (voice.width >> 1)) >> 14;
    i32 other_lean = (((65536 - voice.balance) >> 1) * (voice.width >> 1)) >> 14;
    const i32 one_slide = ((((balance >> 1) * (width >> 1)) >> 14) - one_lean) >> 6;
    const i32 other_slide = ((((65536 - balance) >> 1) * (width >> 1) >> 14) - other_lean) >> 6;
    voice.balance = balance;
    voice.width = width;
    const i32* middle = bus[0];
    const i32* a = bus[1 + first];
    const i32* b = bus[2 - first];
    for (i32* o = output; o != output + 2 * opxy::block_frames; o += 2) {
        const i32 one = *a++, other = *b++;
        const i32 sides = one + other, note = *middle++ + sides;
        one_lean += one_slide;
        other_lean += other_slide;
        const i32 d = (one >> 16) * one_lean - (other >> 16) * other_lean;
        const i32 wide = d + (d >> 2);
        const i32 left = note + wide < ceiling ? note + wide : ceiling, right = note - wide < ceiling ? note - wide : ceiling;
        o[0] = left > -ceiling ? left : -ceiling;
        o[1] = right > -ceiling ? right : -ceiling;
    }
}
}
