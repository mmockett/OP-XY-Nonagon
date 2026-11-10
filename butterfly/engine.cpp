#include "core.h"
#include "tables_data.h"   // the tables, defined here once

// Butterfly: a struck chain of sites in a magnetic field (harper.h). A note
// is one partial for each of the chain's 17 states, at its energy on a log
// scale above the key (the lowest band's middle), as loud as the weight the
// state gives the struck site, behind a vactrol gate in Moth's vocabulary:
// strike pings it open over a held level, glow sets how slowly it closes, and
// the top of the spectrum closes first (tilt). At strike 0 the gate simply
// holds open while the note sounds: a pad. A slow wingbeat moves the flux
// and the struck site, so the clusters shimmer. The spectrum comes from
// tables, so a note sounds in its first block. The OP-XY's amp envelope ends
// the note.
//
// Stereo is the player's, on the wingbeat: at its resting 15 and below a note
// is one channel twice, and past it the wings open. The width comes from the
// chain's mirror. Its states alternate by rank between even under the mirror,
// alike on both wings, and odd, opposite on the two and with a node at the
// middle. So the even states, the lowest among them, stay in the middle, and the
// odd ones lean out, every other one each way: struck at the middle site the
// odd states all but vanish and the note stands centred however far the wings
// are open. Which state is loud moves with every control, so each block weighs
// the two ways against each other, the louder leaning less, and neither channel
// takes the note's weight from the other. Left plus right is always the mono
// note.
namespace butterfly {
const u32 ceiling_fade = 0x29cbc14eu, ceiling = 0x2e709de5u;   // 7.2 and 8 kHz: the half-rate bank's top
const i32 silence = 1 << 10;        // Q25: below this a partial is skipped
// The half-band filter's odd-phase taps, Q15 (a 27-tap Kaiser-windowed sinc,
// beta 5.5): flat to 8 kHz, its image of the bank 58 dB down above 14 kHz.
const i32 halfband[halfband_taps] = {20556, -6082, 2851, -1374, 597, -205, 38};
// The side's shorter half-band (11 taps, beta 3): the side is quieter than the
// note and its top states rarer, so it keeps its images 36 dB down at 14.9 kHz.
const i32 side_band[3] = {19828, -4970, 1526};

// v within -top..top, as the Blackfin's MIN and MAX.
inline i32 limit(i32 v, i32 top) {
    const i32 under = v < top ? v : top;
    return under > -top ? under : -top;
}

// How far the wingbeat opens the width, Q16: none up to a little past its
// default (0.16), then all of it at the top, quickly at first: 1 - (1 - u)^2.
const i32 width_from = 5243;   // Q15 of the control
inline i32 width_of(i32 control) {
    const i32 k = q15(control);
    if (k <= width_from) return 0;
    const i32 closed = 65536 - clamp(((k - width_from) * 39012) >> 14, 0, 65536);   // 39012: 2^30 / (32767 - width_from)
    return 65536 - (((closed >> 1) * (closed >> 1)) >> 14);
}

// The bank's and the half-band's sample loops, functions of their own so the
// Blackfin compiler makes each a zero-overhead hardware loop (inlined into
// render's partial loop, the bank branched every sample).
// Each sample reads the sine the sample before addressed, so the Blackfin
// moves the index to an address register well ahead of its load.
__attribute__((noinline)) u32 bank(i32* acc, u32 ph, u32 step_phase, i32 g, i32 slide) {
    ph += step_phase;
    const opxy::i16* at = tables::sine + (ph >> (32 - tables::sine_bits));
    for (u32 i = 0; i < half_frames; ++i) {
        const i32 s = *at;
        ph += step_phase;
        at = tables::sine + (ph >> (32 - tables::sine_bits));
        acc[i] += s * static_cast<opxy::i16>(static_cast<u32>(g) >> 10);
        g += slide;
    }
    return ph - step_phase;   // the phase after the block's last sample
}
inline i32 tap(const i32* x, i32 t) {
    const i32 pair = (x[-t] >> 1) + (x[t + 1] >> 1);   // Q29
    return static_cast<opxy::i16>(static_cast<u32>(pair + 16384) >> 15) * static_cast<opxy::i16>(halfband[t]);   // 16 bits of it, rounded (truncating would
                                                       // offset these samples: a tone at 22 kHz)
}
// Back to the full rate: each half-rate sample, then the half-band's value
// between it and the next, its seven taps written out (a seven-step inner
// loop is left branching).
__attribute__((noinline)) void upsample(const i32* low, opxy::Output output) {
    for (u32 j = 0; j < half_frames; ++j) {
        const i32* x = low + j + halfband_taps - 1;
        const i32 between_samples = tap(x, 0) + tap(x, 1) + tap(x, 2) + tap(x, 3) + tap(x, 4) + tap(x, 5) + tap(x, 6);
        output[2 * j] = x[0];
        output[2 * j + 1] = clamp(between_samples, -(1 << 29), (1 << 29) - 1) * 2;
    }
}
static_assert(halfband_taps == 7, "upsample writes out seven taps");

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    for (u32 k = 0; k < partials; ++k) {
        voice.phase[k] = 0;
        voice.amplitude[k] = 0;
    }
    for (u32 i = 0; i < half_frames + 2 * halfband_taps; ++i) voice.low[i] = 0;
    for (u32 i = 0; i < side_tail; ++i) voice.side[i] = 0;
    voice.leaned = 0;
    voice.balance = 1 << 15;
    voice.width = 0;
    voice.gate = 0;
    voice.blocks = 0;
    voice.beat = voice.beat_site = 0;
    voice.strikes = owner * 0x2545f491u;
    voice.struck = voice.sounding = voice.hush = 0;
}
// A note-on is the strike. There is no note-off: the gate holds while the
// note sounds, and the OP-XY's amp envelope ends it.
inline void start(Voice& voice, Shared&, opxy::u32) {
    voice.struck = 1;
    voice.hush = voice.sounding;   // a voice still ringing fades out in one block first
}
inline void destroy(Voice&, Shared&, opxy::u32) {}

inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    const i32* p = parameters;
    const bool hushing = voice.hush != 0;
    if (voice.struck && !hushing) {
        // Each partial starts at a phase of its own (golden-ratio steps from
        // the note's own): in phase, a clustered spectrum's partials revive
        // together every few tens of milliseconds, and a chord's revivals
        // pile up past full scale.
        const u32 common = ++voice.strikes * 0x9e3779b9u;
        for (u32 k = 0; k < partials; ++k) {
            voice.phase[k] = common + k * 0x9e3779b9u;
            voice.amplitude[k] = 0;
        }
        voice.gate = 0;
        voice.blocks = 0;
        voice.beat = voice.beat_site = 0;
        voice.struck = 0;
    }

    // The chain this block: the flux, snapped and beating, the coupling, the
    // struck site, beating on its own phase.
    const u32 alpha = warp(flux(p[FLUX]), snap_pull) + static_cast<u32>(wingbeat_flux(p[WINGBEAT], voice.beat));
    const i32 lambda = coupling(p[COUPLING]);
    const i32 at = clamp(site(p[SITE]) + wingbeat_site(p[WINGBEAT], voice.beat_site), 0, 8 << 16);
    Spectrum spectrum_now;
    spectrum(alpha, lambda, at, spectrum_now);
    Notes notes;
    shape(spectrum_now, lambda, notes);
    const u32 rate = wingbeat_rate(p[WINGBEAT]);
    voice.beat += rate;
    voice.beat_site += wingbeat_site_rate(rate);
    if (!hushing) {
        voice.gate = gate_step(voice.gate, voice.blocks, strike(p[STRIKE]), glow_log2(p[GLOW]));
        if (voice.blocks < 0xffffu) ++voice.blocks;
    }

    // Spread: the span of the spectrum in octaves, Q16.
    const i32 octaves = static_cast<i32>(exp2_q16(spread_log2(p[SPREAD])));
    const i32 slope = tilt(p[TILT]);
    const u32 note = pitch[0];
    const u32 light = hushing ? 0 : static_cast<u32>(voice.gate) >> 15;   // Q15; 0: every partial's target is
    const i32 gate_log = light ? gate_log2(light) : 0, makeup = tilt_makeup(slope);
    bool sounding = false, rendered = false;
    i32* low = voice.low;
    i32* acc = low + 2 * halfband_taps;
    for (u32 i = 0; i < half_frames; ++i) acc[i] = 0;
    // With the wings open, or still closing, the odd states ring into two buses
    // of their own (ranks 1, 5, 9, 13 and 3, 7, 11, 15). Closed, they join the note.
    const i32 width = width_of(p[WINGBEAT]);
    const bool opening = width || voice.width;
    i32 lean[2][half_frames];
    if (opening) for (u32 i = 0; i < half_frames; ++i) lean[0][i] = lean[1][i] = 0;
    i32* const bus[3] = {acc, opening ? lean[0] : acc, opening ? lean[1] : acc};
    u32 power[3] = {0, 0, 0};   // each bus's, this block: Q13 levels squared
    bool leaning = false;
    for (u32 k = 0; k < partials; ++k) {
        const i32 a = voice.amplitude[k];
        const i32 level = notes.level[k];
        const i32 place = notes.place[k];
        // The partial's increment: the note's, 2^(octaves x place) times.
        u32 pm;
        i32 pw;
        exp2_parts(mul16(octaves, place), pm, pw);
        const u32 base = mul16u(note, pm);   // note x 2^(fraction) / 2
        const i32 up = pw + 1;
        u32 increment = 0;
        i32 gain = 0;
        if (up >= 0 && up < 31 && base < (ceiling >> up)) {
            increment = base << up;
            gain = increment <= ceiling_fade ? 32768 : static_cast<i32>((((ceiling - increment) >> 16) * 112978u) >> 12);   // 32768 / 1188
        }
        i32 target = 0;
        if (light && level > 0 && gain > 0)
            target = ((((level * partial_share(gate_log, makeup, slope, place)) >> 15) * gain) >> 15) << 10;   // Q25
        voice.amplitude[k] = target < silence ? 0 : target;
        const u32 step_phase = increment << 1;   // a half-rate sample
        u32 ph = voice.phase[k];
        if (target < silence && a < silence) {
            voice.phase[k] = ph + (step_phase << 5);
            continue;
        }
        rendered = true;
        if (target >= silence) sounding = true;
        const u32 way = k & 1 ? 1u + ((k >> 1) & 1) : 0u;
        i32* const into = bus[way];
        if (opening) {
            if (k & 1) leaning = true;
            power[way] += static_cast<u32>(target >> 12) * static_cast<u32>(target >> 12);
        }
        // The bank: 32 samples at half rate, the amplitude ramped across them.
        voice.phase[k] = bank(into, ph, step_phase, a, (target - a) >> 5);
    }
    voice.sounding = sounding ? 1 : 0;
    if (hushing) {
        voice.hush = 0;
        voice.sounding = 0;
    }
    // The note holds every state; the side is the odd buses' difference, in
    // a lower precision that its short half-band sums without shifts. Each bus
    // leans in inverse proportion to its power, so the side stays uncorrelated
    // with the note and the channels level, but for a floor of an eighth of the
    // note's power: a lone odd state still leans a little. The note's strike says
    // which bus leans which way, so a chord spreads. With the wings fully open
    // the leans add up to 1.25: 0.625 each when the buses balance.
    i32 side[side_tail + half_frames];
    if (leaning) {
        const u32 flip = (voice.strikes * 0x9e3779b9u) >> 31;
        const u32 floor = (power[0] + power[1] + power[2]) >> side_floor;
        const u32 weigh = power[2 - flip] + floor, weigh_all = power[1] + power[2] + 2 * floor;
        i32 balance = voice.balance;
        if (weigh_all) {
            const i32 up = static_cast<i32>(leading_zeros(weigh_all)) - 16;   // as a Q16 fraction, 1/2 to 1
            const u32 fraction = up >= 0 ? weigh << up : weigh >> -up;
            const u32 whole = up >= 0 ? weigh_all << up : weigh_all >> -up;
            balance = static_cast<i32>(mul16u(fraction, static_cast<u32>(reciprocal(static_cast<i32>(whole)))));   // Q16
        }
        // Both leans ramp across the block, so turning the wingbeat never steps.
        i32 first_lean = ((voice.balance >> 1) * (voice.width >> 1)) >> 14;
        i32 second_lean = (((65536 - voice.balance) >> 1) * (voice.width >> 1)) >> 14;
        const i32 first_slide = ((((balance >> 1) * (width >> 1)) >> 14) - first_lean) >> 5;
        const i32 second_slide = ((((65536 - balance) >> 1) * (width >> 1) >> 14) - second_lean) >> 5;
        voice.balance = balance;
        const i32* a = lean[flip];
        const i32* b = lean[flip ^ 1u];
        i32* note = acc;
        for (i32* y = side + side_tail; y != side + side_tail + half_frames; ++y) {
            const i32 first = *a++, second = *b++;
            *note++ += first + second;
            first_lean += first_slide;
            second_lean += second_slide;
            const i32 d = (first >> 16) * first_lean - (second >> 16) * second_lean;
            *y = (d + (d >> 2)) >> 15;
        }
    }
    voice.width = width;
    // Nothing played and no tail of the last block left, the side's included:
    // the filter would give zeros.
    if (!rendered && !voice.leaned) {
        u32 tail = 0;
        for (u32 i = 0; i < 2 * halfband_taps; ++i) tail |= static_cast<u32>(low[i]);
        if (!tail) {
            for (u32 i = 0; i < 2 * opxy::block_frames; ++i) output[i] = 0;
            return;
        }
    }
    // Back to the full rate: each half-rate sample, then the half-band's
    // value between it and the next. The levels sum to at most 32767 (Q15),
    // so the bank stays within half scale (Q30 here); the filter's sums run
    // at half that.
    upsample(low, output);
    for (u32 i = 0; i < 2 * halfband_taps; ++i) low[i] = low[half_frames + i];
    // The two channels, last frame first, as they share the output with the
    // note. With no side in this block or the last (whose tail it would still
    // be playing), both are simply the note.
    if (!leaning && !voice.leaned) {
        for (u32 i = opxy::block_frames; i-- > 0;) output[2 * i] = output[2 * i + 1] = output[i];
        return;
    }
    if (!leaning) for (u32 i = 0; i < half_frames; ++i) side[side_tail + i] = 0;
    voice.leaned = leaning ? 1 : 0;
    for (u32 i = 0; i < side_tail; ++i) side[i] = voice.side[i];
    // The side, in step with the note: the note's half-band reads 8 half-rate
    // samples back, and so does this. A channel stops at half scale, as the note
    // does; its peaks stay far from it, so left plus right stays the note.
    const i32 top = 1 << 30;
    for (u32 j = half_frames; j-- > 0;) {
        const i32* y = side + j + side_tail - 8;
        const i32 note0 = output[2 * j], note1 = output[2 * j + 1];
        const i32 wide0 = y[0] << 15;
        const i32 wide1 = (y[0] + y[1]) * side_band[0] + (y[-1] + y[2]) * side_band[1] + (y[-2] + y[3]) * side_band[2];
        i32* out = output + 4 * j;
        out[0] = limit(note0 + wide0, top);
        out[1] = limit(note0 - wide0, top);
        out[2] = limit(note1 + wide1, top);
        out[3] = limit(note1 - wide1, top);
    }
    for (u32 i = 0; i < side_tail; ++i) voice.side[i] = side[half_frames + i];
}
}
