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
// The chain moves only as fast as a hand on a knob or the wingbeat's 0.2-0.6
// Hz, so a note solves its spectrum a stage a block, over eight blocks, and
// each partial ramps to where a solve lands it across the next eight; only
// the gate, which strikes in a millisecond, and the pitch follow every block.
namespace butterfly {
const u32 ceiling_fade = 0x29cbc14eu, ceiling = 0x2e709de5u;   // 7.2 and 8 kHz: the half-rate bank's top
const i32 lift = 51339;   // Q15: 1.567, the partials lifted 3.9 dB to the suites' shared loudness
// A partial sounds while it is within 36 dB of the block's loudest partial
// and above -72 dB of full scale (Q25), and once sounding until 6 dB under
// that, so one near the line doesn't flicker as the wingbeat moves it.
const i32 quiet = 1 << 13;
const u32 cull_shift = 6;
// The half-band filter's odd-phase taps, Q15 (a 27-tap Kaiser-windowed sinc,
// beta 5.5): flat to 8 kHz, its image of the bank 58 dB down above 14 kHz.
const i32 halfband[halfband_taps] = {20556, -6082, 2851, -1374, 597, -205, 38};

// The bank's and the half-bands' sample loops, functions of their own so the
// Blackfin compiler makes each a zero-overhead hardware loop (inlined into
// render's partial loop, the bank branched every sample).
// Each sample reads the sine the sample before addressed, so the Blackfin
// moves the index to an address register well ahead of its load. N samples:
// 32 at half rate, 16 at a quarter, 8 at an eighth.
template <u32 N>
__attribute__((noinline)) u32 bank(i32* acc, u32 ph, u32 step_phase, i32 g, i32 slide) {
    ph += step_phase;
    const opxy::i16* at = tables::sine + (ph >> (32 - tables::sine_bits));
    for (u32 i = 0; i < N; ++i) {
        const i32 s = *at;
        ph += step_phase;
        at = tables::sine + (ph >> (32 - tables::sine_bits));
        acc[i] += s * static_cast<opxy::i16>(static_cast<u32>(g) >> 10);
        g += slide;
    }
    return ph - step_phase;   // the phase after the block's last sample
}
inline i32 tap(const i32* x, i32 t, i32 h) {
    const i32 pair = (x[-t] >> 1) + (x[t + 1] >> 1);   // Q29
    return static_cast<opxy::i16>(static_cast<u32>(pair + 16384) >> 15) * static_cast<opxy::i16>(h);   // 16 bits of it, rounded (truncating would
                                                       // offset these samples: a tone at 22 kHz)
}
inline i32 tap(const i32* x, i32 t) { return tap(x, t, halfband[t]); }
// Back to the full rate: each half-rate sample, then the half-band's value
// between it and the next, its seven taps written out (a seven-step inner
// loop is left branching).
__attribute__((noinline)) void upsample(const i32* low, opxy::Output output) {
    for (u32 j = 0; j < half_frames; ++j) {
        const i32* x = low + j + halfband_taps - 1;
        const i32 between_samples = tap(x, 0) + tap(x, 1) + tap(x, 2) + tap(x, 3) + tap(x, 4) + tap(x, 5) + tap(x, 6);
        output[2 * j] = clamp(x[0], -(1 << 30), (1 << 30) - 1);
        output[2 * j + 1] = clamp(between_samples, -(1 << 29), (1 << 29) - 1) * 2;
    }
}
// The slower rates' stages: Sand's equiripple half-bands (images 72 dB down,
// flat within 0.0003 dB to 1.385 kHz from an eighth and to 2.076 kHz from a
// quarter of the rate), their odd-phase taps in Q15 and summed as tap() sums
// the half-band to the full rate. From n inputs after 2K - 1 of history,
// 2n outputs: each input in turn (K late) and the value between it and the next.
const i32 taps4[4] = {20017, -4749, 1356, -248}, taps3[3] = {19528, -3736, 600};
__attribute__((noinline)) void up_from_eighth(const i32* low, i32* out) {
    for (u32 j = 0; j < half_frames / 4; ++j) {
        const i32* x = low + j + 3;
        const i32 between = tap(x, 0, taps4[0]) + tap(x, 1, taps4[1]) + tap(x, 2, taps4[2]) + tap(x, 3, taps4[3]);
        out[2 * j] = x[0];
        out[2 * j + 1] = clamp(between, -(1 << 29), (1 << 29) - 1) * 2;
    }
}
__attribute__((noinline)) void up_from_quarter(const i32* low, i32* out) {
    for (u32 j = 0; j < half_frames / 2; ++j) {
        const i32* x = low + j + 2;
        const i32 between = tap(x, 0, taps3[0]) + tap(x, 1, taps3[1]) + tap(x, 2, taps3[2]);
        out[2 * j] = x[0];
        out[2 * j + 1] = clamp(between, -(1 << 29), (1 << 29) - 1) * 2;
    }
}
// The half-band to the full rate with the quarter stage's three taps about
// the same middle, for blocks no partial at the half rate reaches: what they
// hold is under 2.1 kHz, which these pass flat (to 4.15 kHz here, images
// 72 dB down), so either filter can take over from the other at a block.
__attribute__((noinline)) void upsample_low(const i32* low, opxy::Output output) {
    for (u32 j = 0; j < half_frames; ++j) {
        const i32* x = low + j + halfband_taps - 1;
        const i32 between_samples = tap(x, 0, taps3[0]) + tap(x, 1, taps3[1]) + tap(x, 2, taps3[2]);
        output[2 * j] = clamp(x[0], -(1 << 30), (1 << 30) - 1);
        output[2 * j + 1] = clamp(between_samples, -(1 << 29), (1 << 29) - 1) * 2;
    }
}
// A partial's rate, from its pitch (a full-rate increment): an eighth below
// 1.385 kHz, a quarter below 2.076 kHz, else a half, where those stages pass
// it flat. It moves to a slower rate again only 8% below those, so a partial
// at a boundary does not hop to and fro.
const u32 rate_up[2] = {202184855u, 134887295u}, rate_down[2] = {186010067u, 124096311u};
inline u32 rate_for(u32 now, u32 increment) {
    const u32 most = 3u - (increment >= rate_up[1]) - (increment >= rate_up[0]);     // the slowest that holds it
    const u32 least = 3u - (increment >= rate_down[1]) - (increment >= rate_down[0]);
    const u32 r = now > most ? most : now;
    return r < least ? least : r;
}
static_assert(halfband_taps == 7, "upsample writes out seven taps");

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
// What initialize() leaves alone a note's first block sets before reading
// it: the strike resets the phases, amplitudes, gate and wingbeat, and the
// whole reading (stale) lands every partial at once. So it clears only the
// stages' sums and the flags, and a voice's first block writes no more than
// it needs (the SDK has already cleared it on the OP-XY; a browser preview
// reuses one).
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    for (u32 i = 0; i < half_frames + 2 * halfband_taps; ++i) voice.low[i] = 0;
    for (u32 i = 0; i < eighth_history + half_frames / 4; ++i) voice.eighth[i] = 0;
    for (u32 i = 0; i < quarter_history + half_frames / 2; ++i) voice.quarter[i] = 0;
    for (u32 i = 0; i < quarter_wait; ++i) voice.quarter_late[i] = 0;
    for (u32 i = 0; i < half_wait; ++i) voice.half_late[i] = 0;
    for (u32 k = 0; k < partials; ++k) {
        voice.amplitude[k] = 0;
        voice.rate[k] = 3;
    }
    voice.high = 0;
    voice.strikes = owner * 0x2545f491u;
    voice.struck = voice.sounding = voice.hush = 0;
    voice.stage = voice.still = 0;
    voice.stale = 1;
}
// A note-on is the strike. There is no note-off: the gate holds while the
// note sounds, and the OP-XY's amp envelope ends it.
inline void start(Voice& voice, Shared&, opxy::u32) {
    voice.struck = 1;
    voice.hush = voice.sounding;   // a voice still ringing fades out in one block first
}
inline void destroy(Voice&, Shared&, opxy::u32) {}

