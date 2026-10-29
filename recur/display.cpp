// Recur's screen: a clockwork tin picture puzzle. Each map has its own print,
// a bold picture named for it (the cat of Arnold and Avez, a loaf, a nautilus,
// a silver moon, scissors, a honeycomb), carried in pieces on the grid's square
// tiles; they slide to where the map puts the slices they stand for, as the
// sound plays them, and come home together at the period. The knob is the
// map, the pendulum's weight slice, the lever rate, the comb's plucked teeth
// and the print's outline again under the tiles chrd, the pinned and darkened
// tiles depth, the lit stretch of the sample's tape pos and the window's
// length, and the key turns a click an iteration. With no sample, a
// cover says how to load one. Drawn from the controls, the notes and time
// through recur.h, not read from the audio.
#include "display.h"
#include "recur.h"
#include <opxy/sample_overview.hpp>

namespace recur {
namespace {
using namespace opxy::draw;

// Shades (palette entries, black to white in 16 steps).
const u32 moving_shade = 15, held_shade = 5, part = 11, pin_shade = 14, tooth_lit = 15, tooth_dark = 6;
const u32 ghost_shade = 9, bar_lit = 15, bar_dim = 6, bar_outside = 4;
// All of a frame's changes to tiles and pins, sliding ones included: the frame's churn.
const u32 most_changes = 20;
// A lit comb tooth stands this much longer than the rest, Q10.
const i32 plucked = 1331;
// The display has no tempo: it times slices at 120 BPM, 31.25 ms a 64th note.
const u32 sixty_fourths_a_second = 32, us_a_64th = 31250;

const char* const map_names[map_count] = {"cat", "baker", "gold", "pell", "shear", "hex"};
const char* const chord_names[chord_count] = {"off", "dtun", "oct", "5th", "maj", "min", "harm"};
const char* const slice_names[slice_count] = {"hiss", "1/64", "1/32", "1/16", "1/8", "1/4"};
const char* const rate_names[rate_count] = {"off", "4 bar", "2 bar", "1 bar", "1/2", "1/4", "1/8", "1/16"};
const char* const grid_names[grid_count] = {"2x2", "4x4", "8x8"};
// Each chrd's readers, in semitones above the note (dtun: the note and a tooth up).
const u32 chord_notes[chord_count][3] = {{0, 0, 0}, {0, 1, 0}, {0, 12, 0}, {0, 7, 0}, {0, 4, 7}, {0, 3, 7}, {0, 12, 19}};
const u32 chord_readers[chord_count] = {1, 2, 2, 2, 3, 3, 3};

char* put(char* out, u32 n) {
    char digits[10];
    u32 count = 0;
    do { digits[count++] = char('0' + n % 10); n /= 10; } while (n);
    while (count) *out++ = digits[--count];
    return out;
}

// The sample as the screen knows it, or none.
struct Sample { u32 frames, begin, span, rate; bool shown; };
Sample read_sample(const SampleOverview& o) {
    if (!o.assigned || !o.frames || !o.rate) return Sample{0, 0, 0, 44100, false};
    Sample s;
    s.frames = o.frames;
    s.begin = o.begin < o.frames ? o.begin : 0;
    const u32 end = o.end && o.end <= o.frames ? o.end : o.frames;
    s.span = end > s.begin ? end - s.begin : 0;
    s.rate = o.rate;
    s.shown = true;
    return s;
}

// a * b / c for b below 2^15, no 64-bit product: c's low bits drop as needed.
u32 scaled(u32 a, u32 b, u32 c) {
    while (c >= 65536) { a >>= 1; c >>= 1; }
    return c ? (a / c) * b + (a % c) * b / c : 0;
}

// The time-based ease: the share of a distance left after ms, Q10.
i32 remaining(u32 ms) { return i32(page_header_detail::remaining(ms < 400 ? ms : 400) >> 6); }

// A slot's centre on screen, Q10.
void slot_centre(u32 bits, u32 slot, i32& x, i32& y) {
    const u32 side = 1u << bits;
    const i32 size = (tray_side * unit) >> bits;
    x = tray_x * unit + i32(slot & (side - 1)) * size + size / 2;
    y = tray_y * unit + i32(slot >> bits) * size + size / 2;
}

bool shown(const Frame& frame, u32 node) { return frame.nodes[node].x != hidden_x * unit || frame.nodes[node].y != hidden_y * unit; }
bool changed(const Frame& frame, u32 node, i32 x, i32 y, u32 shade, i32 scale = unit, Turn t = Turn{unit, 0}, Paint paint = stroke) {
    const Node& n = frame.nodes[node];
    return n.x != x || n.y != y || n.shade != shade || n.paint != paint || n.scale != scale || n.cos != t.cos || n.sin != t.sin;
}
void put(Frame& frame, u32 node, i32 x, i32 y, u32 shade, i32 scale = unit, Turn t = Turn{unit, 0}, Paint paint = stroke) {
    place(frame, node, x, y, scale, shade, paint);
    turn(frame, node, t);
}

void place_header(DisplayState& state, const Input& input, Frame& frame, const Sample& sample, u32 k, u32 period) {
    const char* const titles[8] = {"pos", "grid", "slice", "map", "step", "rate", "depth", "chrd"};
    char values[8][header_capacity] = {};
    if (sample.shown) {
        // Seconds into the sample, to 5 characters: 0.62s, 12.4s, 123s.
        const u32 per_ms = sample.rate / 1000 ? sample.rate / 1000 : 1;
        const u32 hundredths = scaled(sample.span, level(input.slots[kPos]) >> 4, 4096) / (per_ms * 10);
        char* at = values[kPos];
        if (hundredths < 1000) { at = put(at, hundredths / 100); *at++ = '.'; at = put(at, hundredths / 10 % 10); at = put(at, hundredths % 10); }
        else if (hundredths < 10000) { at = put(at, hundredths / 100); *at++ = '.'; at = put(at, hundredths / 10 % 10); }
        else at = put(at, hundredths / 100);
        *at++ = 's'; *at = 0;
    } else header_amount(values[kPos], header_capacity, input.slots[kPos]);
    copy(values[kGrid], header_capacity, grid_names[choice(input.slots[kGrid], grid_count)]);
    copy(values[kSlice], header_capacity, slice_names[choice(input.slots[kSlice], slice_count)]);
    copy(values[kMap], header_capacity, map_names[choice(input.slots[kMap], map_count)]);
    // The iteration of the period: 2/3, 11/96; past 5 characters, the iteration alone.
    char* at = put(values[kStep], k);
    if (period < 100 || k < 10) { *at++ = '/'; at = put(at, period); }
    *at = 0;
    copy(values[kRate], header_capacity, rate_names[choice(input.slots[kRate], rate_count)]);
    header_amount(values[kDepth], header_capacity, input.slots[kDepth]);
    at = values[kDepth];
    while (*at) ++at;
    *at++ = '%'; *at = 0;
    copy(values[kChrd], header_capacity, chord_names[choice(input.slots[kChrd], chord_count)]);
    page_header(state.header, input, frame, titles, values, 0);
}

// The tiles: plain squares, sliding from slot to slot as the map moves the
// slices they stand for, easing out over 0.16 s, with the map's print riding
// on them a piece a cell. Changes are spent from a budget each frame, in turn
// round the tray, so a big reshuffle ripples across it; a sliding tile and its
// pieces always move together.
void place_tiles(DisplayState& state, Frame& frame, u32 bits, u32 map, u32 k, const u32 (&moving)[2], bool hiss,
                 u32 hiss_bits, u32 depth, u32 chord, u32 ms, bool snap, u32& budget) {
    const u32 side = 1u << bits, cells = side * side;
    if (state.bits != bits) {   // a new grid: its tiles start at home
        state.bits = bits;
        for (u32 c = 0; c < 64; ++c) state.slides[c] = Slide{opxy::u8(c), opxy::u8(c), at_rest};
        state.hiss_key = 0;
    }
    // Inks: a moving tile bright, a held one dark; in hiss, each tile by how
    // much of it is heard in place: the part back home, and depth's plain window.
    const u32 key = (hiss ? map << 24 | hiss_bits << 16 | k : 1u << 31) ^ (moving[0] * 2654435761u) ^ moving[1] ^ bits ^ depth << 7;
    if (key != state.hiss_key) {
        const u32 fine = 1u << hiss_bits, per = fine >> bits;   // sub-cells a tile a side
        const Mat m = power(map, k, hiss_bits);
        for (u32 c = 0; c < cells; ++c) {
            u32 ink = moves(moving, c) ? moving_shade : held_shade;
            if (hiss) {
                u32 home = 0;
                for (u32 y = 0; y < per; ++y)
                    for (u32 x = 0; x < per; ++x) {
                        const u32 cx = (c & (side - 1)) * per + x, cy = (c >> bits) * per + y;
                        u32 mx = cx, my = cy;
                        send(m, map, hiss_bits, mx, my);
                        home += mx == cx && my == cy;
                    }
                const u32 cells_in = per * per, plain = (65536 - depth) >> 8;   // Q8
                ink = 2 + (home * 256 + plain * (cells_in - home)) * (moving_shade - 2) / (cells_in * 256);
            }
            state.inks[c] = opxy::u8(ink);
        }
        state.hiss_key = key;
    }
    // A tile's slide costs the tile and the pieces of the print it carries.
    const u32 shift = 3 - bits;
    u32 cost[64];
    for (u32 c = 0; c < cells; ++c) cost[c] = 1;
    for (u32 j = 0; j < print_count[map]; ++j) {
        const u32 at = piece_cell[print_first[map] - print_first[0] + j];
        ++cost[((at >> 3) >> shift) * side + ((at & 7) >> shift)];
    }
    const Mat forward = power(map, k, bits);
    u32 sliding = 0;
    for (u32 c = 0; c < cells; ++c) sliding += state.slides[c].ms != at_rest ? cost[c] : 0;
    budget = budget > sliding ? budget - sliding : 0;   // slides under way spend the budget first
    i32 at_x[64], at_y[64];
    bool gliding[64];
    const i32 square_scale = bits == 2 ? 2 * unit : unit;
    for (u32 n = 0; n < cells; ++n) {
        const u32 c = (n + state.sweep) % cells;
        Slide& s = state.slides[c];
        u32 target = c;
        if (!hiss && moves(moving, c)) {
            u32 x = c & (side - 1), y = c >> bits;
            send(forward, map, bits, x, y);
            target = y * side + x;
        }
        const bool was_sliding = s.ms != at_rest;
        if (snap) s = Slide{opxy::u8(target), opxy::u8(target), at_rest};
        else if (!was_sliding && s.to != target && (budget >= cost[c] || (budget && !sliding))) {
            s = Slide{s.to, opxy::u8(target), 0};
            budget = budget > cost[c] ? budget - cost[c] : 0;
            sliding += cost[c];
        }
        i32 x, y;
        slot_centre(bits, s.to, x, y);
        if (s.ms != at_rest) {
            s.ms = opxy::u16(s.ms + ms);
            if (s.ms >= 160) s.ms = at_rest;
            else {
                i32 fx, fy;
                slot_centre(bits, s.from, fx, fy);
                const i32 left = remaining(s.ms);
                x += ((fx - x) * left) >> 10;
                y += ((fy - y) * left) >> 10;
            }
        }
        at_x[c] = x;
        at_y[c] = y;
        gliding[c] = was_sliding || s.ms != at_rest;
        // The square a shade under its print, so the picture reads over the grid.
        const u32 node = (bits == 1 ? large_first : small_first) + c, ink = state.inks[c] * 5 / 8;
        if (!changed(frame, node, x, y, ink, square_scale)) continue;
        if (!snap && !gliding[c]) { if (!budget) continue; --budget; }
        put(frame, node, x, y, ink, square_scale);
    }
    state.sweep = (state.sweep + 7) % cells;
    // Squares this grid doesn't use go.
    for (u32 c = 0; c < 68; ++c) {
        const u32 node = small_first + c;
        const bool used = c < 64 ? bits > 1 && c < cells : bits == 1;
        if (!used && shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
    }
    // The map's print rides on the tiles, a piece in each cell of the 8 grid
    // it crosses; the other prints go.
    const i32 cell = (tray_side * unit) >> 3;
    for (u32 m = 0; m < map_count; ++m)
        for (u32 j = 0; j < print_count[m]; ++j) {
            const u32 node = print_first[m] + j;
            if (m != map) {
                if (shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
                continue;
            }
            const u32 at = piece_cell[node - print_first[0]], cx = at & 7, cy = at >> 3;
            const u32 t = (cy >> shift) * side + (cx >> shift);
            i32 hx, hy;
            slot_centre(bits, t, hx, hy);
            const i32 x = at_x[t] + tray_x * unit + i32(cx) * cell + cell / 2 - hx;
            const i32 y = at_y[t] + tray_y * unit + i32(cy) * cell + cell / 2 - hy;
            const u32 ink = state.inks[t];
            if (!changed(frame, node, x, y, ink)) continue;
            if (!snap && !gliding[t]) { if (!budget) continue; --budget; }
            put(frame, node, x, y, ink);
        }
    // chrd's second reader: the print's outline again under the tiles, faint,
    // raised and shifted by the reader's interval.
    const i32 semis = i32(chord_notes[chord][1]), apart = semis < 3 ? 3 : semis;
    const i32 gx = (tray_x + tray_side / 2) * unit + apart * 614, gy = (tray_y + tray_side / 2) * unit - apart * 410;
    for (u32 m = 0, node = ghost_first; m < map_count; ++m)
        for (u32 j = 0; j < ghost_count[m]; ++j, ++node) {
            if (m != map || chord_readers[chord] < 2) {
                if (shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
                continue;
            }
            if (changed(frame, node, gx, gy, ghost_shade) && (snap || budget)) { put(frame, node, gx, gy, ghost_shade); if (!snap) --budget; }
        }
}

// depth's pins: a large one at the corner of each tile it holds still, on
// the 2 and 4 grids (on the 8, held tiles are dark without).
void place_pins(Frame& frame, u32 bits, const u32 (&moving)[2], bool hiss, bool snap, u32& budget) {
    const u32 cells = 1u << (2 * bits);
    const i32 size = (tray_side * unit) >> bits, inset = (bits == 1 ? 12 : 8) * unit, scale = bits == 1 ? 2 * unit : 1600;
    for (u32 c = 0; c < pin_count; ++c) {
        const u32 node = pin_first + c;
        if (hiss || bits > 2 || c >= cells || moves(moving, c)) {
            if (shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
            continue;
        }
        i32 x, y;
        slot_centre(bits, c, x, y);
        x += inset - size / 2;
        y += inset - size / 2;
        if (changed(frame, node, x, y, pin_shade, scale) && (snap || budget)) {
            put(frame, node, x, y, pin_shade, scale);
            if (!snap) --budget;
        }
    }
}

// The window pos picks, frames into the sample's region: in slices it starts
// at pos of the whole region and wraps round its end (to may fall below from);
// in hiss it starts at pos of the room the window leaves.
void window(const Sample& sample, const Input& input, u32 bits, u32 slice, bool hiss, u32 hiss_bits, u32& from, u32& to) {
    const u32 span = sample.span ? sample.span : 1, pos = level(input.slots[kPos]) >> 4;
    // A slice's frames at 120 BPM: 1378 a 64th note.
    const u32 length = hiss ? 1u << (2 * hiss_bits) : (1u << (2 * bits)) * slice64[slice] * 1378;
    if (length >= span) { from = 0; to = span; return; }
    if (hiss) { from = scaled(span - length + 1, pos, 4096); to = from + length; return; }
    from = scaled(span, pos, 4096);
    to = from + length < span ? from + length : from + length - span;
}

u32 root(u32 n) {
    u32 r = 0;
    for (u32 bit = 1u << 15; bit; bit >>= 1)
        if ((r + bit) * (r + bit) <= n) r += bit;
    return r;
}

// The tape: a bar for each stretch of the sample, as tall as the root of its
// peak against the loudest (so quiet stretches still show); the window's bars
// lit solid, the rest of the region dim and what lies outside it dark.
void place_tape(const Input& input, Frame& frame, const SampleOverview& overview, const Sample& sample, u32 bits, u32 slice,
                bool hiss, u32 hiss_bits, bool snap, u32 budget) {
    u32 from, to;
    window(sample, input, bits, slice, hiss, hiss_bits, from, to);
    const bool wraps = to <= from;
    const i32 width = (rule_x1 - rule_x0) * unit;
    u32 loudest = 1;
    for (u32 i = 0; i < overview_bins; ++i) if (overview.peaks[i] > loudest) loudest = overview.peaks[i];
    for (u32 i = 0; i < bar_count; ++i) {
        const u32 b0 = scaled(sample.frames, i, bar_count), b1 = scaled(sample.frames, i + 1, bar_count);
        u32 shade = bar_outside;
        bool lit = false;
        if (b1 > sample.begin && b0 < sample.begin + sample.span) {
            const u32 r0 = b0 > sample.begin ? b0 - sample.begin : 0, r1 = b1 - sample.begin;
            lit = wraps ? (r1 > from || r0 < to) : (r1 > from && r0 < to);
            shade = lit ? bar_lit : bar_dim;
        }
        u32 peak = 0;
        for (u32 b = i * overview_bins / bar_count; b < ((i + 1) * overview_bins + bar_count - 1) / bar_count; ++b)
            if (overview.peaks[b] > peak) peak = overview.peaks[b];
        const i32 tall = i32(root(peak * unit * unit / loudest)), scale = tall > 300 ? tall : 300;
        const Paint paint = lit ? fill_and_stroke : stroke;
        const u32 node = bar_first + i;
        const i32 x = rule_x0 * unit + width * i32(2 * i + 1) / i32(2 * bar_count);
        if (changed(frame, node, x, rule_y * unit, shade, scale, Turn{unit, 0}, paint) && (snap || budget)) {
            put(frame, node, x, rule_y * unit, shade, scale, Turn{unit, 0}, paint);
            if (!snap) --budget;
        }
    }
}

// The knob, the pendulum, the lever, the comb, the key and the caliper.
void place_works(DisplayState& state, const Input& input, Frame& frame, const Sample& sample, u32 bits, bool hiss,
                 u32 hiss_bits, bool held, u32 ms, bool snap) {
    const u32 map = choice(input.slots[kMap], map_count);
    place(frame, pointer_node, knob_x * unit, knob_y * unit, unit, 14, stroke);
    turn(frame, pointer_node, rotation(map_angles[map]));
    // The pendulum: its weight up the rod by slice; while a note plays it
    // swings a side each slice, less for slices too quick to follow.
    const u32 slice = choice(input.slots[kSlice], slice_count);
    i32 aim = 0;
    if (held && !input.reduced_motion) {
        const u32 us = slice64[slice] ? slice64[slice] * us_a_64th : 6000;
        const i32 amplitude = us >= 125000 ? 140 : i32(140 * us / 125000);
        const u32 t = (state.held_ms % 600000) * 1000 % (2 * us);
        aim = amplitude * sine(i32(t * 1800 / us) + 900) / unit;
    }
    state.swing = snap ? aim : aim + ((state.swing - aim) * remaining(ms) >> 10);
    const Turn swing = rotation(state.swing);
    place(frame, rod_node, pendulum_x * unit, pendulum_y * unit, unit, 12, stroke);
    turn(frame, rod_node, swing);
    const i32 height = weight_heights[slice];
    place(frame, weight_node, pendulum_x * unit + height * swing.sin, pendulum_y * unit - height * swing.cos, unit, 15, stroke);
    turn(frame, weight_node, swing);
    const u32 rate = choice(input.slots[kRate], rate_count);
    place(frame, lever_node, lever_x * unit, lever_y * unit, unit, 14, stroke);
    turn(frame, lever_node, rotation(-600 + i32(rate) * 1200 / 7));
    // The comb: the teeth of chrd's readers lit, the rest dark.
    const u32 chord = choice(input.slots[kChrd], chord_count);
    for (u32 t = 0; t < teeth; ++t) {
        bool lit = false;
        for (u32 i = 0; i < chord_readers[chord]; ++i) lit = lit || chord_notes[chord][i] == tooth_notes[t];
        place(frame, teeth_first + t, (comb_x + i32(t) * comb_pitch) * unit, comb_y * unit, lit ? plucked : unit,
              lit ? tooth_lit : tooth_dark, lit ? fill_and_stroke : stroke);
    }
    for (u32 pose = 0; pose < key_poses; ++pose) {
        if (pose == state.turns % key_poses) place(frame, key_first + pose, key_x * unit, key_y * unit, unit, held ? 14 : part, stroke);
        else hide(frame, key_first + pose);
    }
    // The caliper: the window pos picks, on the tape.
    const u32 frames = sample.frames;
    u32 from, to;
    window(sample, input, bits, slice, hiss, hiss_bits, from, to);
    const i32 width = rule_x1 - rule_x0;
    const i32 x0 = rule_x0 * unit + i32(scaled(sample.begin + from, u32(width) * 64, frames)) * 16;
    i32 x1 = rule_x0 * unit + i32(scaled(sample.begin + to, u32(width) * 64, frames)) * 16;
    if (to > from && x1 < x0 + 4 * unit) x1 = x0 + 4 * unit;
    place(frame, jaw_first, x0, rule_y * unit, unit, 15, stroke);
    place(frame, jaw_first + 1, x1, rule_y * unit, unit, 15, stroke);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.track = ~0u;
    state.fresh = 1;
    state.held_ms = 0;
    state.bits = 0;
    state.iteration = 0;
    state.turns = 0;
    state.hiss_key = 0;
    state.swing = 0;
    state.sweep = 0;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const SampleOverview& overview = *opxy_sample_overview();
    const Sample sample = read_sample(overview);
    const u32 map = choice(input.slots[kMap], map_count), bits = tile_bits(input.slots[kGrid]);
    const u32 slice = choice(input.slots[kSlice], slice_count);
    const bool hiss = slice == 0;
    u32 hiss_bits = fitting_bits(bits + fine_extra, sample.shown ? sample.span : 1u << 20);
    if (!hiss_bits) hiss_bits = smallest_fine_bits;
    const u32 period = recur::period(map, hiss ? hiss_bits : bits);

    // A track change starts afresh; a note starts at step, and rate moves it on.
    bool snap = state.fresh != 0 || input.reduced_motion;
    if (state.track != input.track) { state.track = input.track; snap = true; }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const u32 ms = u32(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
    if (events.hold_started) state.held_ms = 0;
    if (held && !input.reduced_motion) state.held_ms += ms;
    else state.held_ms = 0;
    const u32 rate = rate64[choice(input.slots[kRate], rate_count)];
    u32 moved = 0;
    if (rate && state.held_ms) {
        // Iterations change on slice boundaries (in hiss, as time goes).
        u32 sixty_fourths = state.held_ms * sixty_fourths_a_second / 1000;
        if (!hiss) sixty_fourths = sixty_fourths / slice64[slice] * slice64[slice];
        moved = sixty_fourths / rate;
    }
    const u32 start = hiss ? step_position(input.slots[kStep], period) >> iteration_bits : (level(input.slots[kStep]) * period) >> 16;
    const u32 k = (start + moved) % period;
    if (k != state.iteration && held && !snap) ++state.turns;
    state.iteration = k;

    place_header(state, input, frame, sample, k, period);
    // With no sample, a cover over everything but the header says how to load one.
    if (!overview.assigned) {
        place(frame, cover_stage, 0, 0, unit, 0, fill_and_stroke);
        hide(frame, cover_hint);
        for (u32 n = 8; n < cover_stage; ++n) hide(frame, n);
        state.bits = 0;
        state.fresh = 1;
        return;
    }
    hide(frame, cover_stage);
    place(frame, cover_hint, 0, 0, unit, 0, fill_and_stroke);
    u32 moving[2] = {~0u, ~0u};
    if (!hiss) moving_cells(map, bits, depth16(input.slots[kDepth]), moving);
    u32 budget = most_changes;
    place_tiles(state, frame, bits, map, k, moving, hiss, hiss_bits, depth16(input.slots[kDepth]), choice(input.slots[kChrd], chord_count),
                ms, snap, budget);
    place_pins(frame, bits, moving, hiss, snap, budget);
    place_tape(input, frame, overview, sample, bits, slice, hiss, hiss_bits, snap, budget > 2 ? budget : 2);   // what's left, and a little
    place_works(state, input, frame, sample, bits, hiss, hiss_bits, held, ms, snap);
    state.fresh = 0;
}
}
