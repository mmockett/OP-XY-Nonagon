#include "display.h"
#include "plate.h"

// Cocoa's screen: one mug of hot chocolate in section (tools/make_panel.py),
// to scale at 1.5 px a millimetre, in four greys and three weights: the mug
// boldest and brightest, the spoon and the drink's surface a step down, the
// drink's detail finer.
//
// The wall is china, glass or enamelled tin (cup): a thick wall and a C
// handle, a tapering tumbler with a heavy base, or a thin wall with a rolled
// rim and a strap handle. The drink stands at its depth (fill), curling up
// the wall in a meniscus. It is drawn in lines, as many as it is dark
// (powder: pale milk to thick chocolate). Foam rides the surface: pops'
// amount, fizz's size. Unstirred (stir 0), powder lies in a heap on the floor
// and clumps hang in the drink; stirred, the heap dissolves, the drink turns
// in a funnel of swirls and its surface dips into a vortex.
//
// A spoon stands in the drink, its bowl on the floor (spoon): a short, fat,
// grained wooden spoon leaning on the wall inside, to a long, slim silver one
// leaning on the inside of the rim's lip; neither crosses a line of the cup.
// It rests lifted, 24 degrees for a single tap to 2 for the fastest (taps),
// and each tap rocks it onto the floor in time with the voices' taps,
// rippling the surface and the floor; tapping fast it shakes.
//
// The drink shows its own pitch. A note's stir dips the surface further and
// fills the drink with bubbles: five rings a class, more for more powder,
// larger for larger bubbles, each class rising at its own speed. While bubbles
// fill the drink at a line's height the line is frothy; the drink clears from
// the floor up as the bubbles rise out, and it rings in tune (within 3 cents)
// once every line is clear. Bubbles burst from the foam as the voices pop.
//
// The screen cannot hear the voices: it keeps its own drink and spoon, stepped
// with the voices' own code (drink.h) for each 64 samples of time. Like the
// voices' drink, it waits while nothing sounds, and settles by the next note
// after a pause. Integer arithmetic only.
namespace cocoa {
// The SDK's draw::sine and draw::rotation, kept local to this machine: the
// Blackfin compiler emits an inline function it doesn't inline as a strong
// global symbol, so two machines using the SDK's copy can't link into one
// firmware. Same arithmetic, internal linkage.
static i32 sine(i32 tenths) {
    tenths %= 3600;
    if (tenths < 0) tenths += 3600;
    const i32 sign = tenths >= 1800 ? -1 : 1;
    tenths %= 1800;
    if (tenths > 900) tenths = 1800 - tenths;
    const i32 degree = tenths / 10, fraction = tenths % 10;
    const i32 low = opxy::draw::sine_q10[degree], high = opxy::draw::sine_q10[degree < 90 ? degree + 1 : 90];
    return sign * (low + (high - low) * fraction / 10);
}
static opxy::draw::Turn rotation(i32 tenths) {
    const opxy::draw::Turn turn = {sine(tenths + 900), sine(tenths)};
    return turn;
}

namespace screen {
using namespace opxy::draw;
const u32 budget = 14;          // bubbles moved a frame, at most
const i32 release = 512;        // Q10 s the voices sound on after the last key is let go (an estimate)
const i32 froth_void = 470;     // a void fraction (Q31) that flattens the column 3 cents
// Four greys, each with its weight (make_panel.py): the mug, the spoon and
// the drink's surface, the drink's detail, and what recedes.
namespace grey { const u32 mug = 15, spoon = 13, detail = 11, quiet = 7; }

inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
// value eased toward target by `keep` (Q16: the share of the distance left).
inline i32 ease(i32 value, i32 target, u32 keep) {
    return target + static_cast<i32>((static_cast<i32>(value - target) >> 4) * static_cast<i32>(keep >> 4) >> 8);
}
// Places a node upright when it is not already so.
inline void put(Frame& frame, u32 n, i32 x, i32 y, i32 scale, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == unit && d.sin == 0 && d.shade == grey && d.paint == paint)
        return;
    place(frame, n, x, y, scale, grey, paint);
}
// Places a node turned, when it is not already so.
inline void put_turned(Frame& frame, u32 n, i32 x, i32 y, i32 scale, Turn t, u32 grey, Paint paint = stroke) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == t.cos && d.sin == t.sin && d.shade == grey && d.paint == paint)
        return;
    place(frame, n, x, y, scale, grey, paint);
    turn(frame, n, t);
}
inline void conceal(Frame& frame, u32 n) { put(frame, n, hidden_x * unit, hidden_y * unit, unit, 0, keep); }
inline bool shown(const Frame& frame, u32 n) { return frame.nodes[n].x != hidden_x * unit; }
// Shows an authored node in place, or hides it.
inline void show_if(Frame& frame, u32 n, bool show, u32 grey, Paint paint) {
    if (show) put(frame, n, 0, 0, unit, grey, paint);
    else conceal(frame, n);
}
// Sets outline p when a point has moved a quarter pixel, or `force`.
inline void outline(Frame& frame, u32 p, const Point* points, u32& key, bool force) {
    Frame::path_type& path = frame.paths[p];
    bool moved = force || !path.key;
    for (u32 i = 0; i < Frame::point_count && !moved; ++i)
        if (magnitude(points[i].x - path.points[i].x) >= 256 || magnitude(points[i].y - path.points[i].y) >= 256)
            moved = true;
    if (!moved) return;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        path.points[i] = points[i];
        if (i && points[i].x == path.points[i - 1].x && points[i].y == path.points[i - 1].y) ++path.points[i].y;   // distinct
    }
    key = key + 1 ? key + 1 : 1;
    path.key = key;
}
// A morph of two of plate's shapes, by m (Q10, 0 the first).
inline void morph(Point* points, const i32 (&a)[64][2], const i32 (&b)[64][2], i32 m) {
    for (u32 i = 0; i < 64; ++i) {
        points[i].x = a[i][0] + (((b[i][0] - a[i][0]) * m) >> 10);
        points[i].y = a[i][1] + (((b[i][1] - a[i][1]) * m) >> 10);
    }
}
inline u32 next(u32& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
// A row of plate's 33 spoon rests, wood to silver, at m (Q10).
inline i32 along(const i32 (&row)[33], i32 m) {
    const i32 i = m >> 5;
    if (i >= 32) return row[32];
    return row[i] + (((row[i + 1] - row[i]) * (m & 31)) >> 5);
}
// The drink's height in Q10 px (1.5 px a millimetre).
inline i32 height(i32 fill) { return static_cast<i32>(drink::fill_um(fill) * 1536u >> 10); }
// The drop of a class's bubbles were they the whole drink, to count its rings.
inline i32 crowd(i32 d, u32 i) {
    const u32 share = static_cast<u32>(tables::class_share[i]) >> 16;
    const u32 equal = static_cast<u32>(d) / (share ? share : 1);
    return drink::wood_drop(static_cast<i32>(equal > 65535u ? 65535u << 15 : equal << 15));
}

// The header's captions, in the controls' units.
inline void put_number(char* t, u32& at, u32 v) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n) t[at++] = digits[--n];
}
inline void caption_unit(char* out, u32 v, const char* unit_name) {
    char t[8];
    u32 at = 0;
    put_number(t, at, v);
    for (u32 i = 0; unit_name[i] && at < 7; ++i) t[at++] = unit_name[i];
    t[at] = 0;
    copy(out, header_capacity, t);
}
// powder as the scoop it gives a fresh stir: "-5st".
inline void caption_powder(char* out, i32 powder) {
    const u32 semitones = (static_cast<u32>(drink::powder_drop(powder)) * 12u + 32768u) >> 16;
    char t[8];
    u32 at = 0;
    if (semitones) t[at++] = '-';
    put_number(t, at, semitones);
    t[at++] = 's'; t[at++] = 't'; t[at] = 0;
    copy(out, header_capacity, t);
}
inline void caption_taps(char* out, i32 taps) {
    if (!drink::tap_step(taps)) { copy(out, header_capacity, "once"); return; }
    const u32 tenths_of = drink::tap_tenths(taps);
    char t[8];
    u32 at = 0;
    put_number(t, at, tenths_of / 10);
    if (tenths_of < 100) { t[at++] = '.'; t[at++] = static_cast<char>('0' + tenths_of % 10); }
    t[at++] = 'H'; t[at++] = 'z'; t[at] = 0;
    copy(out, header_capacity, t);
}
// fizz as the median bubble: "600um", "1.2mm".
inline void caption_fizz(char* out, i32 fizz) {
    const u32 um = drink::radius_um(fizz);
    if (um < 995) { caption_unit(out, (um + 5) / 10 * 10, "um"); return; }
    const u32 tenths_of = (um + 50) / 100;
    const char t[6] = {static_cast<char>('0' + tenths_of / 10), '.', static_cast<char>('0' + tenths_of % 10), 'm', 'm', 0};
    copy(out, header_capacity, t);
}
// pops as how often a fresh stir's foam pops: "15/s", "off".
inline void caption_pops(char* out, i32 pops) {
    const u32 rate = (drink::q16(pops) * 30u + 32768u) >> 16;   // the engine's foam_pops, 30 a second at 100
    if (!rate) { copy(out, header_capacity, "off"); return; }
    caption_unit(out, rate, "/s");
}
// spoon as its contact time: 4.0ms (soft) to 75us (hard).
inline void caption_spoon(char* out, i32 spoon) {
    const i32 log2_us = tables::log2_soft_us - static_cast<i32>((static_cast<u32>(tables::corner_span) >> 4) * drink::q16(spoon) >> 12);
    const u32 us = drink::exp2_int(log2_us);
    if (us < 995) { caption_unit(out, (us + 5) / 10 * 10, "us"); return; }
    const u32 tenths_of = (us + 50) / 100;
    const char t[6] = {static_cast<char>('0' + tenths_of / 10), '.', static_cast<char>('0' + tenths_of % 10), 'm', 's', 0};
    copy(out, header_capacity, t);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;   // the next frame snaps everything into place
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    const i32* slots = input.slots;

    // The header.
    const char* const titles[8] = {"Powder", "Taps", "Fizz", "Cup", "Fill", "Spoon", "Pops", "Stir"};
    const char* const cups[3] = {"china", "glass", "tin"};
    char values[8][header_capacity];
    caption_powder(values[POWDER], slots[POWDER]);
    caption_taps(values[TAPS], slots[TAPS]);
    caption_fizz(values[FIZZ], slots[FIZZ]);
    const u32 cup = drink::choice(slots[CUP], 3);
    copy(values[CUP], header_capacity, cups[cup]);
    caption_unit(values[FILL], (drink::fill_um(slots[FILL]) + 500) / 1000, "mm");
    caption_spoon(values[SPOON], slots[SPOON]);
    caption_pops(values[POPS], slots[POPS]);
    caption_unit(values[STIR], (static_cast<u32>(drink::restir(slots[STIR])) * 100u + 16384u) >> 15, "%");
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, pages);

    // Time.
    const NoteEvents events = state.notes.update(input);
    const bool fresh = !state.initialized || state.track != input.track;
    const bool reduced = input.reduced_motion != 0;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 102);
    const u32 keep = fresh || reduced ? 0u : page_header_detail::remaining(static_cast<u32>(dt));
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    if (fresh) {
        state.initialized = 1;
        state.track = input.track;
        state.serial = input.note_serial;
        for (u32 i = 0; i < drink::classes; ++i) state.d[i] = 0, state.s[i] = state.s0[i] = drink::clear;
        state.owed = state.tail = 0;
        state.tap_phase = 0;
        state.since_tap = state.since_stir = 1 << 20;
        state.stirred = 0;
        state.surface = height(slots[FILL]);
        state.spoon = static_cast<i32>(drink::q16(slots[SPOON]));
        state.cup = 0xffffffffu;
        state.seed = 0x2545f491u;
        state.turn = 0;
        for (u32 k = 0; k < node::pops; ++k) state.pop_age[k] = -1, state.pop_x[k] = 0;
        for (u32 k = 0; k < node::rings; ++k) state.ring_age[k] = -1;
    }
    i32 rate[drink::classes];
    drink::rates(slots[FILL], slots[FIZZ], rate);
    const i32 x0 = drink::powder_void(slots[POWDER]);

    // The voices sound while a key is held, and for the amp's release after.
    if (events.hold_ended) state.tail = release;
    const i32 elapsed = fresh ? 0 : clamp(input.elapsed, 0, 1024);
    const bool was_sounding = held || state.tail > 0;
    state.tail = state.tail > elapsed ? state.tail - elapsed : 0;
    const bool sounding = held || state.tail > 0;
    state.since_tap = state.since_tap < (1 << 20) ? state.since_tap + elapsed : state.since_tap;
    state.since_stir = state.since_stir < (1 << 20) ? state.since_stir + elapsed : state.since_stir;

    // Time in blocks of 64 samples; it catches up a second a frame, so a slow
    // screen keeps time with the voices.
    state.owed += elapsed * 11025 / 16;
    const i32 blocks = sounding ? state.owed >> 10 : 0;
    state.owed &= 1023;

    // A note stirs the drink and taps at once, as the voices do: after a
    // pause the drink has settled, and is stirred afresh.
    bool tapped = false;
    if (!fresh && input.note_serial != state.serial) {
        const bool settled = !was_sounding;
        if (settled)
            for (u32 i = 0; i < drink::classes; ++i) state.d[i] = 0, state.s[i] = drink::clear;
        const i32 stir = settled ? 32767 : drink::restir(slots[STIR]);
        drink::stir(state.d, state.s, x0, stir);
        for (u32 i = 0; i < drink::classes; ++i) state.s0[i] = state.s[i];
        state.tap_phase = 0;
        state.since_stir = 0;
        state.stirred = stir;
        tapped = true;
    }
    state.serial = input.note_serial;
    const u32 step = drink::tap_step(slots[TAPS]);
    if (blocks > 0) {
        drink::advance(state.s, rate, static_cast<u32>(blocks));
        if (step)
            for (i32 b = 0; b < blocks; ++b) {
                const u32 before = state.tap_phase;
                state.tap_phase += step;
                if (state.tap_phase < before) tapped = true;
            }
    }
    if (tapped) state.since_tap = 0;

    const i32 cx = scene::cx * unit, floor = scene::bottom * unit;
    // ---- The vessel, its handle and its material's marks.
    if (cup != state.cup) {
        Point points[64];
        for (u32 i = 0; i < 64; ++i) points[i].x = plate::vessel[cup][i][0], points[i].y = plate::vessel[cup][i][1];
        outline(frame, node::vessel, points, state.key, true);
        state.cup = cup;
    }
    put(frame, node::vessel, cx, scene::vessel_y * unit, 2 * unit, grey::mug, stroke);
    show_if(frame, node::handle_china, cup == 0, grey::mug, stroke);
    show_if(frame, node::handle_tin, cup == 2, grey::mug, stroke);

    // ---- The drink: its surface eased to the fill, curling up at the walls;
    // the stir's vortex (a parabola that keeps the drink's volume) and each
    // tap's ripple. Drawn at twice.
    const i32 depth = height(slots[FILL]);
    state.surface = keep ? ease(state.surface, depth, keep) : depth;
    if (magnitude(state.surface - depth) < 128) state.surface = depth;
    const i32 h = state.surface, level = floor - h;
    {
        // The stirred drink's standing vortex, to 12 px (stir), and a note's stir on top.
        i32 vortex = static_cast<i32>(drink::q16(slots[STIR]) * 12 >> 6), ripple = 0;
        if (!reduced) {
            if (state.since_stir < 720)   // 0.7 s
                vortex += ((state.stirred * 8) >> 5) * (720 - state.since_stir) / 720;   // Q10 px, at most 8
            if (state.since_tap < 400) {
                const i32 fade = (400 - state.since_tap) * 1536 / 400;                     // 1.5 px, Q10
                ripple = (fade * sine(state.since_tap * 3600 * 5 / 1024)) >> 10;            // 5 Hz
            }
        }
        const i32 meniscus[4] = {2560, 1280, 580, 230};   // Q10 px up, at the wall and inward
        Point points[64];
        for (u32 i = 0; i < 32; ++i) {
            const i32 u = -scene::inner * unit + 410 + static_cast<i32>(i) * (2 * scene::inner * unit - 820) / 31;
            const i32 w = u * 32 / scene::inner >> 10;                                        // -32..32
            i32 y = -(i < 4 ? meniscus[i] : i > 27 ? meniscus[31 - i] : 0);
            const i32 v = 32 - (w < 0 ? -w : w);
            y += vortex * (v * v * 3 - 1024) / 3072;                                         // a funnel, D ((1 - |u|/a)^2 - 1/3)
            y += (ripple * sine(static_cast<i32>(i) * 3600 * 3 / 31)) >> 10;
            points[i].x = u >> 1;
            points[i].y = y >> 1;
            points[63 - i].x = u >> 1;
            points[63 - i].y = (y >> 1) + 184;   // the ribbon's width, at twice
        }
        outline(frame, node::surface, points, state.key, fresh);
        put(frame, node::surface, cx, level, 2 * unit, grey::spoon, stroke);
    }
    // The drink's lines: every fourth for pale milk (powder 0) to every one
    // for thick chocolate; each frothy while bubbles fill the drink at its
    // height (more than would flatten the note 3 cents), clear once they leave.
    {
        const u32 every = 4 - ((drink::q16(slots[POWDER]) * 4) >> 16);   // 4, 3, 2, 1
        u32 lines = 0;
        for (u32 k = 0; k < node::waters; ++k) {
            const i32 y = floor - static_cast<i32>(scene::water_step * unit * (10 * k + 6) / 10);
            const bool show = y > level + 3 * unit && k % (every ? every : 1) == 0;
            bool frothy = false;
            if (show) {
                const i32 z = static_cast<i32>((static_cast<u32>(floor - y) << 10) / static_cast<u32>(h >> 10)) << 20;   // Q30 of the drink
                i32 there = 0;
                for (u32 i = 0; i < drink::classes; ++i)
                    if (state.s[i] < drink::clear && state.s[i] <= z) there += state.d[i];
                frothy = there > froth_void;
            }
            // A line that changes costs two nodes; at most six change a frame.
            const bool was = shown(frame, node::water + k), was_frothy = shown(frame, node::froth + k);
            if ((was != (show && !frothy) || was_frothy != (show && frothy)) && !fresh) {
                if (lines >= 6) continue;
                ++lines;
            }
            show_if(frame, node::water + k, show && !frothy, grey::quiet, stroke);
            show_if(frame, node::froth + k, show && frothy, grey::detail, stroke);
        }
    }
    // Under half stirred (stir), powder lies in a heap on the floor and clumps
    // hang in the drink, gone by 50; stirred, the drink turns in a funnel of
    // swirls, one more each quarter.
    {
        const u32 stir = drink::q16(slots[STIR]), left = stir < 32768 ? 65535 - 2 * stir : 0;
        const i32 heap = static_cast<i32>(left >> 6);   // Q10 scale
        if (heap < 120) conceal(frame, node::heap);
        else put(frame, node::heap, cx + scene::heap_x * unit, floor, heap / 16 * 16, grey::quiet, fill_and_stroke);
        // Clumps at shares of the drink's depth (Q10), and swirls narrowing to the floor.
        const i32 clump_up[3] = {300, 520, 160}, clump_x[3] = {40, 46, 18};   // clear of the spoon's way
        const u32 clumps = (left * 3 + 32768) >> 16;
        for (u32 k = 0; k < node::clumps; ++k) {
            const i32 y = (floor - (h >> 10) * clump_up[k]) / unit * unit;
            if (k >= clumps || y < level + 5 * unit) { conceal(frame, node::clump + k); continue; }
            put(frame, node::clump + k, cx + clump_x[k] * unit, y, unit, grey::quiet, fill_and_stroke);
        }
        const i32 swirl_up[4] = {800, 590, 390, 200}, swirl_size[4] = {1024, 880, 740, 600};
        const u32 swirls = (stir * 4 + 32768) >> 16;
        for (u32 k = 0; k < node::swirls; ++k) {
            const i32 y = (floor - (h >> 10) * swirl_up[k]) / unit * unit;
            if (k >= swirls || y < level + 6 * unit) { conceal(frame, node::swirl + k); continue; }
            put(frame, node::swirl + k, cx, y, swirl_size[k], grey::detail, stroke);
        }
    }

    // ---- The spoon: its shape eased to the control, a short wooden spoon
    // leaning on the wall to a long silver one resting on the rim; rocked
    // about where it leans, its bowl lifted by taps' rest (24 degrees for a
    // single tap to 2 for the fastest) and brought down onto the floor at each
    // tap. Each tap ripples across the floor; tapping fast, the bowl shakes.
    {
        const i32 target = static_cast<i32>(drink::q16(slots[SPOON]));
        state.spoon = keep ? ease(state.spoon, target, keep) : target;
        if (magnitude(state.spoon - target) < 256) state.spoon = target;
        const i32 m = state.spoon >> 6;   // Q10
        Point points[64];
        morph(points, plate::bowl[0], plate::bowl[1], m);
        outline(frame, node::bowl, points, state.key, fresh);
        morph(points, plate::handle[0], plate::handle[1], m);
        outline(frame, node::handle, points, state.key, fresh);

        const i32 rest = 240 - static_cast<i32>((220u * drink::q16(slots[TAPS])) >> 16);   // tenths of a degree
        const i32 swing = rest > 40 ? rest : 40;   // tapping, it always swings 4 degrees or more
        i32 lift = rest;
        const bool tapping = !reduced && sounding && step;
        if (tapping) {
            const i32 phase = static_cast<i32>(state.tap_phase >> 16);   // Q16 of the period
            lift = swing;
            if (phase < 22938) {          // lifting, easing out, to 0.35
                const i32 r = 65536 - phase * 65536 / 22938;
                lift = swing - static_cast<i32>((static_cast<u32>(swing) * static_cast<u32>((r >> 8) * (r >> 8))) >> 16);
            } else if (phase > 52429) {   // coming down, from 0.8
                const i32 r = (phase - 52429) * 65536 / 13107;
                lift = swing - static_cast<i32>((static_cast<u32>(swing) * static_cast<u32>((r >> 8) * (r >> 8))) >> 16);
            }
        } else if (!reduced && sounding && state.since_tap < 256) {
            const i32 r = 1024 - state.since_tap * 4;   // back up in a quarter second
            lift = rest - rest * (r * r >> 10) / 1024;
        }
        lift = lift / 5 * 5;   // in half degrees, to change less often
        // Its rest on this cup, leaning on the rim's lip or the wall without
        // crossing them (plate.h), and the tip turned about the lip by the lift.
        const i32 tip_rest_y = along(plate::tip_y[cup], m), lean = along(plate::lean[cup], m);
        const i32 pivot_x = along(plate::pivot_x[cup], m), pivot_y = along(plate::pivot_y[cup], m);
        const Turn rock = rotation(lift);
        const i32 ox = (plate::tip_x - pivot_x) >> 4, oy = (tip_rest_y - pivot_y) >> 4;   // Q6
        const i32 tip_x = pivot_x + ((ox * rock.cos - oy * rock.sin) >> 6);
        const i32 tip_y = pivot_y + ((ox * rock.sin + oy * rock.cos) >> 6);
        const Turn t = rotation(lean + lift);
        const i32 mid_x = tip_x + scene::handle_mid * t.cos, mid_y = tip_y + scene::handle_mid * t.sin;
        put_turned(frame, node::bowl, tip_x, tip_y, 2 * unit, t, grey::spoon);
        put_turned(frame, node::handle, mid_x, mid_y, 2 * unit, t, grey::spoon);
        // Silver's bowl shines; wood is grained along its handle and in its bowl.
        const bool silver = m >= 512;
        if (silver) put_turned(frame, node::shine, tip_x, tip_y, unit, t, grey::detail);
        else conceal(frame, node::shine);
        for (u32 k = 0; k < node::grains; ++k) {
            if (silver) { conceal(frame, node::grain + k); continue; }
            if (k == 0) put_turned(frame, node::grain, mid_x, mid_y, unit, t, grey::detail);
            else put_turned(frame, node::grain + k, tip_x, tip_y, unit, t, grey::detail);
        }
        // Tapping fast, strokes beside the bowl: one from 1.5 taps a second, three from 8.
        const u32 tenths_hz = step ? drink::tap_tenths(slots[TAPS]) : 0;
        const u32 shakes = !tapping ? 0 : tenths_hz >= 80 ? 3 : tenths_hz >= 40 ? 2 : tenths_hz >= 15 ? 1 : 0;
        for (u32 k = 0; k < node::shakes; ++k) {
            if (k < shakes) put_turned(frame, node::shake + k, tip_x, tip_y, unit, t, grey::detail);
            else conceal(frame, node::shake + k);
        }
        // Each tap sends a ring across the floor from under the bowl, for 0.35 s.
        if (tapped && !reduced) {
            u32 slot = 0;
            for (u32 k = 1; k < node::rings; ++k)
                if (state.ring_age[k] < 0 || (state.ring_age[slot] >= 0 && state.ring_age[k] > state.ring_age[slot])) slot = k;
            state.ring_age[slot] = 0;
        }
        for (u32 k = 0; k < node::rings; ++k) {
            const i32 age = state.ring_age[k];
            if (age < 0 || age > 358 || reduced) { state.ring_age[k] = -1; conceal(frame, node::ring + k); continue; }
            put(frame, node::ring + k, plate::tip_x + 8 * unit, floor - unit, 1024 + age * 14 / 5, grey::detail, stroke);
            state.ring_age[k] = age + elapsed;
        }
    }

    // ---- The bubbles: class i's five rings at fixed places across the mug
    // and up its column, shown for its crowd, rising as its clearing does.
    {
        const i32 grow = 512 + static_cast<i32>(1536u * drink::q16(slots[FIZZ]) >> 16);   // Q10: 0.5 to 2
        u32 moved = 0;
        for (u32 n = 0; n < node::bubbles; ++n) {
            const u32 b = (state.turn + n) % node::bubbles, i = b / 5, j = b % 5;
            // Rings for the class: 5 sqrt(drop / 4 octaves), so small scoops still show bubbles.
            const i32 c25 = 25 * crowd(state.d[i], i);
            i32 count = 0;
            while (count < 5 && (4 * count * count + 4 * count + 1) * 65536 <= c25) ++count;
            const u32 id = node::bubble + b;
            i32 y = 0;
            bool show = static_cast<i32>(j) < count && state.s[i] < drink::clear;
            if (show) {
                const i32 z = state.s[i] + ((drink::clear - state.s0[i]) >> 10) * plate::up[b];   // Q30 of the column
                show = z < drink::clear - (1 << 25);
                y = floor - static_cast<i32>((static_cast<u32>(z) >> 15) * static_cast<u32>(h) >> 15);
            }
            if (!show) { conceal(frame, id); continue; }
            // Radius 1.5 px (the finest that shows on the screen) to 4 px; authored at 2.
            const i32 r = clamp(((1434 + 256 * static_cast<i32>(i)) * grow) >> 10, 1536, 4096) >> 1;
            const Node& d = frame.nodes[id];
            const bool near = shown(frame, id) && magnitude(d.y - y) < 2 * unit && d.scale == r;
            if (near && !fresh) continue;
            if (moved >= budget && !fresh) continue;
            ++moved;
            put(frame, id, cx + plate::across[b], y, r, grey::detail, stroke);
        }
        state.turn = (state.turn + 7) % node::bubbles;
    }

    // ---- The foam: pops' amount of fizz's size along the surface.
    {
        const u32 foams = (drink::q16(slots[POPS]) * node::foams + 32768) >> 16;
        // Authored 3 px; 2 px for the finest bubbles to 4 px for the largest.
        const i32 r = 683 + static_cast<i32>((683u * drink::q16(slots[FIZZ])) >> 16);   // scale, Q10
        const i32 radius = 3 * r;                                                         // Q10 px
        for (u32 k = 0; k < node::foams; ++k) {
            const u32 id = node::foam + k;
            if (k >= foams) { conceal(frame, id); continue; }
            const u32 row = k / 10, at = (k * 7 + row * 5) % 10;
            const i32 u = (-scene::inner + 6) * unit + static_cast<i32>(at) * ((2 * scene::inner - 12) * unit / 9) + static_cast<i32>(row) * radius;
            const i32 lift = radius * (1 + 2 * static_cast<i32>(row));   // the second row sits on the first
            put(frame, id, cx + u, (level - lift) / unit * unit, r, grey::detail, stroke);
        }
    }

    // ---- Pops: rings bursting from the surface, as often as the voices pop.
    {
        u32 total = 0;
        for (u32 i = 0; i < drink::classes; ++i)
            if (state.s[i] < drink::clear) total += static_cast<u32>(state.d[i] >> 12) * static_cast<u32>(rate[i] >> 12);
        const u32 chance = ((((total >> 8) * 750u) >> 9) * static_cast<u32>(drink::q16(slots[POPS]) >> 1)) >> 15;
        for (i32 b = 0; b < blocks && b < 64; ++b) {
            if (!total || (next(state.seed) & 0xffff) >= chance) continue;
            for (u32 k = 0; k < node::pops; ++k)
                if (state.pop_age[k] < 0) {
                    state.pop_age[k] = 0;
                    state.pop_x[k] = (static_cast<i32>(next(state.seed) >> 16) * (2 * scene::inner - 12) >> 16) - scene::inner + 6;
                    break;
                }
        }
        for (u32 k = 0; k < node::pops; ++k) {
            const u32 id = node::pop + k;
            if (state.pop_age[k] < 0 || reduced) { state.pop_age[k] = -1; conceal(frame, id); continue; }
            const i32 age = state.pop_age[k];
            if (age > 160) { state.pop_age[k] = -1; conceal(frame, id); continue; }
            put(frame, id, cx + state.pop_x[k] * unit, level - 768, 683 + age * 4, grey::detail, stroke);
            state.pop_age[k] = age + elapsed;
        }
    }
}
}
