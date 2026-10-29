// Sand's screen: the apparatus, as an engraving shows it. A square plate on
// its stand, seen from the front and a little to the left, clamped at its
// centre; the current figure's nodal lines and up to forty-eight grains of
// sand on its top; a violin bow across its right edge at the figure's bow point and,
// as Bow gives way to a strike, a felt mallet over it.
//
// Every control changes the apparatus itself. Figure draws its lines and
// moves the bow; Bow lifts the bow off the edge as it falls and brings the
// mallet in; Sand sets how much sand there is, from a bare plate to the
// figure drawn in heaped sand, and Grain how big its grains are;
// Plate sets the plate's thickness, edge and marks (a ruled border, glints or
// wood grain); Settle its finish, from a polished sheen to a matte, rosined
// tooth; Tilt tips the whole plate on its column, so the sand runs downhill;
// Shake draws the plate twice more aside, as an engraver draws a thing that
// trembles, and throws its sand into the air while it sounds.
//
// The grains are thrown by the same physics as the sound's (sand.h), driven
// by the bowed mode as it swells while notes are held and by a strike as it
// rings down. A note with the plate silent pours fresh sand, which dances
// bright and leaps, then gathers on the lines and lies there in drifts that
// draw the figure; a note while the
// plate still sounds keeps its sand, as the sound does, and a strike jolts
// it. Settled grains stop moving, so a still plate redraws nothing.
#include "display.h"

