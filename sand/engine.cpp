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

// Sounds a mode into acc over a block, ramping from (a, b) by (da, db) a
// sample, and returns its phase: one table pointer, two 16 x 16 products.
inline u32 sound(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    a += da * (kStep / 2);
    b += db * (kStep / 2);
    for (i32* step = acc; step != acc + opxy::block_frames; step += kStep) {
        const i32 ah = a >> 16, bh = b >> 16;
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
inline u32 sound_bowed(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db, i32 h, i32 dh) {
    a += da * (kStep / 2);
    b += db * (kStep / 2);
    h += dh * (kStep / 2);
    for (i32* step = acc; step != acc + opxy::block_frames; step += kStep) {
        const i32 ah = a >> 16, bh = b >> 16, hh = h >> 16;
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
inline i32 sounded(i32 amplitude) { return multiply(amplitude, kOutput); }
const u32 kKnock = 6;
const u32 kBounce = 29;  // samples from a landing to its bounce (0.7 ms)
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
    pour(voice.grains, kGrains, voice.seed);
    forget_shapes(voice);
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
    i32 impulses[opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) impulses[i] = 0;
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
            for (u32 k = 0; k < kModes; ++k) voice.gather[k] += shapes[k] * shapes[k] >> 15;
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
    i32 acc[opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) acc[i] = 0;
    const i32 drive = bow_level(bow);
    const i32 rosin = drive * kRosin >> 15;
    const i32 shaken = (((shake * shake >> 15) + shake) >> 1) * kShakeAmplitude >> 15;  // halfway between Shake and its square
    const i32 hit = voice.struck ? strike * kStrikeAmplitude >> 15 : 0;
    const i32 held = multiply(mat.rise, bow << 16);  // the bow's pull, by how much it bows
    for (u32 k = 0; k < kModes; ++k) {
        const PlateMode& pm = modes[k];
        Mode& m = voice.modes[k];
        if (octave + pm.log2_ratio > kTopLog2) {  // above the top: fade out, then rest
            if (m.sa || m.sb) sound(acc, m.phase, kTopIncrement, m.sa, m.sb, -(m.sa >> 6), -(m.sb >> 6));
            m.a = m.b = m.sa = m.sb = 0;
            if (!k) voice.twin.a = voice.twin.b = voice.harmonic.a = voice.harmonic.sa = 0;
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
        i32 decay = keep(log2_loss) - (k ? weight << 8 : weight << 4);
        decay = decay < 0 ? 0 : decay;
        const i32 quiet = weight >> 4 > 16384 ? 16384 : weight >> 4;
        const i32 reach = k ? (mat.halo * kHalo >> 2) * pm.bow >> 15 : 32767 - quiet;
        const i32 target = (drive * reach >> 15) << 16;
        const i32 rate = k ? held >> 2 : held;
        const i32 noise = stir(log2_loss, shaken + (rosin * pm.bow >> 15));
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
            m.phase = sound(acc, m.phase, inc, m.sa, m.sb, (sa - m.sa) >> 6, (sb - m.sb) >> 6);
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
        const i32 ca = sounded(clamp_amplitude(a1 + ta)), cb = sounded(clamp_amplitude(b1 + tb));
        // The bow's stick and slip: the note's 2nd to 4th harmonics, in tune
        // with it, rising with Bow (pressure brightens a bowed tone); faded
        // out where the 4th would pass the top.
        Mode& harm = voice.harmonic;
        const i32 level = inc > kTopIncrement / 4 ? 0 : (bow * bow >> 15) * kHarmonicShare >> 15;  // Q15 of the bowed level
        const i32 harm_target = static_cast<i32>(times(static_cast<u32>(drive * (level * (mat.harmonic >> 3) >> 9) >> 15),
                                                       static_cast<u32>(kHarmonicPeak) << 4)) << 16;
        harm.a = clamp_amplitude(multiply(harm.a, decay) + multiply(harm_target - harm.a, held));
        const i32 ha = sounded(harm.a);
        m.phase = sound_bowed(acc, m.phase, inc, m.sa, m.sb, (ca - m.sa) >> 6, (cb - m.sb) >> 6, harm.sa, (ha - harm.sa) >> 6);
        m.sa = ca;
        m.sb = cb;
        harm.sa = ha;
    }
    voice.struck = 0;
    voice.seed = seed;

    i32 y1 = voice.click1, y2 = voice.click2;
    const i32 limit = kHalf >> 1;  // so c1 y1 cannot overflow
    if (!clicked && magnitude(y1) + magnitude(y2) < 4096) {
        // No landing this block and the last click has rung out: the tone alone.
        y1 = y2 = 0;
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = clamp(acc[i], -kToneLimit, kToneLimit) << 2;
    } else {
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            const i32 y = clamp(impulses[i] + 2 * multiply(y1, c1) - multiply(y2, c2), -limit, limit);
            y2 = y1;
            y1 = y;
            // Each voice's rattle is clamped at a tenth of full scale, so coincident
            // heavy landings across a chord keep the set's headroom.
            const i32 rattle = clamp(y, -kRattleLimit, kRattleLimit);
            output[i] = clamp((clamp(acc[i], -kToneLimit, kToneLimit) << 2) + rattle, -kHalf, kHalf);
        }
    }
    voice.click1 = y1;
    voice.click2 = y2;
}
}
