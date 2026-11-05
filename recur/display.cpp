// Recur's screen: a clockwork tin picture puzzle. Each map has its own print,
// a bold picture named for it (the cat of Arnold and Avez, a loaf, a nautilus,
// a silver moon, scissors, a honeycomb), carried in pieces on the grid's square
// tiles; they slide to where the map puts the slices they stand for, as the
// sound plays them, and come home together at the period. The print is the
// map, the pendulum's weight slice, the lever rate, the comb's plucked teeth
// chrd, and the pinned and darkened tiles depth. Along the tin's foot the
// tape: the sample's waveform with the trim's markers and the grid's cuts on
// it, and a frame round the stretch the slot playing reads, which fit
// stretches to its slice or leaves at the note's pitch.
// With no sample, a cover says how to load one. Drawn from the controls, the
// notes and time through recur.h, not read from the audio.
#include "display.h"
#include "recur.h"
#include <opxy/sample_overview.hpp>

namespace recur {
namespace {
using namespace opxy::draw;

// The track's amp envelope as the screen follows it (SDK 1.10, input.amp),
// so the note's motion lasts as long as its sound. The times and shapes are
// stock OS 1.2.1's, from amp_curve.h (a byte-identical copy of the helper the
// other suites use): a larger attack or decay is a longer stage, but a larger
// release is a shorter one (a release of 100 is stock's shortest, about 6 ms).
// The level rises concavely over the attack, from wherever it is, falls
// exponentially over the decay and stops at the sustain level plus 1%, holds
// the sustain, a linear level, while a note is held, and fades exponentially
// over the release from wherever it was, to -66 dB. With the envelope
// unknown, or reduced motion, the level is 1 while a note is held and 0
// otherwise, as before.
namespace amp_env {
#include "amp_curve.h"
}
// A Q16 level (0 .. 2.0) times a Q15 factor, in 32 bits.
inline opxy::u32 scaled(opxy::u32 level, opxy::u32 factor) {
    return (level >> 15) * factor + (((level & 0x7fffu) * factor) >> 15);
}
// us: the time since the last frame. Stages: 0 idle, 1 attack, 2 decay, 3 release, 4 sustain.
inline void follow(Swell& s, const opxy::draw::Envelope& e, bool held, bool started, bool still, opxy::u32 us) {
    if (still || !opxy::draw::envelope_known(e)) { s.level = held ? 65536 : 0; s.stage = held ? 2 : 0; s.from = 0; return; }
    const opxy::u32 top = opxy::u32(amp_env::sustain_q15(e.sustain)) << 1;   // the sustain, Q16
    // A new note, or keys held while the envelope has let go, starts the attack from where the level is.
    if (started || (held && (s.stage == 0 || s.stage == 3))) s.stage = 1;
    if (s.stage == 1) {
        // L(u) = 2 (1 - 0.505^u): what is left below 2 shrinks by the attack's factor.
        s.level = 131072 - scaled(131072 - s.level, amp_env::attack_frame_q15(e.attack, us));
        if (s.level >= opxy::u32(amp_env::attack_peak_q15) << 1) { s.level = 65536; s.stage = 2; }
    } else if (s.stage == 2) {
        s.level = scaled(s.level, amp_env::decay_frame_q15(e.decay, us));
        if (s.level <= top + 655) { s.level = top; s.stage = 4; }
    } else if (s.stage == 4) {
        s.level = top;
    } else if (s.stage == 3) {
        s.level = scaled(s.level, amp_env::release_frame_q15(e.release, us));
        if (s.level < 33) { s.level = 0; s.stage = 0; }   // 0.0005, -66 dB
    }
    // The last key up (or a note too short to see held) lets go: the release runs from this level.
    if (!held && (s.stage == 1 || s.stage == 2 || s.stage == 4)) s.stage = s.level ? 3 : 0;
    s.from = 0;
}


// Shades (palette entries, black to white in 16 steps). The tape's grid: its
// rows and its other slices, bright over the waveform.
const u32 moving_shade = 15, held_shade = 5, pin_shade = 14, tooth_lit = 15, tooth_dark = 6;
const u32 row_shade = 14, cut_shade = 12;
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

// The sample as the screen knows it, or none; and its gain in stock's steps
// (-30..+20, SDK 1.10 with #400), or gain_unavailable.
struct Sample { u32 frames, begin, span, rate; bool shown; i32 gain; };
Sample read_sample(const SampleOverview& o) {
    if (!o.assigned || !o.frames || !o.rate) return Sample{0, 0, 0, 44100, false, o.gain};
    Sample s;
    s.gain = o.gain;
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

// Frames as seconds, within the 53 px a value has in the device's face:
// 0.62s, 12.4s, 123s, and from 1000 s on whole minutes, 17m ("1000s" is too
// wide).
void seconds(char* at, u32 frames, u32 rate) {
    const u32 per = rate / 100 ? rate / 100 : 1, hundredths = frames / per;
    if (hundredths < 1000) { at = put(at, hundredths / 100); *at++ = '.'; at = put(at, hundredths / 10 % 10); at = put(at, hundredths % 10); }
    else if (hundredths < 10000) { at = put(at, hundredths / 100); *at++ = '.'; at = put(at, hundredths / 10 % 10); }
    else if (hundredths < 100000) at = put(at, hundredths / 100);
    else { at = put(at, hundredths / 6000); *at++ = 'm'; *at = 0; return; }
    *at++ = 's';
    *at = 0;
}

// Shift (resources.stockShift): encoders 1, 2 and 4 turn the stock sampler's
// start, both markers together (the window, keeping its length) and end;
// encoder 3 the sample's gain.
const char* const shift_titles[4] = {"start", "window", "gain", "end"};

// The gain as stock steps it, signed: +0, -12, +20; nothing when unknown.
void gain_caption(char* out, i32 gain) {
    if (gain == gain_unavailable) { out[0] = 0; return; }
    *out++ = gain < 0 ? '-' : '+';
    out = put(out, u32(gain < 0 ? -gain : gain));
    *out = 0;
}

// The header: real values. fit 0 to 100; step the iteration and its
// fraction; depth the share of the slices that move. While
// Shift is held, the row the encoders turn names what Shift edits, as the
// stock header does: the sampler's start and end at their times in the
// sample, the window at its length, and the gain (with no gain known, its
// cell is blank).
void place_header(DisplayState& state, const Input& input, Frame& frame, const Sample& sample, u32 k, u32 ahead16,
                  u32 period, u32 moving_share) {
    const char* own[8] = {"fit", "grid", "slice", "map", "step", "rate", "depth", "chrd"};
    char values[8][header_capacity] = {};
    header_amount(values[kFit], header_capacity, input.slots[kFit]);
    copy(values[kGrid], header_capacity, grid_names[choice(input.slots[kGrid], grid_count)]);
    copy(values[kSlice], header_capacity, slice_names[choice(input.slots[kSlice], slice_count)]);
    copy(values[kMap], header_capacity, map_names[choice(input.slots[kMap], map_count)]);
    // The iteration of the period, and a tenth for a part of the next:
    // 2/3, 1.5/3, 11/96; past 5 characters, the iteration alone.
    const u32 nearest = k * 10 + ((ahead16 * 10 + 32768) >> 16), whole = nearest / 10 % period, tenths = nearest % 10;
    char* at = put(values[kStep], whole);
    if (tenths) { *at++ = '.'; at = put(at, tenths); }
    if (at - values[kStep] + 1 + (period < 10 ? 1 : period < 100 ? 2 : 3) <= 5) { *at++ = '/'; at = put(at, period); }
    *at = 0;
    copy(values[kRate], header_capacity, rate_names[choice(input.slots[kRate], rate_count)]);
    if (moving_share <= 100) { at = put(values[kDepth], moving_share); *at = 0; }
    else header_amount(values[kDepth], header_capacity, input.slots[kDepth]);
    at = values[kDepth];
    while (*at) ++at;
    *at++ = '%'; *at = 0;
    copy(values[kChrd], header_capacity, chord_names[choice(input.slots[kChrd], chord_count)]);
    if (input.shift) {
        u32 page = 0;   // as page_header finds it
        for (u32 col = 0; col < 4; ++col)
            if (page_header_slots[1][col] != page_header_slots[0][col] && input.shown[col] == page_header_slots[1][col]) page = 1;
        const u32 marks[4] = {sample.begin, sample.span, 0, sample.begin + sample.span};
        for (u32 col = 0; col < 4; ++col) {
            const u32 slot = page_header_slots[page][col];
            own[slot] = shift_titles[col];
            if (col == 2) {
                gain_caption(values[slot], sample.gain);
                if (sample.gain == gain_unavailable) own[slot] = "";
            } else if (sample.shown) seconds(values[slot], marks[col], sample.rate);
            else copy(values[slot], header_capacity, "-");
        }
    }
    const char* const titles[8] = {own[0], own[1], own[2], own[3], own[4], own[5], own[6], own[7]};
    page_header(state.header, input, frame, titles, values, header_first);
}

// The tiles: plain squares, sliding from slot to slot as the map moves the
// slices they stand for, easing out over 0.16 s, with the map's print riding
// on them a piece a cell. Changes are spent from a budget each frame, in turn
// round the tray, so a big reshuffle ripples across it; a sliding tile and its
// pieces always move together.
void place_tiles(DisplayState& state, Frame& frame, u32 bits, u32 map, u32 k, const Orbits& orbits, bool hiss,
                 u32 hiss_bits, u32 depth, u32 ms, bool snap, u32& budget, u32 bright) {
    const u32 side = 1u << bits, cells = side * side;
    if (state.bits != bits) {   // a new grid: its tiles start at home
        state.bits = bits;
        for (u32 c = 0; c < 64; ++c) state.slides[c] = Slide{opxy::u8(c), opxy::u8(c), at_rest};
        state.hiss_key = 0;
    }
    // Inks: a moving tile bright (as bright as the amp envelope lets it, in
    // four steps), a held one dark; in hiss, each tile by how much of it is
    // heard in place: the part back home, and depth's plain window.
    const u32 key = ((hiss ? map << 24 | hiss_bits << 16 | k : 1u << 31) ^ (orbits.moving[0] * 2654435761u) ^ orbits.moving[1] ^ bits ^ depth << 7) +
                    bright * 0x01000193u;
    if (key != state.hiss_key) {
        const u32 fine = 1u << hiss_bits, per = fine >> bits;   // sub-cells a tile a side
        const Mat m = power(map, k, hiss_bits);
        for (u32 c = 0; c < cells; ++c) {
            u32 ink = moves(orbits.moving, c) ? bright : held_shade;
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
                ink = 2 + (home * 256 + plain * (cells_in - home)) * (bright - 2) / (cells_in * 256);
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
    // A moving tile goes where k iterations take its slice; one a step ahead, k + 1.
    const Mat forward = power(map, k, bits), beyond = power(map, (k + 1) % recur::period(map, bits), bits);
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
        if (!hiss && moves(orbits.moving, c)) {
            u32 x = c & (side - 1), y = c >> bits;
            send(moves(orbits.ahead, c) ? beyond : forward, map, bits, x, y);
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
        // The square under its print, so the picture reads over the grid: well
        // under on the 8 grid, where the squares are many and the pieces small.
        const u32 node = (bits == 1 ? large_first : small_first) + c, ink = state.inks[c] * (bits == 3 ? 3 : 5) / 8;
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
    // it crosses, shrunk about its tile's centre to sit inside the tile's
    // square, as if printed on it; the other prints go.
    const i32 cell = (tray_side * unit) >> 3, fit = tile_fit[bits - 1];
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
            const i32 x = at_x[t] + (((tray_x * unit + i32(cx) * cell + cell / 2 - hx) * fit) >> 10);
            const i32 y = at_y[t] + (((tray_y * unit + i32(cy) * cell + cell / 2 - hy) * fit) >> 10);
            const u32 ink = state.inks[t];
            if (!changed(frame, node, x, y, ink, fit)) continue;
            if (!snap && !gliding[t]) { if (!budget) continue; --budget; }
            put(frame, node, x, y, ink, fit);
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

// log2 and 2^x of Q16 values by Mitchell's approximations: near enough for a picture.
i32 log2_q16(u32 x) {
    if (!x) return -(24 << 16);
    i32 e = 0;
    while (x >= 131072) { x >>= 1; ++e; }
    while (x < 65536) { x <<= 1; --e; }
    return e * 65536 + i32(x - 65536);
}
u32 exp2_q16(i32 l) {
    const i32 e = l >> 16;
    const u32 m = 65536 + u32(l & 0xffff);
    return e >= 15 ? 0x7fffffffu : e >= 0 ? m << e : e > -17 ? m >> -e : 0;
}

// A place in the sample, frames, as an x on the tape, Q10.
i32 tape_x(const Sample& sample, u32 frame) {
    return rule_x0 * unit + i32(scaled(frame, u32(rule_x1 - rule_x0) * 64, sample.frames ? sample.frames : 1)) * 16;
}

// The sample's view, shared byte for byte with the other sample engines
// (sample_view.h): the waveform, the trim's markers and the Shift overlay.
namespace sample_view {
#include "sample_view.h"
}
using sample_view::SampleSlot;

// Where slice c of the trim begins, frames into the sample, as the engine cuts it.
u32 cut(const Sample& sample, u32 bits, u32 c) {
    const u32 shift = 2 * bits, cells = 1u << shift;
    return sample.begin + (sample.span >> shift) * c + (((sample.span & (cells - 1)) * c) >> shift);
}

// The grid's cuts on the tape: a long tick where each row of the square
// begins and a short one at each other slice, between the trim's markers.
// Slices closer than 3 px show only their rows, and rows closer than that
// nothing; in hiss, nothing (its cells are single samples).
void place_cuts(Frame& frame, const Sample& sample, u32 bits, bool hiss, bool snap, u32& budget) {
    const u32 side = 1u << bits, cells = side * side;
    const i32 span_px = tape_x(sample, sample.begin + sample.span) - tape_x(sample, sample.begin);
    const bool slices = !hiss && span_px >= i32(cells) * 3 * unit, rows = !hiss && span_px >= i32(side) * 3 * unit;
    for (u32 r = 0; r < row_count; ++r) {
        const u32 node = row_first + r;
        if (!rows || r + 1 >= side) {
            if (shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
            continue;
        }
        const i32 x = tape_x(sample, cut(sample, bits, (r + 1) * side));
        if (changed(frame, node, x, rule_y * unit, row_shade) && (snap || budget)) { put(frame, node, x, rule_y * unit, row_shade); if (!snap) --budget; }
    }
    u32 c = 0;   // the slices within rows, in order
    for (u32 i = 0; i < cut_count; ++i) {
        const u32 node = cut_first + i;
        while (c < cells && c % side == 0) ++c;
        if (!slices || c >= cells) {
            if (shown(frame, node) && (snap || budget)) { hide(frame, node); if (!snap) --budget; }
            continue;
        }
        const i32 x = tape_x(sample, cut(sample, bits, c++));
        if (changed(frame, node, x, rule_y * unit, cut_shade) && (snap || budget)) { put(frame, node, x, rule_y * unit, cut_shade); if (!snap) --budget; }
    }
}

// The frame round the stretch read, width (Q10 px, 3 to 120) wide: a
// rectangle of 64 points round it, 16 a side, the slot's height, about its
// middle, so its points stay within the 64 px a path may reach either side of
// its node. Reshaped only when the width changes; every frame gives it a
// shape (the SDK rejects a frame with a path never shaped).
void shape_reading(Frame& frame, i32 width) {
    Frame::path_type& path = frame.paths[reading];
    const u32 key = u32(width) | 1u << 31;
    if (path.key == key) return;
    const i32 h = 9 * unit, half = width / 2;
    for (u32 i = 0; i < 16; ++i) {
        const i32 along = width * i32(i) / 16, down = 2 * h * i32(i) / 16;
        path.points[i] = Point{along - half, -h};
        path.points[16 + i] = Point{width - half, -h + down};
        path.points[32 + i] = Point{width - along - half, h};
        path.points[48 + i] = Point{-half, h - down};
    }
    path.key = key;
}

// The stretch the slot playing reads, frames: from its slice's cut, a slot
// at the note's pitch at fit 0 (more than the slice, running on past its end,
// or less, cropped), exactly the slice at fit 100, geometrically between, as
// the engine reads it at middle C. The display has no tempo: 120 BPM, as the
// pendulum ticks. In hiss, the square of single samples from the trim's start.
void place_reading(Frame& frame, const Sample& sample, const Input& input, u32 bits, bool hiss, u32 hiss_bits, u32 source, u32 bright) {
    u32 from, length;
    if (hiss) {
        from = sample.begin;
        length = 1u << (2 * hiss_bits);
    } else {
        const u32 shift = 2 * bits, slice = sample.span >> shift ? sample.span >> shift : 1;
        // A slot's frames at the sample's own rate: a 64th is 1/32 s at 120 BPM.
        const u32 own = slice64[choice(input.slots[kSlice], slice_count)] * (sample.rate / 32);
        const i32 l0 = log2_q16(scaled(own, 32768, slice) * 2);   // the slot over the slice, Q16
        const u32 fit = level(input.slots[kFit]);
        const u32 heard = exp2_q16(((l0 >> 4) * i32((65535 - fit) >> 4)) >> 8);   // over the slice, Q16
        from = cut(sample, bits, source);
        length = heard >= (64u << 16) ? slice * 64 : slice * (heard >> 16) + scaled(slice, (heard & 0xffff) >> 1, 32768);
    }
    const i32 x0 = tape_x(sample, from);
    i32 width = tape_x(sample, from + length) - x0;
    if (from + length > sample.frames) width = tape_x(sample, sample.frames) - x0;
    width = width < 3 * unit ? 3 * unit : width > 120 * unit ? 120 * unit : width;
    shape_reading(frame, width);
    place(frame, reading, x0 + width / 2, rule_y * unit, unit, bright, stroke);
}

// The pendulum, the lever and the comb.
void place_works(DisplayState& state, const Input& input, Frame& frame, bool held, u32 ms, bool snap) {
    // The pendulum: its weight up the rod by slice; while a note sounds it
    // swings a side each slice, less for slices too quick to follow, and as
    // wide as the amp envelope's level.
    const u32 slice = choice(input.slots[kSlice], slice_count);
    i32 aim = 0;
    if (held && !input.reduced_motion) {
        const u32 us = slice64[slice] ? slice64[slice] * us_a_64th : 6000;
        const i32 amplitude = (i32((us >= 125000 ? 140 : 140 * us / 125000) * (state.swell.level >> 6)) >> 10);
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
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.track = ~0u;
    state.fresh = 1;
    state.held_ms = 0;
    state.bits = 0;
    state.hiss_key = 0;
    state.swing = 0;
    state.sweep = 0;
    state.wave_key = 0;
    state.swell = Swell{0, 0, 0};
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
    // The amp envelope: the scramble, the tape and the pendulum play on while
    // the note sounds, held or in its release, and dim and narrow with it.
    follow(state.swell, input.amp, held, events.hold_started, input.reduced_motion != 0, u32(clamp(input.elapsed, 0, 256)) * 15625u >> 4);
    const bool sounding = held || state.swell.level;
    const u32 bright = sounding ? 6 + 3 * ((state.swell.level * 3 + 32768) >> 16) : moving_shade;   // 6, 9, 12, 15
    if (sounding && !input.reduced_motion) state.held_ms += ms;
    else state.held_ms = 0;
    const u32 rate = rate64[choice(input.slots[kRate], rate_count)];
    u32 moved = 0;
    if (rate && state.held_ms) {
        // Iterations change on slice boundaries (in hiss, as time goes).
        u32 sixty_fourths = state.held_ms * sixty_fourths_a_second / 1000;
        if (!hiss) sixty_fourths = sixty_fourths / slice64[slice] * slice64[slice];
        moved = sixty_fourths / rate;
    }
    const u32 position = step_position(input.slots[kStep], period) >> 4;   // Q16 iterations
    const u32 start = hiss ? step_position(input.slots[kStep], period) >> iteration_bits : position >> 16;
    const u32 ahead16 = hiss ? 0 : position & 0xffff;
    const u32 k = (start + moved) % period;

    Orbits orbits = {{~0u, ~0u}, {0, 0}, 0};
    if (!hiss) rank_orbits(map, bits, depth16(input.slots[kDepth]), ahead16, orbits);
    const u32 cells = 1u << (2 * bits);
    place_header(state, input, frame, sample, k, ahead16, period, hiss ? ~0u : (orbits.moving_cells * 100 + cells / 2) / cells);
    // With no sample, a cover over everything but the header says how to load one.
    if (!overview.assigned) {
        place(frame, cover_stage, 0, 0, unit, 0, fill_and_stroke);
        hide(frame, cover_hint);
        for (u32 n = 0; n < cover_stage; ++n)
            if (n < header_first || n >= header_first + 8) hide(frame, n);
        if (!frame.paths[reading].key) shape_reading(frame, 3 * unit);   // hidden, but shaped
        sample_view::place_shift_overlay(frame, overlay_cover, overlay_icon, overlay_text, false);
        state.bits = 0;
        state.fresh = 1;
        state.wave_key = 0;
        return;
    }
    hide(frame, cover_stage);
    place(frame, cover_hint, 0, 0, unit, 0, fill_and_stroke);
    u32 budget = most_changes;
    place_tiles(state, frame, bits, map, k, orbits, hiss, hiss_bits, depth16(input.slots[kDepth]), ms, snap, budget, bright);
    place_pins(frame, bits, orbits.moving, hiss, snap, budget);
    // The tape. The slot playing (the first at rest, as time goes while a note
    // is held) reads the slice the map put there, as the engine finds it.
    u32 slot = 0;
    if (!hiss && state.held_ms) slot = (state.held_ms * sixty_fourths_a_second / 1000 / slice64[slice]) % cells;
    u32 source = slot;
    if (!hiss && moves(orbits.moving, slot)) {
        const u32 at = moves(orbits.ahead, slot) ? (k + 1) % period : k;
        u32 x = slot & ((1u << bits) - 1), y = slot >> bits;
        send(power(map, (period - at) % period, bits), map, bits, x, y);
        source = (y << bits) | x;
    }
    // The tape's waveform (the sample view's columns, at most 12 changing a
    // frame while the sample, its trim or its gain changes), the trim's
    // markers, lit while Shift edits them, and while Shift is held the tin
    // dimmed under a cover and how to swap the sample over it, the tape and
    // the header staying bright.
    const SampleSlot tape = {rule_x0 * unit, (rule_x1 - rule_x0) * unit, rule_y * unit, 8 * unit, 9, 4, 3};
    sample_view::place_waveform(frame, sample_line, overview, tape, state.wave_key, snap, 12);
    sample_view::place_trim_marks(frame, mark_first, overview, tape, input.shift ? 15 : 9);
    sample_view::place_shift_overlay(frame, overlay_cover, overlay_icon, overlay_text, input.shift != 0);
    u32 left = budget > 4 ? budget : 4;   // what's left, and a little
    place_cuts(frame, sample, bits, hiss, snap, left);
    place_reading(frame, sample, input, bits, hiss, hiss_bits, source, bright);
    place_works(state, input, frame, sounding, ms, snap);
    state.fresh = 0;
}
}