namespace sand {
namespace screen {
using namespace opxy::draw;

// One look at every grain each 5.8 ms (256 samples), as the sound looks at
// each of its grains every four blocks; at most this many a frame.
const i32 kLookTime = 6233;  // Q20 seconds
const u32 kMostLooks = 24;
// With reduced motion a note shows its settled figure at once: this many looks.
const u32 kSettleLooks = 400;
// A grain that has sat this many looks counts as settled, and dims.
const u32 kSettled = 24;
// The plate counts as silent below this swing (amplitude units): a note then
// pours fresh sand, and Shake's jolts stop.
const i32 kSilent = 60;
// A grain settled within this much of the figure's loudest (Q15) lies on its
// nodal line, and is drawn with the drift of sand it stands for, along the line.
const i32 kOnLine = 6554;
// The highest a grain is drawn (Q10 px): the header owns y 0 to 38.
const i32 kBelowHeader = 42 * 1024;
// Settled grains are redrawn one frame in this many.
const u32 kSettledTurns = 6;
// A grain in the air is drawn this far above the plate per look it has left
// to fall, up to the most (px).
const i32 kRise = 1, kRiseMost = 12;
// A shaken plate's double image: this far aside at the least Shake, and
// further by Shake (px).
const i32 kTremor = 3, kTremorMore = 8;
// The plate tips up to 10 degrees at full Tilt, drawn in half-degree steps.
const i32 kTiltMost = 100, kTiltStep = 5;  // tenths of a degree
// 1000 tan of each whole degree, 0 to 11, for the tilt's caption.
const u32 kGrade[12] = {0, 17, 35, 52, 70, 87, 105, 123, 141, 158, 176, 194};
// More Bow engages more of the bow: it lies up to this far over the plate at
// full Bow. Below a light bow it lifts clear of the edge, up to this far at 0.
// Below half Bow the mallet comes in, from this far away, and hovers this far
// above the plate between strikes (px).
const i32 kEngage = 30, kLift = 26, kMalletReach = 110, kHover = 7;
const i32 kLight = 4915, kHalfBow = 16384;  // Bow 0.15 and 0.5
// The mallet's rise after a strike (Q10 seconds).
const i32 kRebound = 184;

// The figure's lift on the drawn plate, which knows only the bowed mode: its
// twin and the bow's harmonics, which share its shape, lift about an eighth
// more (engine.cpp).
inline i32 figure_weight(i32 swing, u32 ratio) { return clamp(lift(swing, ratio) * 9 >> 3, 0, 65535); }

// A point of the plate (u along the bowed edge from the back, v in from it,
// 0..65535) on screen before any tilt (Q10), through the view: x = 1 - v
// across, y = u deep.
inline i32 flat_x(u32 u, u32 v) {
    const i32 x = 32767 - static_cast<i32>(v), y = static_cast<i32>(u) - 32768;
    return view_x * unit + ((x * view_w + y * view_s) >> 6);
}
inline i32 flat_y(u32 u) { return view_y * unit + ((static_cast<i32>(u) - 32768) * view_h >> 6); }

// The direction of the figure's nodal line through (u, v) on screen, before
// any tilt: along the shape's contour there, as a unit turn (Q10).
inline Turn contour(const PlateMode& mode, u32 u, u32 v) {
    const u32 d = 1024;
    const u32 u0 = u < d ? 0 : u - d, u1 = u > 65535 - d ? 65535 : u + d;
    const u32 v0 = v < d ? 0 : v - d, v1 = v > 65535 - d ? 65535 : v + d;
    const i32 gu = shape(mode, u1, v) - shape(mode, u0, v), gv = shape(mode, u, v1) - shape(mode, u, v0);
    // Along the contour (-gv, gu) in the plate, then through the view: x moves
    // view_s per u and -view_w per v, y view_h per u.
    i32 x = -gv * view_s - gu * view_w, y = -gv * view_h;
    while (magnitude(x) > 16383 || magnitude(y) > 16383) {
        x >>= 1;
        y >>= 1;
    }
    i32 length = 0;  // the integer square root of x^2 + y^2
    for (i32 bit = 1 << 14; bit; bit >>= 1)
        if ((length + bit) * (length + bit) <= x * x + y * y) length += bit;
    const Turn along = {length ? x * unit / length : unit, length ? y * unit / length : 0};
    return along;
}

// A screen point (Q10) turned with the plate about the pivot.
struct Point2 { i32 x, y; };
inline Point2 tipped(i32 x, i32 y, Turn r) {
    const i32 dx = x - pivot_x, dy = y - pivot_y;
    Point2 p = {pivot_x + ((dx * r.cos - dy * r.sin) >> 10), pivot_y + ((dx * r.sin + dy * r.cos) >> 10)};
    return p;
}

// A number as text: whole, or with two decimals (hundredths) or one (tenths),
// and a unit; bare drops a fraction's leading zero (".3mm"), to fit the header.
inline void number(char* out, u32 capacity, u32 n, u32 decimals, const char* unit_text, bool bare = false) {
    char text[header_capacity];
    u32 at = 0;
    const u32 whole = decimals == 2 ? n / 100 : decimals == 1 ? n / 10 : n;
    if (whole >= 100) text[at++] = static_cast<char>('0' + whole / 100 % 10);
    if (whole >= 10) text[at++] = static_cast<char>('0' + whole / 10 % 10);
    if (whole || !bare || !decimals) text[at++] = static_cast<char>('0' + whole % 10);
    if (decimals) {
        text[at++] = '.';
        if (decimals == 2) text[at++] = static_cast<char>('0' + n / 10 % 10);
        text[at++] = static_cast<char>('0' + n % 10);
    }
    for (u32 i = 0; unit_text[i] && at + 1 < sizeof(text); ++i) text[at++] = unit_text[i];
    text[at] = 0;
    copy(out, capacity, text);
}

// Eases value toward target, a share dt * rate / 1024 of the way (rate in
// 1/s), finishing within half a unit.
inline i32 ease(i32 value, i32 target, i32 dt, i32 rate) {
    const i32 step = clamp(dt * rate, 0, 1024);
    const i32 next = value + ((target - value) >> 5) * step / 32;
    return magnitude(target - next) < unit / 2 ? target : next;
}

// The drawn plate's sand for one look.
inline void look_all(DisplayState& state, const PlateMode* modes, const Throw& t, u32 count) {
    for (u32 i = 0; i < count; ++i) look(state.grains[i], t, modes, state.seed, 0);
}

// A plate part: at the pivot, turned with the plate, in a grey.
template <class F>
inline void part(F& frame, u32 node, Turn r, u32 grey, Paint paint) {
    place(frame, node, pivot_x, pivot_y, unit, grey, paint);
    turn(frame, node, r);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.started = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    const u32 figure = choice(input.slots[0], kFigures);
    const u32 plate_index = choice(input.slots[4], 3);
    const i32 bow = control(input.slots[1]), sand = control(input.slots[2]), grain = control(input.slots[3]),
              settle = control(input.slots[5]), tilt = control(input.slots[6]), shake = control(input.slots[7]);
    const Material mat = material(plate_index == 2 ? 32767 : static_cast<i32>(plate_index) * 16384);

    // The header: both pages, in the units the sound uses: the figure's mode
    // pair, the bow's force, how many grains are on the plate and their size,
    // the material by name, the sand's friction (mu), the plate's slope and
    // Shake's largest jolt (in g). Each fits its column in the OP-XY's font
    // (tests/screen.cpp measures them).
    const char* const titles[8] = {"Figure", "Bow", "Sand", "Grain", "Plate", "Settle", "Tilt", "Shake"};
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    copy(values[0], header_capacity, kFigureName[figure]);
    number(values[2], header_capacity, static_cast<u32>(sand * static_cast<i32>(kGrains) + 16384) >> 15, 0, "");
    // The grain's size, from 0.06 mm (fine dust) to 7.7 mm (a stone), as tenths or whole millimetres.
    const u32 across = 6 * exp2_q16(grain * 14) >> 16;  // hundredths of a mm
    if (across < 95) number(values[3], header_capacity, (across + 5) / 10, 1, "mm", true);
    else number(values[3], header_capacity, (across + 50) / 100, 0, "mm");
    copy(values[4], header_capacity, plate_index == 0 ? "metal" : plate_index == 1 ? "glass" : "wood");
    number(values[5], header_capacity, static_cast<u32>(10 + (settle * 50 + 16384) / 32767), 2, "");
    // The plate's slope as a grade (100 tan of its tilt, in percent): 10 degrees
    // is 18%, and "10deg" would not fit the header in the OP-XY's font.
    const u32 slope = static_cast<u32>((tilt * kTiltMost + 16384) / 32767), degree = slope / 10;  // tenths of a degree
    const u32 grade = (kGrade[degree] * (10 - slope % 10) + kGrade[degree + 1] * (slope % 10) + 50) / 100;
    number(values[6], header_capacity, grade, 0, "%");
    number(values[7], header_capacity, static_cast<u32>((shake_jolt(shake) * 10 + 2048) >> 12), 1, "g");
    // The bow's force on the plate's edge, up to 2 newtons.
    number(values[1], header_capacity, static_cast<u32>((bow * 20 + 16384) / 32767), 1, "N");
    page_header(state.header, input, frame, titles, values, header_first);

    // Notes. A note while the plate is silent pours fresh sand; one while it
    // still sounds keeps the sand, as the sound's notes do. Every note strikes
    // the plate as hard as it is not bowed, and the strike jolts the sand.
    const bool fresh = !state.started || !state.notes.initialized || state.notes.track != input.track;
    state.notes.update(input);
    const i32 dt = clamp(input.elapsed, 0, 102);
    const i32 strike = strike_share(bow);
    const i32 tilt_target = -(tilt * kTiltMost / 32767) * unit;  // the far (left) edge goes down
    bool poured = false;
    if (fresh) {
        state.seed = input.track * 2654435761u | 1u;
        pour(state.grains, grains_shown, state.seed);
        state.serial = input.note_serial;
        state.swing = state.ring = 0;
        state.jolted = 0;
        state.clock = 0;
        state.since = 1 << 20;
        state.stroke = 0;
        state.tilt = tilt_target;
        state.struck = kRebound;
    } else if (state.serial != input.note_serial) {
        state.serial = input.note_serial;
        if (state.swing + state.ring < kSilent) {
            pour(state.grains, grains_shown, state.seed);
            poured = true;
        }
        state.ring = clamp(state.ring + (strike * kStrikeAmplitude >> 15), 0, 4095);
        state.jolted = 1;
        state.since = 0;
        state.struck = 0;
    }
    state.since = state.since > (1 << 20) ? state.since : state.since + dt;
    const bool unknown = input.held_notes > 128;
    const bool held = unknown ? state.since < 1536 : input.held_notes != 0;  // without activity: 1.5 s a note

    // Run the sand: one look per 5.8 ms. The bowed mode swells toward the
    // bow's level while held (as fast as it is bowed) and rings down after;
    // the strike rings down with the plate.
    const PlateMode* const modes = kFigureModes[figure];
    const u32 count = static_cast<u32>(sand * static_cast<i32>(grains_shown) + 16384) >> 15;
    Throw t;
    for (u32 k = 0; k < kModes; ++k) t.weight[k] = 0;
    plan_throw(t, settle, tilt, shake);
    const i32 calm = t.jitter;
    const i32 target = held ? bow_level(bow) : 0;
    const i32 rise = ((mat.rise >> 16) * 4) * (bow >> 3) >> 12;  // Q15 a look: four blocks' worth, by Bow
    if (input.reduced_motion) {
        // The end state at once: a held note's settled figure, the bow at rest.
        state.swing = target;
        state.ring = 0;
        state.stroke = 0;
        state.tilt = tilt_target;
        state.struck = kRebound;
        if (poured || fresh) {
            t.weight[0] = figure_weight(state.swing, modes[0].ratio);
            for (u32 n = 0; n < kSettleLooks; ++n) look_all(state, modes, t, count);
        }
        state.jolted = 0;
    } else {
        u32 looks = 0;
        state.clock += dt << 10;
        while (state.clock >= kLookTime && looks < kMostLooks) {
            state.clock -= kLookTime;
            ++looks;
            state.swing = held ? state.swing + ((target - state.swing) * rise >> 15) : state.swing * mat.fall >> 15;
            state.ring = state.ring * mat.fall >> 15;
            if (held) state.ring -= state.ring * rise >> 15;  // the bow damps the strike
            t.weight[0] = figure_weight(state.swing + state.ring, modes[0].ratio);
            // Shake jolts the plate while it sounds, as the sound's shake does.
            const bool sounding = held || state.swing + state.ring >= kSilent;
            t.jitter = (sounding ? calm : 0) + (state.jolted ? strike * kJolt >> 15 : 0);
            t.jolts = state.jolted ? 32767 : shake;
            state.jolted = 0;
            look_all(state, modes, t, count);
        }
        if (looks == kMostLooks) state.clock = 0;  // a late frame drops the time it could not run
        // The bow draws over the plate while held, 8 to 32 px a second by
        // Bow, to the end of its stroke; released, it eases back to the edge.
        if (held) state.stroke = clamp(state.stroke + (8 + (bow * 24 >> 15)) * dt, 0, stroke_most * unit);
        else state.stroke = ease(state.stroke, 0, dt, 16);
        state.tilt = ease(state.tilt, tilt_target, dt, 20);
        state.struck = state.struck < kRebound ? state.struck + dt : kRebound;
    }
    // The plate's turn, in half-degree steps, so a tilt redraws it only as it moves.
    const i32 tenths = (state.tilt >> 10) / kTiltStep * kTiltStep;
    const Turn r = rotation(tenths);

    // The plate: its edge, its thickness and marks by material, its finish
    // by Settle, its trembling edge by Shake, and the clamp.
    part(frame, plate_top, r, top_grey, stroke);
    for (u32 m = 0; m < materials; ++m) {
        if (m == plate_index) part(frame, thickness_first + m, r, edge_grey[m], stroke);
        else hide(frame, thickness_first + m);
    }
    for (u32 k = 0; k < 2; ++k) {
        if (plate_index == 1) part(frame, glass_first + k, r, 0, keep);
        else hide(frame, glass_first + k);
    }
    for (u32 k = 0; k < 4; ++k) {
        if (plate_index == 2) part(frame, wood_first + k, r, 0, keep);
        else hide(frame, wood_first + k);
    }
    // A polished plate (Settle below 35) shows its sheen, a sticky one (above 65)
    // its rosined tooth; the default's plate is plain.
    const i32 polish = clamp(11468 - settle, 0, 11468), tooth = clamp(settle - 21299, 0, 11468);
    const u32 sheen = static_cast<u32>(polish * 11 / 11468), matte = static_cast<u32>(tooth * 9 / 11468);
    for (u32 k = 0; k < 2; ++k) {
        if (sheen >= 3) part(frame, sheen_first + k, r, sheen, fill_and_stroke);
        else hide(frame, sheen_first + k);
    }
    for (u32 k = 0; k < 2; ++k) {
        if (matte >= 3) part(frame, matte_first + k, r, matte, fill_and_stroke);
        else hide(frame, matte_first + k);
    }
    // Shake: the whole plate drawn twice more, plainly aside, as an engraver
    // draws a thing that trembles.
    const i32 tremble = shake >= 1000 ? (kTremor + (shake * kTremorMore >> 15)) * unit : 0;
    for (u32 k = 0; k < 2; ++k) {
        if (!tremble) {
            hide(frame, tremor_first + k);
            continue;
        }
        place(frame, tremor_first + k, pivot_x + (k ? -tremble : tremble), pivot_y + (k ? tremble : -tremble) / 2, unit, 8, stroke);
        turn(frame, tremor_first + k, r);
    }
    for (u32 k = 0; k < 3; ++k) part(frame, clamp_first + k, r, 0, keep);

    // The figure's nodal lines, on the plate's top: on a bare plate only scribed.
    for (u32 p = 0; p < kPaths; ++p) {
        Frame::path_type& path = frame.paths[p];
        const u32 key = figure * kPaths + p + 1;
        if (path.key != key || !state.started) {
            path.key = key;
            for (u32 i = 0; i < kPoints; ++i) {
                path.points[i].x = kOutline[figure][p][i][0] * 4;
                path.points[i].y = kOutline[figure][p][i][1] * 4;
            }
        }
        if (p < kOutlinesUsed[figure]) {
            place(frame, p, pivot_x, pivot_y, path_scale, count ? line_grey : bare_grey, stroke);
            turn(frame, p, r);
        } else {
            hide(frame, p);
        }
    }

    // The bow, its hair on the right edge at the figure's bow point, turned
    // with the plate: lifted off the edge as Bow falls, drawn back along its
    // axis over the plate in whole pixels while held. A bow point near the
    // plate's back has less room: the stroke stops before the head reaches the
    // header (y 40, with the head 18 px tall).
    const Point2 contact = tipped(flat_x(kBowAt[figure], 0), flat_y(kBowAt[figure]), r);
    const i32 axis_x = (bow_cos * r.cos - bow_sin * r.sin) >> 10, axis_y = (bow_cos * r.sin + bow_sin * r.cos) >> 10;
    const i32 room = clamp(((contact.y >> 10) - 58) * 9 >> 2, 0, stroke_most + kEngage);
    const i32 engaged = bow < kLight ? 0 : (bow - kLight) * kEngage / (32767 - kLight);
    const i32 lifted = bow < kLight ? (kLight - bow) * kLift / kLight : 0;
    const i32 along = lifted - clamp(engaged + (state.stroke >> 10), 0, room);
    for (u32 k = 0; k < bow_parts; ++k) {
        place(frame, bow_first + k, contact.x + along * axis_x, contact.y + along * axis_y, unit,
              bow_grey[k][held ? 1 : 0], k == 1 ? stroke : fill_and_stroke);
        turn(frame, bow_first + k, r);
    }

    // The mallet: in from beyond the screen as Bow falls below half, down onto
    // the plate as a note strikes it, then back up to hover.
    const i32 hover = state.struck >= kRebound ? kHover * unit : kHover * state.struck * unit / kRebound;
    const i32 away = (bow * kMalletReach / kHalfBow) * unit + hover;  // Q10 px along the shaft
    const Point2 spot = tipped(mallet_x, mallet_y, r);
    for (u32 k = 0; k < 2; ++k) {
        if (bow >= kHalfBow) {
            hide(frame, mallet_first + k);
            continue;
        }
        place(frame, mallet_first + k, spot.x + (away >> 10) * mallet_cos, spot.y + (away >> 10) * mallet_sin, unit, 0, keep);
    }

    // The grains: as large as Grain (from fine dust to stones), bright while
    // they dance, in the air above the plate, dim once settled; turned with
    // the plate. A grain settled on the figure's line lies on the drift of
    // sand it stands for, along the line, so plenty of Sand draws the figure
    // in heaped sand. Only some of the sand is redrawn each frame, so a plate
    // of dancing sand, or one tipping under it, never redraws all of it at
    // once: moving grains on alternate frames, settled ones every sixth (as
    // the plate tips they follow it within a tenth or a fifth of a second).
    const i32 size = unit + (grain * unit >> 15);  // 2.6 to 5.2 px across: no dust finer than the screen shows
    // More Sand heaps longer, whiter drifts, until they join along the lines;
    // fine sand lies in long drifts, stones mostly as themselves.
    const i32 drift_size = clamp((unit * 6 / 10 + (sand * (unit * 9 / 10) >> 15)) * (1280 - (grain * 640 >> 15)) >> 10, unit * 6 / 10, 2 * unit);
    const u32 drift_grey = 7 + static_cast<u32>(sand * 2 >> 15);
    ++state.frames;
    for (u32 i = 0; i < grains_shown; ++i) {
        const u32 node = grain_first + i, drift = drift_first + i;
        if (i >= count) {
            hide(frame, node);
            hide(frame, drift);
            continue;
        }
        const Grain& g = state.grains[i];
        const bool moving = g.air || g.rest < kSettled;
        const bool turn_now = !state.started || i % kSettledTurns == state.frames % kSettledTurns;
        const Point2 at = tipped(flat_x(g.u, g.v), flat_y(g.u), r);
        if (moving ? !state.started || ((i ^ state.frames) & 1) == 0 : turn_now) {
            const u32 grey = g.air ? aloft_grey : moving ? moving_grey : rest_grey;
            const i32 rise = clamp(g.air * kRise, 0, kRiseMost) * unit;
            const i32 y = at.y - rise < kBelowHeader ? kBelowHeader : at.y - rise;  // a tipped plate's grains stay clear of the header
            place(frame, node, at.x, y, g.air ? clamp(size * 5 / 4, 0, 2 * unit) : size, grey, fill_and_stroke);
        }
        if (!turn_now) continue;  // the drift follows on the settled grains' turn
        if (moving || magnitude(shape(modes[0], g.u, g.v)) >= kOnLine) {
            hide(frame, drift);
            continue;
        }
        const Turn line = contour(modes[0], g.u, g.v);
        const Turn lying = {clamp((line.cos * r.cos - line.sin * r.sin) >> 10, -unit, unit),
                            clamp((line.cos * r.sin + line.sin * r.cos) >> 10, -unit, unit)};
        place(frame, drift, at.x, at.y, drift_size, drift_grey, fill_and_stroke);
        turn(frame, drift, lying);
    }
    state.started = 1;
}
}