// ---------------------------------------------------------------- the solve

const u32 solve_stages = 8, ramp_shift = 3;   // a solve every 8 blocks (11.6 ms), each landing ramped over 8

// Its first stage: the controls as they are now, the chain's place on the
// tables' grid, and the first states.
inline void solve_start(Voice& voice, const i32* p) {
    // The flux, snapped and beating, the coupling, the struck site, beating on its own phase.
    const u32 alpha = warp(flux(p[FLUX]), snap_pull) + static_cast<u32>(wingbeat_flux(p[WINGBEAT], voice.beat));
    voice.lambda = coupling(p[COUPLING]);
    voice.at = clamp(site(p[SITE]) + wingbeat_site(p[WINGBEAT], voice.beat_site), 0, 8 << 16);
    voice.grid = grid(alpha, voice.lambda);
    voice.octaves = static_cast<i32>(exp2_q16(spread_log2(p[SPREAD])));   // the span, octaves, Q16
    voice.slope = tilt(p[TILT]);
    voice.makeup = tilt_makeup(voice.slope);
    voice.sampled[0] = p[FLUX];
    voice.sampled[1] = p[COUPLING];
    voice.sampled[2] = p[SITE];
    voice.sampled[3] = p[SPREAD];
    voice.sampled[4] = p[TILT];
}

