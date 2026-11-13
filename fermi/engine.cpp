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
const u32 level = 2358;                     // a mode's amplitude (Q12) to its partial's (Q16): 0.144 x 16 / 1024 x 1024 (0.08, lifted 5.1 dB to the suites' shared loudness)
const u32 fade_from = 1717986918u;          // partials fade out from 0.40 of the sample rate...
const u32 fade_to = 1932735283u;            // ...to silence at 0.45
const i32 ceiling = 1 << 30;                // half scale
// The partials' floor: one under -80 dBFS (a level of 65536 is full scale),
// or more than 40 dB under the note's loudest, falls silent and costs nothing;
// a silent one sounds again only once it is within 36 dB, so a partial
// hovering at the line does not flicker.
const u32 quietest = 7;                     // -80 dBFS, Q16
const u32 below_off = 100, below_on = 64;   // 40 and 36 dB
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
    voice.chain.n = 0;
    voice.fresh = 1;
    voice.quiet = 0;
}

// Each note loads its chain afresh on its first block, from the controls then.
inline void start(Voice& voice, Shared&, opxy::u32) { voice.fresh = 1; }

inline void destroy(Voice&, Shared&, opxy::u32) {}

// One partial over the block, added into out. Levels step four times a
// block (every 16 samples) toward their targets: a ramp a sample costs
// registers the Blackfin cannot spare. Two samples a pass, each read through
// an address made a sample earlier, so the address register is ready when
// the load needs it (an address made just before costs 4 cycles a read).
// Partials run one at a time: two to a pass needed four such addresses and
// spilled. The phase ends 64 increments on, as two were made past the block.
inline void partial(i32* out, const i16* w, u32& p, u32 inc, const i32* g) {
    u32 q = p;
    const i16* even = w + (q >> 22);
    q += inc;
    const i16* odd = w + (q >> 22);
    q += inc;
    for (unsigned quarter = 0; quarter < 4; ++quarter) {
        const i32 a = g[quarter];
        i32* o = out + 16 * quarter;
        for (unsigned i = 0; i < 16; i += 2) {
            const i32 x = *even;
            even = w + (q >> 22);
            q += inc;
            o[i] += x * a;
            const i32 y = *odd;
            odd = w + (q >> 22);
            q += inc;
            o[i + 1] += y * a;
        }
    }
    p = q - 2 * inc;
}
// The same with its level ramped every sample, for a note's first block,
// where a level rises from silence (or a retriggered note's falls) and
// quartered steps would click.
inline void partial_ramped(i32* out, const i16* w, u32& p, u32 inc, i32 from, i32 to) {
    u32 q = p;
    i32 a = from << 15;
    const i32 d = ((to - from) << 15) >> 6;
    const i16* even = w + (q >> 22);
    q += inc;
    const i16* odd = w + (q >> 22);
    q += inc;
    for (unsigned i = 0; i < opxy::block_frames; i += 2) {
        const i32 x = *even;
        even = w + (q >> 22);
        q += inc;
        out[i] += x * (a >> 15);
        a += d;
        const i32 y = *odd;
        odd = w + (q >> 22);
        q += inc;
        out[i + 1] += y * (a >> 15);
        a += d;
    }
    p = q - 2 * inc;
}

// The output, held within half scale: a function of its own so its loop is a
// hardware loop. The ceiling is passed in, so the compiler keeps the clamp as
// a minimum and a maximum.
__attribute__((noinline)) void finish(const i32* sum, opxy::Output output, i32 most) {
    const i32 least = -most;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 x = sum[i] > most ? most : sum[i];
        output[i] = x < least ? least : x;
    }
}

// The tilt's gain, min(num / lean, 16384), for num < 2^28 and 0 < lean < 2^16,
// without the runtime's divide: past the clamp it is the clamp, and under it
// the quotient has 14 bits, which long division finds in 14 compares.
inline u32 tilt_gain(u32 num, u32 lean) {
    u32 d = lean << 14;
    if (num >= d) return 16384;
    u32 q = 0;
    for (u32 i = 0; i < 14; ++i) {
        d >>= 1;
        const u32 take = num >= d;
        num -= d & (0u - take);
        q = (q << 1) | take;
    }
    return q;
}

