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
// A synth voice on this SDK hears its note start but not its release:
// every note starts the gesture and the amp envelope ends it (README.md).
namespace fermi {
namespace sound {
using opxy::q31::multiply;
const u32 level = 1311;                     // a mode's amplitude (Q12) to its partial's (Q16): 0.08 x 16 / 1024 x 1024
const u32 fade_from = 1717986918u;          // partials fade out from 0.40 of the sample rate...
const u32 fade_to = 1932735283u;            // ...to silence at 0.45
const i32 ceiling = 1 << 30;                // half scale
}

inline void initialize(Voice& voice, Shared& shared, opxy::u32) {
    shared.key = 0;
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
__attribute__((noinline)) void pair(i32* out, const i16* w1, u32& p1, u32 i1, const i32* g1, const i16* w2, u32& p2, u32 i2, const i32* g2) {
    u32 q1 = p1, q2 = p2;
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        const i16 a1 = static_cast<i16>(g1[quarter]), a2 = static_cast<i16>(g2[quarter]);
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
// The same pair with levels ramped every sample, for a note's first block,
// where a level rises from silence and quartered steps would click.
__attribute__((noinline)) void pair_ramped(i32* out, const i16* w1, u32& p1, u32 i1, i32 from1, i32 to1,
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

// The output, within half scale: a function of its own, so its loop is a hardware loop.
__attribute__((noinline)) void held_within(const i32* sum, opxy::Output output) {
    using sound::ceiling;
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        output[i] = sum[i] > ceiling ? ceiling : sum[i] < -ceiling ? -ceiling : sum[i];
}

inline void render(Voice& voice, Shared& shared, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    using namespace sound;
    chain::Setup s;
    chain::configure(s, parameters);
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

    i32 sum[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) sum[i] = 0;
    for (u32 k = 0; k < chain::voiced; k += 2) {
        // Silent pairs only keep time.
        if (!gains[k][3] && !gains[k + 1][3] && !moving[k] && !moving[k + 1]) {
            voice.phase[k] += voice.inc[k] << 6;
            voice.phase[k + 1] += voice.inc[k + 1] << 6;
            continue;
        }
        if (load)
            pair_ramped(sum, wave[k], voice.phase[k], voice.inc[k], was[k], gains[k][3],
                        wave[k + 1], voice.phase[k + 1], voice.inc[k + 1], was[k + 1], gains[k + 1][3]);
        else
            pair(sum, wave[k], voice.phase[k], voice.inc[k], gains[k], wave[k + 1], voice.phase[k + 1], voice.inc[k + 1], gains[k + 1]);
    }
    held_within(sum, output);
}
}
