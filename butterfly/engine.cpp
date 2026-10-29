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
namespace butterfly {
const u32 ceiling_fade = 0x29cbc14eu, ceiling = 0x2e709de5u;   // 7.2 and 8 kHz: the half-rate bank's top
const i32 silence = 1 << 10;        // Q25: below this a partial is skipped
// The half-band filter's odd-phase taps, Q15 (a 27-tap Kaiser-windowed sinc,
// beta 5.5): flat to 8 kHz, its image of the bank 58 dB down above 14 kHz.
const i32 halfband[halfband_taps] = {20556, -6082, 2851, -1374, 597, -205, 38};

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    for (u32 k = 0; k < partials; ++k) {
        voice.phase[k] = 0;
        voice.amplitude[k] = 0;
    }
    for (u32 i = 0; i < half_frames + 2 * halfband_taps; ++i) voice.low[i] = 0;
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
    bool sounding = false;
    i32* low = voice.low;
    i32* acc = low + 2 * halfband_taps;
    for (u32 i = 0; i < half_frames; ++i) acc[i] = 0;
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
        if (!hushing && level > 0 && gain > 0)
            target = ((((level * partial_gate(voice.gate, slope, place)) >> 15) * gain) >> 15) << 10;   // Q25
        voice.amplitude[k] = target < silence ? 0 : target;
        const u32 step_phase = increment << 1;   // a half-rate sample
        u32 ph = voice.phase[k];
        if (target < silence && a < silence) {
            voice.phase[k] = ph + (step_phase << 5);
            continue;
        }
        if (target >= silence) sounding = true;
        // The bank: 32 samples at half rate, the amplitude ramped across them.
        i32 g = a;
        const i32 slide = (target - a) >> 5;
        for (u32 i = 0; i < half_frames; ++i) {
            ph += step_phase;
            acc[i] += tables::sine[ph >> (32 - tables::sine_bits)] * (g >> 10);
            g += slide;
        }
        voice.phase[k] = ph;
    }
    voice.sounding = sounding ? 1 : 0;
    if (hushing) {
        voice.hush = 0;
        voice.sounding = 0;
    }
    // Back to the full rate: each half-rate sample, then the half-band's
    // value between it and the next. The levels sum to at most 32767 (Q15),
    // so the bank stays within half scale (Q30 here); the filter's sums run
    // at half that.
    for (u32 j = 0; j < half_frames; ++j) {
        const i32* x = low + j + halfband_taps - 1;
        i32 between_samples = 0;
        for (u32 t = 0; t < halfband_taps; ++t) {
            const i32 pair = (x[-static_cast<i32>(t)] >> 1) + (x[t + 1] >> 1);   // Q29
            between_samples += ((pair + 16384) >> 15) * halfband[t];             // 16 bits of it, rounded (truncating would
                                                                                 // offset these samples: a tone at 22 kHz)
        }
        output[2 * j] = x[0];
        output[2 * j + 1] = clamp(between_samples, -(1 << 29), (1 << 29) - 1) * 2;
    }
    for (u32 i = 0; i < 2 * halfband_taps; ++i) low[i] = low[half_frames + i];
}
}