// A value's step a block to reach its target in 2^ramp_shift blocks; within
// a few steps of it, the value takes the target at once.
inline void aim(i32& value, i32& step, i32 target, bool at_once) {
    const i32 gap = target - value;
    if (at_once || (gap > -8 && gap < 8)) {
        value = target;
        step = 0;
    } else {
        step = (gap + (1 << (ramp_shift - 1))) >> ramp_shift;
    }
}

// The pitch increment of a partial at `ratio` (Q16) to the note's, Q32 a
// sample, held at the half-rate bank's ceiling at or above it.
inline u32 increment_of(u32 note, u32 ratio) {
    if ((note >> 16) * (ratio >> 16) >= (ceiling >> 16)) return ceiling;   // and mul16u can't overflow below it
    const u32 i = mul16u(note, ratio);
    return i < ceiling ? i : ceiling;
}

// Partials from..to-1 land: their pitch (the note's, 2^(octaves x place)
// times), level, and the gate's power and darkening at their place, as
// targets to ramp to.
// (Each stage's functions are out of line, one copy each, so the solve's
// code stays small beside the bank's.)
__attribute__((noinline)) void land(Voice& voice, u32 from, u32 to, bool at_once) {
    // Under 60 dB beneath the loudest, a partial is silent, whatever the gate and tilt make of the rest.
    const i32 least = voice.loudest >> 10;
    for (u32 k = from; k < to; ++k) {
        const i32 unlifted = voice.notes.level[k];
        const i32 level = unlifted > least ? (unlifted * lift) >> 15 : 0;   // lifted 3.9 dB to the suites' shared loudness
        if (!level && voice.level[k] < 8 && voice.level[k] > -8) {
            // Silent and staying so.
            voice.level[k] = 0;
            voice.increment_step[k] = voice.level_step[k] = voice.power_step[k] = voice.dim_step[k] = 0;
            continue;
        }
        // Its pitch, 2^(octaves x place) times the note's. The products here
        // keep 13 to 15 bits of each side, within a hundredth of a cent and
        // a thousandth of a dB, without mul16's four multiplies.
        const i32 place = voice.notes.place[k];   // Q16, -0.1 to 1.1
        u32 pm;
        i32 pw;
        exp2_parts(((voice.octaves >> 3) * (place >> 2)) >> 11, pm, pw);   // octaves 1/4 to 4, Q16; pm: Q15, 1 to 2
        const i32 up = pw + 1;
        const u32 ratio = up <= 0 ? pm >> clamp(-up, 0, 31) : pm << clamp(up, 0, 15);   // Q16
        voice.ratio[k] = ratio;
        // The gate's power and the darkening, as partial_share (harper.h) has them.
        const i32 tp = ((voice.slope >> 2) * ((place > 0 ? place : 0) >> 2)) >> 12;   // tilt x place, Q16 (tilt to 3)
        const i32 above = place > 16384 ? (((place - 16384) >> 2) * 87381) >> 14 : 0;
        const i32 dim = ((((voice.slope >> 2) * (above >> 2)) >> 12) * 85 >> 6) - voice.makeup;
        // A partial coming in from silence takes its pitch and tilt at once and ramps its level in.
        const bool snap = at_once || !voice.level[k];
        i32 increment = static_cast<i32>(voice.increment[k]);
        aim(increment, voice.increment_step[k], static_cast<i32>(increment_of(voice.note, ratio)), snap);
        voice.increment[k] = static_cast<u32>(increment);
        aim(voice.power[k], voice.power_step[k], (65536 + tp) >> 4, snap);
        aim(voice.dim[k], voice.dim_step[k], dim, snap);
        aim(voice.level[k], voice.level_step[k], level, at_once);
    }
}

__attribute__((noinline)) void solve_states(Voice& voice, u32 from, u32 to) { spectrum_states(voice.grid, voice.at, from, to, voice.solving); }
__attribute__((noinline)) void solve_places(Voice& voice) { voice.sum = shape_places(voice.solving, voice.lambda, voice.notes); }
__attribute__((noinline)) void solve_levels(Voice& voice) {
    shape_levels(voice.solving, voice.sum, voice.notes);
    i32 loudest = 0;
    for (u32 k = 0; k < partials; ++k) loudest = voice.notes.level[k] > loudest ? voice.notes.level[k] : loudest;
    voice.loudest = loudest;
}