// Each partial's increment, from the mode's ratio to the struck mode's: the
// chain's own, blended toward harmonic. Partials near Nyquist fade, and those
// at even multiples of the struck mode fade with springs. Out of line: it
// runs only when they change, and render is called from two places.
static __attribute__((noinline)) void tune(Voice& voice, const chain::Setup& s, u32 key) {
    using namespace sound;
    const chain::Chain& c = voice.chain;
    const u32 count = c.n < chain::voiced ? c.n : chain::voiced;
    const u32* chain_ratio = &tables::ratio[(c.size * 4 + c.m - 1) * 17];
    const u32* harmonic = &tables::harmonic[(c.m - 1) * 17];
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
// A damped chain that has died away: every displacement and velocity under
// 2^-8 of a unit (Q20), far below anything its modes still sound.
inline bool still(const chain::Chain& c) {
    u32 widest = 0;
    for (u32 j = 1; j <= c.n; ++j) {
        const i32 x = c.x[j], v = c.v[j];
        widest |= static_cast<u32>(x < 0 ? -x : x) | static_cast<u32>(v < 0 ? -v : v);
    }
    return widest < 4096;
}

inline void render(Voice& voice, Shared& shared, opxy::u32, opxy::Output output,
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
        voice.quiet = 0;
    }
    const u32 key = pitch[0];
    const bool retune = load || key != voice.key || s.stretch != voice.dispersion || s.springs != voice.springs;
    // A damped note that has died away stays silent until a new note or a new
    // chain: no steps, no projections, no partials. Its partials keep time
    // (and their pitch), so the next note starts them where it would have.
    if (voice.quiet) {
        if (retune) tune(voice, s, key);
        for (u32 k = 0; k < chain::voiced; ++k) voice.phase[k] += voice.inc[k] << 6;
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
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
    const u32 hold = lean ? tilt_gain(chain::root(plain) << 12, lean) : 4096;   // Q12, once a block
    // The increments (and their fades) change only with the pitch, the
    // dispersion, the springs or the chain.
    const u32 third = fade_to / 3;   // the body's third harmonic stays under 0.45 of the rate
    if (retune) tune(voice, s, key);
    const i16* wave[chain::voiced];
    i32 gains[chain::voiced][4];   // each partial's level over the block's four quarters, Q16
    bool moving[chain::voiced];
    i32 was[chain::voiced];
    u32 targets[chain::voiced];
    u32 loudest = 0;
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
        targets[k] = target;
        loudest = target > loudest ? target : loudest;
    }
    for (u32 k = 0; k < chain::voiced; ++k) {
        u32 target = targets[k];
        if (target < quietest || target * (voice.amp[k] ? below_off : below_on) < loudest) target = 0;
        was[k] = voice.amp[k] >> 15;
        const i32 delta = static_cast<i32>(target << 15) - voice.amp[k];
        for (u32 q = 0; q < 4; ++q) gains[k][q] = (voice.amp[k] + (delta >> 2) * static_cast<i32>(q + 1)) >> 15;
        moving[k] = delta != 0;
        voice.amp[k] = static_cast<i32>(target << 15);
        wave[k] = voice.plain[k] ? tables::sine : tables::body;
    }

    i32 sum[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) sum[i] = 0;
    // Silent partials (silent all block: on a note's first block, silent in
    // the last note and in this one) only keep time; the rest sound, ramped
    // on a note's first block.
    for (u32 k = 0; k < chain::voiced; ++k) {
        if (!gains[k][3] && !moving[k]) voice.phase[k] += voice.inc[k] << 6;
        else if (load) partial_ramped(sum, wave[k], voice.phase[k], voice.inc[k], was[k], gains[k][3]);
        else partial(sum, wave[k], voice.phase[k], voice.inc[k], gains[k]);
    }
    // Silent, with every mode's level at zero and damping holding the chain
    // there: from the next block, the note stops.
    if (s.loss) {
        u32 any = 0;
        for (u32 k = 0; k < chain::voiced; ++k) any |= voice.level[k] | static_cast<u32>(voice.amp[k]);
        if (!any) voice.quiet = still(c);
    }
    finish(sum, output, ceiling);
}
}
