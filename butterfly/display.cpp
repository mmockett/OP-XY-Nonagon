#include "display.h"

// Butterfly's screen: a pinned swallowtail, Moth's neighbour in the drawer,
// drawn in Moth's line. Each wing is the chain, in two zones.
//
// Its breadth, from the body out to the submarginal band, is the chain from
// its middle site to its end. Three pigment bands, at sites 0, 4 and 8, are
// dark or pale with the strike's density there: even below lambda = 1, where every
// state spreads, and gathered at the struck site above it, where they
// localize. The localized states are eyespots, at their sites and energies.
// The lit band lies at the struck site and rings with the gate: one thin
// line at no strike, a broad band between two bright edges at full strike,
// and on it a row of lunules, filled scales, pinpricks at no strike and
// bright 6 px spots at full.
//
// Outside it the margin is the spectrum, read from the forewing's leading
// edge (the top) round to the hindwing's tail (the bottom): each band of the
// butterfly swells a lobe and each gap cuts a notch, so flux reshapes it.
// Tilt sweeps the forewing back over the hindwing, as it darkens the top of
// the spectrum; glow draws a swallowtail's tail out of the hindwing, none at
// 40 ms and long at 8 s, as it draws out the ring. Spread is the wingspan;
// the wingbeat breathes the wings while a note sounds; strike widens the lit
// band and stands the antennae up, short and folded back at none, long and
// upright at full, and a strike flicks them.
//
// The wings are the chain's two halves, mirror images as the chain is. The
// screen reads the tables as the engine does (harper.h): a solve costs about
// two engine blocks (13,000 to 23,000 instructions), at most eight times a
// second. Integers only.
namespace butterfly {
namespace screen {
using namespace opxy::draw;

const i32 frame_gap = 128;        // Q10 seconds: the wings are solved at most eight times a second
const i32 flick_time = 205;       // Q10 seconds: the antennae flick out and back
const i32 sweep_per_tilt = 70;    // tenths of a degree the forewing sweeps back a unit of tilt (21 degrees at 3)
const i32 tail_longest = 40;      // px: the tail at the longest glow, while the screen's foot allows
const i32 foot = 88;              // px below the centre the tail may reach (212, its strap and stroke within 216)
const i32 lunule_small = 640;     // Q10 of the artwork's 3 px radius: the smallest lunule shown (2 at full)
const i32 lit_thin = 197, lit_broad = 2294;   // the lit band's half width at no strike and full, Q15 of the reach (0.006, 0.07)
const i32 block_q16 = 95;         // a block, Q16 seconds
const i32 bump = 3932;            // a state's lobe spans 0.12 of the spectrum either way, Q15
const i32 lobe_low = 23593;       // the margin's reach at a gap and at a band, Q15: 0.72 to 1
const i32 floor_reach = 22282;    // and never inside 0.68, clear of the veins and bands (0.66)
enum { VEIN = 8, EDGE = 12, LIT_REST = 6, ANTENNA = 12, BAND_PALE = 5, BAND_DARK = 11 };
// ---------------------------------------------------------------- captions

// Decimal digits by subtraction (no division on the device): at least `width` of them.
inline u32 digits(char* out, u32 value, u32 width) {
    static const u32 powers[7] = {1000000, 100000, 10000, 1000, 100, 10, 1};
    u32 n = 0;
    for (u32 p = 0; p < 7; ++p) {
        char d = '0';
        while (value >= powers[p]) {
            value -= powers[p];
            ++d;
        }
        if (n || d != '0' || 7 - p <= width) out[n++] = d;
    }
    out[n] = 0;
    return n;
}
// `value` with its last `places` digits after a point.
inline void decimal(char* out, u32 value, u32 places) {
    char t[12];
    const u32 n = digits(t, value, places + 1);
    u32 at = 0;
    for (u32 i = 0; i < n; ++i) {
        if (i == n - places) out[at++] = '.';
        out[at++] = t[i];
    }
    out[at] = 0;
}
inline void append(char* out, const char* tail) {
    u32 at = 0;
    while (out[at]) ++at;
    for (u32 i = 0; tail[i] && at + 1 < header_capacity; ++i) out[at++] = tail[i];
    out[at] = 0;
}
// Flux as its fraction when it rests on one, else to three places.
inline void flux_caption(char* out, u32 alpha) {
    const i32 at = resting(alpha);
    if (at >= 0) {
        u32 n = digits(out, tables::numerator[at], 1);
        if (tables::denominator[at] > 1) {
            out[n++] = '/';
            digits(out + n, tables::denominator[at], 1);
        }
        return;
    }
    const u32 v = static_cast<u32>(((alpha >> 12) * 1000u + (1u << 19)) >> 20);   // thousandths
    if (v >= 1000) {
        copy(out, header_capacity, "1");
        return;
    }
    out[0] = '.';
    digits(out + 1, v, 3);
}
// Glow: "40ms" to "990ms", then "1.0s" to "8.0s".
inline void glow_caption(char* out, i32 glow) {
    const u32 ms = (exp2_q16(glow + 653083) + 32768u) >> 16;   // seconds x 1000 (log2 1000, Q16)
    if (ms < 995) {
        digits(out, ms, 1);
        append(out, "ms");
    } else {
        decimal(out, mul16u(ms + 50, 656), 1);   // tenths of a second
        append(out, "s");
    }
}
// Site: "mid", the sites out from it, "end".
inline void site_caption(char* out, i32 at) {
    const u32 d = static_cast<u32>((at + 32768) >> 16);
    if (!d) copy(out, header_capacity, "mid");
    else if (d >= 8) copy(out, header_capacity, "end");
    else digits(out, d, 1);
}


// ---------------------------------------------------------------- the wings

inline u32 next_key(u32 key) { return key + 1 ? key + 1 : 1; }

// A point of the left wing's margin at `reach` (Q12), its forewing swept
// back about the root by `sweep` tenths of a degree (`back`, its turn), Q10
// px from the centre.
inline void swept(u32 i, i32 reach, i32 sweep, Turn back, i32& x, i32& y) {
    x = (plate::wing_x[i] >> 4) * reach >> 8;
    y = (plate::wing_y[i] >> 4) * reach >> 8;
    const i32 f = plate::forewing[i];
    if (!f || !sweep) return;
    const Turn r = f >= 32767 ? back : rotation(-((sweep * (f >> 5)) >> 10));
    const i32 rx = x - plate::pivot_x * unit;
    x = plate::pivot_x * unit + ((r.cos * rx - r.sin * y) >> 10);
    y = (r.sin * rx + r.cos * y) >> 10;
}

// A left-wing point (Q10 px from the centre) as an outline point for the
// wing at `scale` (Q15) about its root, from the wing's middle as it lies at
// that scale, before halving (the node is drawn at scale 2, as Moth's).
inline void scaled(i32& x, i32& y, i32 scale) {
    const i32 middle = -plate::wing_middle * unit, root = plate::pivot_x * unit;
    x = (((x - root) >> 5) * (scale >> 5) >> 5) - (((middle - root) >> 5) * (scale >> 5) >> 5);
    y = (y >> 5) * (scale >> 5) >> 5;
}

inline void finish(Frame& frame, u32 path, u32 side, u32 i, i32 x, i32 y, bool& changed) {
    Frame::path_type& out = frame.paths[path];
    x >>= 1;
    y >>= 1;
    if (side) x = -x;
    changed = changed || out.points[i].x != x || out.points[i].y != y;
    out.points[i].x = x;
    out.points[i].y = y;
}

inline void seal(Frame& frame, DisplayState& state, u32 path, bool changed) {
    if (changed || !state.keys[path]) state.keys[path] = next_key(state.keys[path]);
    frame.paths[path].key = state.keys[path];
}

// A wing's margin at the reaches (Q12), one a margin point: the root's two
// points and the margin, swept and scaled, with the tail drawn out `tail` px
// (Q10) along its direction, as far as the screen's foot allows.
inline void margin_outline(Frame& frame, DisplayState& state, u32 side, const i32* reaches, i32 scale, i32 sweep, i32 tail) {
    const Turn back = rotation(-sweep);
    i32 tx, ty;
    swept(plate::tail_tip, reaches[plate::tail_tip], sweep, back, tx, ty);
    scaled(tx, ty, scale);
    const i32 room = (foot << 10) - ty;
    const i32 most = room > 0 ? (room * 1103) >> 10 : 0;   // room / tail_dy
    const i32 length = tail < most ? tail : most;
    bool changed = false;
    for (u32 i = 0; i < 64; ++i) {
        i32 x, y;
        if (i == 0 || i == 63) {
            x = plate::pivot_x * unit;
            y = i ? 13 * unit : -13 * unit;
            scaled(x, y, scale);
        } else {
            swept(i - 1, reaches[i - 1], sweep, back, x, y);
            scaled(x, y, scale);
            const i32 t = plate::tail[i - 1];
            if (t && length > 0) {
                const i32 d = ((length >> 5) * (t >> 5)) >> 5;   // Q10 px
                x += (d * plate::tail_dx) >> 10;
                y += (d * plate::tail_dy) >> 10;
            }
        }
        finish(frame, side, side, i, x, y, changed);
    }
    seal(frame, state, side, changed);
}

// A wing's lit band: a ribbon `width` (Q15 of the reach) either side of
// `share`, out along every other margin point and back, swept and scaled.
// Mutable outlines take no fill, so a broad band is its two bright edges,
// with the lunules between them.
inline void lit_outline(Frame& frame, DisplayState& state, u32 side, i32 share, i32 width, i32 scale, i32 sweep) {
    const Turn back = rotation(-sweep);
    const i32 outer = (share + width) >> 3, inner = (share - width) >> 3;
    bool changed = false;
    for (u32 n = 0; n < 64; ++n) {
        const u32 i = n < 31 ? 2 * n : n == 31 ? 61 : n < 63 ? 61 - 2 * (n - 32) : 0;
        i32 x, y;
        swept(i, n < 32 ? outer : inner, sweep, back, x, y);
        scaled(x, y, scale);
        finish(frame, 2 + side, side, n, x, y, changed);
    }
    seal(frame, state, 2 + side, changed);
}

// The margin at share u (Q15) of its length, Q10 px from the centre (left wing).
inline void margin_at(i32 u, i32& x, i32& y) {
    const i32 f = u * static_cast<i32>(plate::margin - 1);   // Q15
    u32 i = static_cast<u32>(f >> 15);
    if (i >= plate::margin - 1) i = plate::margin - 2;
    const i32 w = f - static_cast<i32>(i << 15);
    x = plate::wing_x[i] + (((plate::wing_x[i + 1] - plate::wing_x[i]) >> 5) * (w >> 5) >> 5);
    y = plate::wing_y[i] + (((plate::wing_y[i + 1] - plate::wing_y[i]) >> 5) * (w >> 5) >> 5);
}

// A distance from the chain's middle (Q8 sites, 0..8) as a share of the margin's reach, Q15.
inline i32 site_share(i32 d) { return plate::site_near + (((plate::site_far - plate::site_near) * clamp(d, 0, 8 << 8)) >> 11); }

// A point of the left wing (Q10 px from the centre) on screen, for the wing
// at `scale` (Q15) about its root.
inline void on_screen(u32 side, i32 x, i32 y, i32 scale, i32& sx, i32& sy) {
    i32 rx = x - plate::pivot_x * unit;
    if (side) rx = -rx;
    const i32 px = (plate::centre_x + (side ? -plate::pivot_x : plate::pivot_x)) * unit;
    sx = px + ((rx >> 5) * (scale >> 5) >> 5);
    sy = plate::centre_y * unit + ((y >> 5) * (scale >> 5) >> 5);
}

// The wings for the chain at alpha, lambda, struck at `at` (Q16 sites): the
// margin's reaches, the bands' shades, the eyespots (the four most localized
// states, over at most three sites) and the lit band's place.
inline void draw_wings(DisplayState& state, u32 alpha, i32 lambda, i32 at) {
    Spectrum s;
    spectrum(alpha, lambda, at, s);
    Notes notes;
    shape(s, lambda, notes);
    const i32 to_unit = reciprocal(((2 << 12) + 2 * lambda) << 4);   // 1 / (2 + 2 lambda), Q16
    i32 e[partials], lobe[partials];
    i32 loudest = 1;
    for (u32 k = 0; k < partials; ++k) loudest = loudest > notes.level[k] ? loudest : notes.level[k];
    const i32 per_loudest = reciprocal(loudest << 16);
    for (u32 k = 0; k < partials; ++k) {
        e[k] = clamp(mul16(s.energy[k] * 8, to_unit), -32768, 32767);   // -1..1 across the spectrum, Q15
        lobe[k] = 16384 + (mul16(notes.level[k] << 16, per_loudest) >> 2);   // 0.5 to 1 as it is loud, Q15
    }
    // The margin: each state swells it where its energy falls along it,
    // smoothed, so a band's states make one lobe and a gap a notch.
    i32 swell[plate::margin];
    for (u32 i = 0; i < plate::margin; ++i) {
        const i32 x = 32767 - static_cast<i32>(i) * 1074;   // +1 at the leading edge to -1 at the tail, Q15
        i32 d = 0;
        for (u32 k = 0; k < partials; ++k) {
            const i32 off = x - e[k];
            if (off <= -bump || off >= bump) continue;
            const i32 t = mul16(off * 2, reciprocal(bump << 1));    // -1..1, Q16
            const i32 c = 32768 - ((t >> 1) * (t >> 1) >> 15);       // 1 - t^2, Q15
            d += (((c * c) >> 15) * lobe[k]) >> 15;
        }
        swell[i] = d > 32767 ? 32767 : d;
    }
    for (u32 pass = 0; pass < 3; ++pass) {
        i32 before = swell[0];
        for (u32 i = 0; i < plate::margin; ++i) {
            const i32 here = swell[i], after = i + 1 < plate::margin ? swell[i + 1] : here;
            swell[i] = (before + 2 * here + after) >> 2;
            before = here;
        }
    }
    for (u32 i = 0; i < plate::margin; ++i) {
        const i32 r = lobe_low + (((32768 - lobe_low) * swell[i]) >> 15);
        state.reach[i] = (r < floor_reach ? floor_reach : r) >> 3;   // Q12
    }
    // The bands: the strike's density at sites 0, 4 and 8, the sum
    // over states of how much the strike excites each and how much of it
    // lives there; the densest is darkest.
    const Grid g = grid(alpha, lambda);
    i32 density[plate::bands_a_wing], densest = 1;
    for (u32 b = 0; b < plate::bands_a_wing; ++b) {
        i32 sum = 0;
        for (u32 k = 0; k < partials; ++k) {
            const i32 a = amplitude_at(g, k, plate::band_step * b) >> 1;   // Q14
            sum += (s.weight[k] >> 2) * ((a * a) >> 14) >> 14;           // Q14 x Q14 >> 14
        }
        density[b] = sum;
        densest = densest > sum ? densest : sum;
    }
    const i32 per_densest = reciprocal(densest << 2);   // Q16 of 1 / (Q14 >> -2)
    for (u32 b = 0; b < plate::bands_a_wing; ++b) {
        const i32 x = clamp(mul16(density[b] << 2, per_densest), 0, 65536);   // Q16
        const i32 lifted = x * 2 - ((x >> 1) * (x >> 1) >> 14);              // 2x - x^2: a little density shows
        state.band_shade[b] = static_cast<u32>(BAND_PALE + (((BAND_DARK - BAND_PALE) * lifted) >> 16));
    }
    // The eyespots: the most localized states, at their energy along the
    // margin and their distance from the chain's middle out from the root.
    i32 centre[partials], spread[partials];
    for (u32 k = 0; k < partials; ++k) extent(alpha, lambda, k, centre[k], spread[k]);
    bool used[partials] = {};
    for (u32 j = 0; j < plate::eyes_a_wing; ++j) {
        u32 best = partials;
        for (u32 k = 0; k < partials; ++k) {
            if (used[k] || spread[k] > (3 << 8)) continue;   // localized: over at most three sites
            if (best == partials || spread[k] < spread[best]) best = k;
        }
        if (best == partials) {
            state.spot_state[j] = 0;
            continue;
        }
        used[best] = true;
        i32 mx, my;
        const i32 u = (32767 - e[best]) >> 1;
        margin_at(u, mx, my);
        const u32 near = static_cast<u32>((u * static_cast<i32>(plate::margin - 1) + 16384) >> 15);
        state.spot_fore[j] = plate::forewing[near < plate::margin ? near : plate::margin - 1];
        const i32 f = site_share(centre[best]);
        state.spot_x[j] = (mx >> 5) * (f >> 5) >> 5;
        state.spot_y[j] = (my >> 5) * (f >> 5) >> 5;
        state.spot_scale[j] = clamp(1638 - ((spread[best] * 123) >> 8), 1024, 1638);   // 1.6 to 1, Q10
        state.spot_light[j] = mul16(notes.level[best] << 16, per_loudest) >> 1;          // Q15
        state.spot_state[j] = 1;
    }
    state.lit_share = site_share(at >> 8);
}

// One wing's nodes at the wing's pose: its margin and lit band, veins,
// bands and eyespots, the forewing's swept back with it.
inline void place_wing(Frame& frame, DisplayState& state, u32 side, i32 scale, i32 light, i32 struck) {
    const i32 seen = light * 2 - ((light * light) >> 15);   // light (2 - light): a fading ring stays in view
    const i32 sweep = state.sweep;
    margin_outline(frame, state, side, state.reach, scale, sweep, state.tail);
    lit_outline(frame, state, side, state.lit_share, state.lit_width, scale, sweep);
    i32 x, y;
    on_screen(side, -plate::wing_middle * unit, 0, scale, x, y);
    place(frame, plate::edges + side, x, y, 2 * unit, EDGE, stroke);
    // The lit band: brighter as the strike is harder, and brighter still as it rings.
    const i32 ring_gain = 13107 + ((19661 * struck) >> 15);   // 0.4 to 1, Q15
    const i32 lit_shade = LIT_REST + ((4 * struck) >> 15) + ((5 * ((seen * ring_gain) >> 15)) >> 15);
    place(frame, plate::edges + 2 + side, x, y, 2 * unit, static_cast<u32>(lit_shade > 15 ? 15 : lit_shade), stroke);
    // The lunules on the lit band, as large as it is broad, and smaller as
    // it lies nearer the body (0.6 of their size at the root, all at the end);
    // none below an eighth of the strike's breadth, so never a 1 px dot.
    const Turn back = rotation(-sweep);
    const bool beads = state.lit_width - lit_thin >= (lit_broad - lit_thin) >> 3;
    const i32 near = 19661 + ((state.lit_share * 20480) >> 15);   // Q15
    const i32 bead = ((lunule_small + (((state.lit_width - lit_thin) * 683) >> 10)) * near) >> 15;   // Q10, to 2
    for (u32 j = 0; j < plate::lunules_a_wing; ++j) {
        if (!beads) {
            hide(frame, plate::lunules + side * plate::lunules_a_wing + j);
            continue;
        }
        i32 lx, ly;
        swept(plate::lunule_at[j], state.lit_share >> 3, sweep, back, lx, ly);
        on_screen(side, lx, ly, scale, x, y);
        place(frame, plate::lunules + side * plate::lunules_a_wing + j, x, y, clamp((bead * (scale >> 5)) >> 10, 1, 2 * unit),
              static_cast<u32>(lit_shade > 15 ? 15 : lit_shade), fill_and_stroke);
    }
    const Turn fore = rotation(side ? sweep : -sweep);
    on_screen(side, plate::pivot_x * unit, 0, scale, x, y);
    for (u32 half = 0; half < 2; ++half) {
        place(frame, plate::veins + 2 * side + half, x, y, scale >> 5, VEIN, fill_and_stroke);
        if (!half) turn(frame, plate::veins + 2 * side, fore);
        for (u32 b = 0; b < plate::bands_a_wing; ++b) {
            const u32 n = plate::bands + (2 * side + half) * plate::bands_a_wing + b;
            place(frame, n, x, y, scale >> 5, state.band_shade[b], fill_and_stroke);
            if (!half) turn(frame, n, fore);
        }
    }
    for (u32 j = 0; j < plate::eyes_a_wing; ++j) {
        const u32 ring = plate::eyes + 2 * (side * plate::eyes_a_wing + j);
        if (!state.spot_state[j]) {
            hide(frame, ring);
            hide(frame, ring + 1);
            continue;
        }
        i32 sx = state.spot_x[j], sy = state.spot_y[j];
        if (state.spot_fore[j] && sweep) {
            const Turn r = rotation(-((sweep * (state.spot_fore[j] >> 5)) >> 10));
            const i32 rx = sx - plate::pivot_x * unit;
            sx = plate::pivot_x * unit + ((r.cos * rx - r.sin * sy) >> 10);
            sy = (r.sin * rx + r.cos * sy) >> 10;
        }
        on_screen(side, sx, sy, scale, x, y);
        const i32 lit = mul16(state.spot_light[j] << 1, seen << 1) >> 1;   // Q15
        const u32 shade = static_cast<u32>(11 + ((4 * (lit >> 11)) >> 4));
        const i32 size = (state.spot_scale[j] * (scale >> 5)) >> 10;
        place(frame, ring, x, y, size, shade, stroke);
        place(frame, ring + 1, x, y, size, shade - 2, fill_and_stroke);
    }
    state.drawn_scale[side] = scale;
    state.drawn_sweep[side] = sweep;
    state.drawn_tail[side] = state.tail;
    state.drawn_width[side] = state.lit_width;
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    const i32* p = input.slots;
    const bool fresh = !state.initialized || state.track != input.track;
    const NoteEvents events = state.notes.update(input);
    (void)events;
    const bool held = input.held_notes <= 128 && input.held_notes != 0;
    const bool still = input.reduced_motion != 0;
    const i32 dt = clamp(input.elapsed, 0, 102);
    if (fresh) {
        state.initialized = 1;
        state.track = input.track;
        state.serial = input.note_serial;
        state.gate = 0;
        state.blocks = 0xffff;
        state.beat = state.beat_site = 0;
        state.flick = flick_time;
        state.since = frame_gap;
        for (u32 i = 0; i < 4; ++i) state.keys[i] = 0;
        state.drawn_light = 0xffffffffu;
        state.drawn_scale[0] = state.drawn_scale[1] = 0;
        state.dirty = 3;
        state.turn = 0;
    }

    // A new note: the strike.
    if (input.note_serial != state.serial) {
        state.serial = input.note_serial;
        state.gate = 0;
        state.blocks = 0;
        state.beat = state.beat_site = 0;
        state.flick = still ? flick_time : 0;
    }
    // The latest strike's gate and wingbeat, a block at a time, while held;
    // let go, the light fades in about 0.2 s, as the amp envelope takes it.
    const i32 struck = strike(p[STRIKE]), glow = glow_log2(p[GLOW]);
    const u32 rate = wingbeat_rate(p[WINGBEAT]);
    for (i32 t = dt * 64; t > 0; t -= block_q16) {
        if (held || state.blocks < pulse_blocks) {
            state.gate = gate_step(state.gate, state.blocks, struck, glow);
            if (state.blocks < 0xffffu) ++state.blocks;
            if (!still) {
                state.beat += rate;
                state.beat_site += wingbeat_site_rate(rate);
            }
        } else {
            state.gate -= state.gate >> 6;
        }
    }
    if (still && held) state.gate = (32767 - struck) * 24576;   // reduced motion: the held light at once

    // The chain the wings show: as the engine's, easing to the controls in about 0.15 s.
    const u32 beat = held ? state.beat : 0, beat_site = held ? state.beat_site : 0;
    const u32 alpha = warp(flux(p[FLUX]), snap_pull) + static_cast<u32>(wingbeat_flux(p[WINGBEAT], beat));
    const i32 lambda = coupling(p[COUPLING]);
    const i32 at = clamp(site(p[SITE]) + wingbeat_site(p[WINGBEAT], beat_site), 0, 8 << 16);
    // The wings' scale: spread's wingspan, 0.12 an octave of span, Moth's at
    // 24 semitones (0.64 at 3, 1.1 at 48), breathing in by up to 28% with the wingbeat.
    const i32 span = clamp(32768 + (((spread_log2(p[SPREAD]) - 65536) * 3932) >> 16), 0, 36045);
    const i32 breath = (((q15(p[WINGBEAT]) * 9175) >> 15) * ((32768 - cosine(beat)) >> 1)) >> 15;   // 0.28 x amount x (1 - cos) / 2
    const i32 scale = (span * (32768 - breath)) >> 15;
    // Tilt's sweep, glow's tail, strike's band.
    const i32 sweep = (((tilt(p[TILT]) >> 4) * sweep_per_tilt) + 2048) >> 12;
    const i32 tail = (q15(p[GLOW]) * tail_longest) >> 5;
    const i32 width = lit_thin + (((lit_broad - lit_thin) * struck) >> 15);
    const i32 share = clamp(dt * 600, 0, 32768);   // Q15 of the gap a frame
    if (fresh || still) {
        state.eased_alpha = alpha;
        state.eased_lambda = lambda;
        state.eased_site = at;
        state.scale = scale;
        state.sweep = sweep;
        state.tail = tail;
        state.lit_width = width;
    } else {
        const i32 da = static_cast<i32>(alpha - state.eased_alpha);
        state.eased_alpha += static_cast<u32>(da > -(1 << 14) && da < (1 << 14) ? da : (da >> 15) * share);
        const i32 dl = lambda - state.eased_lambda, ds = at - state.eased_site, dw = scale - state.scale;
        state.eased_lambda += dl > -8 && dl < 8 ? dl : (dl * share) >> 15;
        state.eased_site += ds > -256 && ds < 256 ? ds : (ds >> 15) * share;
        state.scale += dw > -64 && dw < 64 ? dw : (dw * share) >> 15;
        const i32 dv = sweep - state.sweep, dt2 = tail - state.tail, dh = width - state.lit_width;
        state.sweep += dv > -2 && dv < 2 ? dv : (dv * share) >> 15;
        state.tail += dt2 > -256 && dt2 < 256 ? dt2 : (dt2 * share) >> 15;
        state.lit_width += dh > -32 && dh < 32 ? dh : (dh * share) >> 15;
    }

    // The wings' shapes, when what they show moved, at most eight times a second.
    state.since += dt;
    if (fresh || state.since >= frame_gap) {
        i32 old[plate::margin + 2 + plate::bands_a_wing + 4 * plate::eyes_a_wing];
        u32 n = 0;
        for (u32 i = 0; i < plate::margin; ++i) old[n++] = state.reach[i];
        old[n++] = state.lit_share;
        old[n++] = 0;
        for (u32 b = 0; b < plate::bands_a_wing; ++b) old[n++] = static_cast<i32>(state.band_shade[b]);
        for (u32 j = 0; j < plate::eyes_a_wing; ++j) {
            old[n++] = state.spot_x[j];
            old[n++] = state.spot_y[j];
            old[n++] = state.spot_scale[j];
            old[n++] = static_cast<i32>(state.spot_state[j]) + (state.spot_light[j] >> 11) * 2;
        }
        draw_wings(state, state.eased_alpha, state.eased_lambda, state.eased_site);
        bool changed = fresh;
        n = 0;
        for (u32 i = 0; i < plate::margin; ++i) changed = changed || old[n++] != state.reach[i];
        changed = changed || old[n++] != state.lit_share;
        ++n;
        for (u32 b = 0; b < plate::bands_a_wing; ++b) changed = changed || old[n++] != static_cast<i32>(state.band_shade[b]);
        for (u32 j = 0; j < plate::eyes_a_wing; ++j) {
            changed = changed || old[n] != state.spot_x[j] || old[n + 1] != state.spot_y[j] || old[n + 2] != state.spot_scale[j] ||
                      old[n + 3] != static_cast<i32>(state.spot_state[j]) + (state.spot_light[j] >> 11) * 2;
            n += 4;
        }
        if (changed) {
            state.dirty = 3;
            state.since = 0;
        } else {
            state.since = frame_gap;   // look again next frame, without solving
        }
    }

    // The light: the gate's, in sixteen steps, on the lit band and the eyespots.
    const i32 light = state.gate >> 15;   // Q15
    const u32 step = static_cast<u32>(light >> 11);
    if (step != state.drawn_light) {
        state.drawn_light = step;
        state.dirty = 3;
    }

    // Each wing's nodes when they changed or the wing's pose moved: one wing
    // a frame, so the wings take turns (both on the first frame).
    const u32 first = state.turn;
    for (u32 k = 0; k < 2; ++k) {
        const u32 side = (first + k) & 1;
        const i32 moved = state.scale - state.drawn_scale[side];
        const bool posed = state.sweep != state.drawn_sweep[side] || state.tail != state.drawn_tail[side] ||
                           state.lit_width != state.drawn_width[side];
        if ((state.dirty & (1u << side)) || moved >= 64 || moved <= -64 || posed) {
            place_wing(frame, state, side, state.scale, light, struck);
            state.dirty &= ~(1u << side);
            state.turn = side ^ 1;
            if (!fresh) break;
        }
    }

    // The antennae: stood up by the strike, folded back along the forewings
    // and short at none (35 degrees out, 0.7 of their length), upright and
    // long at full (13 degrees in, 1.2), turned and drawn out from their
    // bases; a strike flicks them out and back within 0.2 s.
    if (state.flick < flick_time) state.flick += dt;
    const i32 flicked = state.flick < flick_time / 2 ? 40 : state.flick < flick_time ? 20 : 0;   // tenths of a degree
    const i32 out = 350 - ((struck * 480) >> 15) + ((flicked * struck) >> 15);
    const i32 length = 717 + ((struck * 512) >> 15);   // Q10
    for (u32 side = 0; side < 2; ++side) {
        const Turn r = rotation(side ? out : -out);
        const i32 bx = side ? plate::antenna_x : -plate::antenna_x, by = plate::antenna_y;
        const i32 x = bx - ((((r.cos * bx - r.sin * by) >> 10) * length) >> 10);
        const i32 y = by - ((((r.sin * bx + r.cos * by) >> 10) * length) >> 10);
        place(frame, plate::antennae + side, (plate::centre_x << 10) + x, (plate::centre_y << 10) + y, length, ANTENNA, stroke);
        turn(frame, plate::antennae + side, r);
    }

    // The two-page header, captions in real units.
    const char* const titles[8] = {"flux", "coupling", "spread", "strike", "glow", "site", "tilt", "wingbeat"};
    char values[8][header_capacity] = {};
    flux_caption(values[FLUX], warp(flux(p[FLUX]), snap_pull));
    decimal(values[COUPLING], static_cast<u32>((lambda * 100 + 2048) >> 12), 2);
    digits(values[SPREAD], static_cast<u32>((spread_semitones(p[SPREAD]) + 128) >> 8), 1);
    append(values[SPREAD], "st");
    header_amount(values[STRIKE], sizeof(values[STRIKE]), p[STRIKE]);
    glow_caption(values[GLOW], glow);
    site_caption(values[SITE], site(p[SITE]));
    decimal(values[TILT], static_cast<u32>((tilt(p[TILT]) * 10 + 32768) >> 16), 1);
    header_amount(values[WINGBEAT], sizeof(values[WINGBEAT]), p[WINGBEAT]);
    page_header(state.header, input, frame, titles, values, plate::header);
}
}