// One stage of the solve; the eight in turn are spectrum(), shape() and the landing.
__attribute__((noinline)) void solve_stage(Voice& voice, const i32* p, u32 stage, bool at_once) {
    switch (stage) {
    case 0:
        solve_start(voice, p);
        solve_states(voice, 0, 9);
        break;
    case 1:
        solve_states(voice, 9, partials);
        voice.solving.key = spectrum_key(voice.grid);
        break;
    case 2: solve_places(voice); break;
    case 3: solve_levels(voice); break;
    case 4: land(voice, 0, 5, at_once); break;
    case 5: land(voice, 5, 9, at_once); break;
    case 6: land(voice, 9, 13, at_once); break;
    default: land(voice, 13, partials, at_once); break;
    }
}

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
            voice.rate[k] = 3;
        }
        voice.gate = 0;
        voice.blocks = 0;
        voice.beat = voice.beat_site = 0;
        voice.struck = 0;
        voice.stale = 1;
    }

    if (!hushing) {
        voice.gate = gate_step(voice.gate, voice.blocks, strike(p[STRIKE]), glow_log2(p[GLOW]));
        if (voice.blocks < 0xffffu) ++voice.blocks;
    }
    const u32 light = hushing ? 0 : static_cast<u32>(voice.gate) >> 15;   // Q15; 0: every partial's target is
    // A new pitch (a bend or a glide) moves every partial at once, to where it last landed.
    const u32 note = pitch[0];
    if (note != voice.note && !voice.stale) {
        voice.note = note;
        for (u32 k = 0; k < partials; ++k) {
            voice.increment[k] = increment_of(note, voice.ratio[k]);
            voice.increment_step[k] = 0;
        }
    }

    // The solve: whole at the strike, then a stage a block. A dark gate
    // silences every partial whatever the chain, so the solve waits, and
    // runs whole again if the gate lights (a slower glow, a lighter strike).
    if (!light) {
        voice.stale = 1;
    } else if (voice.stale) {
        voice.note = note;
        for (u32 s = 0; s < solve_stages; ++s) solve_stage(voice, p, s, true);
        voice.stale = 0;
        voice.stage = 0;
        voice.still = 0;
    } else {
        bool run = true;
        if (voice.stage == 0) {
            // Solve again only while something moves: a control, or the wingbeat.
            const bool moved = p[FLUX] != voice.sampled[0] || p[COUPLING] != voice.sampled[1] || p[SITE] != voice.sampled[2] ||
                               p[SPREAD] != voice.sampled[3] || p[TILT] != voice.sampled[4] || q15(p[WINGBEAT]) != 0;
            voice.still = moved ? 0 : voice.still < 2 ? voice.still + 1 : 2;
            run = voice.still < 2;   // a solve after the last move lands its targets exactly; then rest
        }
        if (run) {
            solve_stage(voice, p, voice.stage, false);
            voice.stage = (voice.stage + 1) & (solve_stages - 1);
        }
    }
    const u32 rate = wingbeat_rate(p[WINGBEAT]);
    voice.beat += rate;
    voice.beat_site += wingbeat_site_rate(rate);

    bool sounding = false, rendered = false;
    i32* low = voice.low;
    i32* eighth = voice.eighth + eighth_history;   // the sums at each rate this block
    i32 quarter_sum[half_frames / 2], half_sum[half_frames];
    bool slow = false;   // any partial at a quarter or an eighth
    for (u32 i = 0; i < half_frames / 4; ++i) eighth[i] = 0;
    for (u32 i = 0; i < half_frames / 2; ++i) quarter_sum[i] = 0;
    for (u32 i = 0; i < half_frames; ++i) half_sum[i] = 0;
    if (light || voice.sounding) {   // dark and every partial silent: no targets, no bank
        // Each partial's target: its level through the gate at its power and
        // darkening, faded at the ceiling.
        const u32 e = light ? static_cast<u32>(-gate_log2(light)) >> 4 : 0;   // -log2 of the light, Q12
        i32 target[partials];
        i32 loudest = 0;
        for (u32 k = 0; k < partials; ++k) {
            const u32 inc = voice.increment[k] += static_cast<u32>(voice.increment_step[k]);
            const i32 level = voice.level[k] += voice.level_step[k];
            const i32 power = voice.power[k] += voice.power_step[k];
            const i32 dim = voice.dim[k] += voice.dim_step[k];
            i32 t = 0;
            if (light && level > 0 && inc < ceiling) {
                const i32 gain = inc <= ceiling_fade ? 32768 : static_cast<i32>((((ceiling - inc) >> 16) * 112978u) >> 12);   // 32768 / 1188
                const i32 share = exp2_down(-static_cast<i32>((e * static_cast<u32>(power)) >> 8) - dim);   // Q15
                t = ((((level * share) >> 15) * gain) >> 15) << 10;   // Q25
                if (t > loudest) loudest = t;
            }
            target[k] = t;
        }
        const i32 line = loudest >> cull_shift > quiet ? loudest >> cull_shift : quiet;
        for (u32 k = 0; k < partials; ++k) {
            const i32 a = voice.amplitude[k];
            const i32 target_k = target[k] >= (a ? line >> 1 : line) ? target[k] : 0;
            voice.amplitude[k] = target_k;
            if (!target_k && !a) continue;
            rendered = true;
            if (target_k) sounding = true;
            // The bank: the block's samples at the partial's rate, the amplitude ramped across them.
            const u32 inc = voice.increment[k], rate = rate_for(voice.rate[k], inc);
            voice.rate[k] = static_cast<u8>(rate);
            if (rate == 3) voice.phase[k] = bank<half_frames / 4>(eighth, voice.phase[k], inc << 3, a, (target_k - a) >> 3);
            else if (rate == 2) voice.phase[k] = bank<half_frames / 2>(quarter_sum, voice.phase[k], inc << 2, a, (target_k - a) >> 4);
            else voice.phase[k] = bank<half_frames>(half_sum, voice.phase[k], inc << 1, a, (target_k - a) >> 5);
            slow = slow || rate > 1;
            if (rate == 1) voice.high = 3;   // it reaches the full-rate stage's window for three blocks
        }
    }
    voice.sounding = sounding ? 1 : 0;
    if (hushing) {
        voice.hush = 0;
        voice.sounding = 0;
    }
    // What the stages still hold from the last blocks.
    u32 slow_tail = 0, tail = 0;
    for (u32 i = 0; i < eighth_history; ++i) slow_tail |= static_cast<u32>(voice.eighth[i]);
    for (u32 i = 0; i < quarter_history; ++i) slow_tail |= static_cast<u32>(voice.quarter[i]);
    for (u32 i = 0; i < quarter_wait; ++i) slow_tail |= static_cast<u32>(voice.quarter_late[i]);
    for (u32 i = 0; i < 2 * halfband_taps; ++i) tail |= static_cast<u32>(low[i]);
    for (u32 i = 0; i < half_wait; ++i) tail |= static_cast<u32>(voice.half_late[i]);
    // Nothing played and nothing left in the stages: they would give zeros.
    if (!rendered && !slow_tail && !tail) {
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    // The slower rates up to the half rate, each faster sum joining when the
    // slower ones have caught up with it, into the half-band to the full rate.
    i32* to_half = low + 2 * halfband_taps;
    if (slow || slow_tail) {
        i32* q = voice.quarter + quarter_history;
        up_from_eighth(voice.eighth, q);
        for (u32 i = 0; i < quarter_wait; ++i) {
            q[i] += voice.quarter_late[i];
            q[quarter_wait + i] += quarter_sum[i];
            voice.quarter_late[i] = quarter_sum[quarter_wait + i];
        }
        up_from_quarter(voice.quarter, to_half);
        for (u32 i = 0; i < eighth_history; ++i) voice.eighth[i] = voice.eighth[half_frames / 4 + i];
        for (u32 i = 0; i < quarter_history; ++i) voice.quarter[i] = voice.quarter[half_frames / 2 + i];
    } else {
        for (u32 i = 0; i < half_frames; ++i) to_half[i] = 0;
    }
    for (u32 i = 0; i < half_wait; ++i) to_half[i] += voice.half_late[i];
    for (u32 i = half_wait; i < half_frames; ++i) to_half[i] += half_sum[i - half_wait];
    for (u32 i = 0; i < half_wait; ++i) voice.half_late[i] = half_sum[half_frames - half_wait + i];
    // Back to the full rate: each half-rate sample, then the half-band's
    // value between it and the next. The levels sum to at most 32767 (Q15),
    // 1.567 x that once lifted, so the bank stays within 0.78 of full scale
    // (Q30 here) and is held within half scale; the filter's sums run at half
    // that. Spread phases keep a played note's peaks near 0.4.
    if (voice.high) {
        upsample(low, output);
        --voice.high;
    } else {
        upsample_low(low, output);
    }
    for (u32 i = 0; i < 2 * halfband_taps; ++i) low[i] = low[half_frames + i];
}
}
