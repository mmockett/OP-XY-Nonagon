#include "display.h"

// Drop's screen: an engraved plate (tools/make_panel.py), a No. 8 cast-iron
// skillet seen from 30 degrees above, drawn in three weights: the drops
// brightest and boldest, the skillet's outline a step down, a few dim strokes.
//
// Each note lands a drop on it. The screen cannot hear the voices: it keeps
// its own drops, stepped with the voices' own physics (leidenfrost.h) for
// each 64 samples of time since their notes, so each drop's radius, lobes and
// end are the sound's. A floating drop is its rim, a star of its lobes,
// breathing in and out (16 times slower than it beats, to be seen), raised on
// its vapour layer over its bright underside, its dark shadow on the iron.
// At each lobe it gives up, the star fades and grows back with one fewer, and
// a spark flies, as the sound clicks. Its last, round life shrinks and
// quickens, and it is gone. Under the Leidenfrost point it is a flattened
// puddle, spraying, gone in a moment. On the ratchet it runs round the teeth.
//
// Every control shows: heat in the rings tempered into the iron (or frost)
// and in the drop's state; size in the water mark where the next drop
// lands and in the drops; liquid in their radius and how fast they breathe;
// crackle in the spatter dried on the iron (seen with no note playing) and the
// sparks; groove in the teeth and the drops' running; vapour in the gap
// between each drop and its shadow; life in the
// steam's plume and how fast they shrink; seed in the water
// stars' turns, the puddles' shapes and the hops. Integer
// arithmetic only.
namespace drop {
// The SDK's draw::sine, kept local to this machine: the
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

namespace screen {
using namespace opxy::draw;
using physics::octave;
const i32 site[scene::most] = {2700, 1500, 300};   // where each drop lands round the track, tenths of a degree
const i32 star_amount = 200, pair_amount = 300, round_amount = 80;   // the lobes' depth, Q10 of the radius
const i32 spark_time = 72;                          // a spark shows 70 ms, Q10 s

inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
// A length at the drop's scale, Q10 px, from its log2 in mm (Q16).
inline i32 drop_px(i32 log2_mm) {
    const u32 mm = physics::exp2_q16(log2_mm + 16 * octave);   // Q16 mm
    return static_cast<i32>((mm >> 6) * static_cast<u32>(scene::drop_scale) >> 10);
}
// How far round the track a drop has run, tenths of a degree: a tooth is
// 1.5 mm of its 50 mm radius, 17.19 tenths.
inline i32 travel_angle(u32 travel) {
    const u32 whole = travel >> 16, part = (travel & 0xffffu) * 1719u >> 16;
    return static_cast<i32>((whole * 1719u + part) / 100u % 3600u);
}
// mm (Q10) at the drop's scale, Q10 px.
inline i32 mm_px(i32 mm) { return mm * scene::drop_scale >> 10; }
inline u32 root(u32 v) {
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
inline u32 next(u32& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
// Places a node upright when it is not already so.
inline void put(Frame& frame, u32 n, i32 x, i32 y, i32 scale, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == unit && d.sin == 0 && d.shade == grey && d.paint == paint)
        return;
    place(frame, n, x, y, scale, grey, paint);
}
inline void conceal(Frame& frame, u32 n) { put(frame, n, hidden_x * unit, hidden_y * unit, unit, 0, keep); }
// Places a node when it has moved half a pixel, rescaled 1/64 or changed shade.
inline void nudge(Frame& frame, u32 n, i32 x, i32 y, i32 scale, u32 grey, Paint paint = stroke) {
    const Node& d = frame.nodes[n];
    if (d.shade == grey && d.paint == paint && d.cos == unit && magnitude(d.x - x) < 512 && magnitude(d.y - y) < 512 &&
        magnitude(d.scale - scale) < 16) return;
    place(frame, n, x, y, scale, grey, paint);
}
// Sets an outline when a point has moved half a pixel, or `force`.
inline void outline(Frame& frame, u32 p, Point* points, u32& key, bool force) {
    Frame::path_type& path = frame.paths[p];
    bool moved = force || !path.key;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        points[i].x = clamp(points[i].x, -63 * unit, 63 * unit);
        points[i].y = clamp(points[i].y, -63 * unit, 63 * unit);
        if (i && points[i].x == points[i - 1].x && points[i].y == points[i - 1].y) ++points[i].y;   // distinct
        if (magnitude(points[i].x - path.points[i].x) >= 512 || magnitude(points[i].y - path.points[i].y) >= 512)
            moved = true;
    }
    if (!moved) return;
    for (u32 i = 0; i < Frame::point_count; ++i) path.points[i] = points[i];
    key = key + 1 ? key + 1 : 1;
    path.key = key;
}
// A small ring, for an outline with nothing to show.
inline void rest(Point* points) {
    for (u32 i = 0; i < 64; ++i) {
        const i32 t = static_cast<i32>(i * 225 / 4);
        points[i].x = sine(t + 900) * 4;
        points[i].y = sine(t) * 2;
    }
}

// The header's captions, in the controls' units.
inline void digits(char* out, u32 value, u32 places) {   // value with `places` digits after a point
    char t[8];
    u32 at = 0, d = 1;
    for (u32 k = 0; k < places; ++k) d *= 10;
    const u32 whole = value / d;
    if (whole >= 100) t[at++] = static_cast<char>('0' + whole / 100 % 10);
    if (whole >= 10) t[at++] = static_cast<char>('0' + whole / 10 % 10);
    t[at++] = static_cast<char>('0' + whole % 10);
    if (places) {
        t[at++] = '.';
        u32 frac = value % d;
        for (u32 k = places; k; --k) {
            u32 p = 1;
            for (u32 j = 1; j < k; ++j) p *= 10;
            t[at++] = static_cast<char>('0' + frac / p % 10);
        }
    }
    t[at] = 0;
    copy(out, header_capacity, t);
}
inline void append(char* out, const char* unit_text) {
    u32 at = 0;
    while (at + 1 < header_capacity && out[at]) ++at;
    for (u32 i = 0; at + 1 < header_capacity && unit_text[i]; ++i) out[at++] = unit_text[i];
    out[at] = 0;
}
// A value of 2^(log / 65536) units with two significant figures, then a unit.
inline void figure(char* out, i32 log2_value, const char* unit_text) {
    const u32 hundredths = physics::exp2_q16(log2_value + 16 * octave) * 100u >> 16;   // to 655 units
    if (hundredths < 100) digits(out, hundredths, 2);
    else if (hundredths < 1000) digits(out, (hundredths + 5) / 10, 1);
    else digits(out, (hundredths + 50) / 100, 0);
    append(out, unit_text);
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
    const physics::Plate p = physics::plate(input.slots);

    // The header.
    const char* const titles[8] = {"Heat", "Size", "Liquid", "Beat", "Groove", "Vapour", "Life", "Crackle"};
    char values[8][header_capacity];
    if (p.heat < 0) {   // liquid nitrogen's plate can be cold
        copy(values[HEAT], header_capacity, "-");
        char t[header_capacity];
        digits(t, static_cast<u32>(-p.heat + 5) / 10, 0);
        append(values[HEAT], t);
    } else {
        digits(values[HEAT], static_cast<u32>(p.heat + 5) / 10, 0);
    }
    append(values[HEAT], "C");
    figure(values[SIZE], p.size + p.lobe, "mm");
    const u32 liquid = physics::q16(input.slots[LIQUID]);
    copy(values[LIQUID], header_capacity, liquid < 16384 ? "water" : liquid < 49152 ? "ethnl" : "LN2");
    // Crackle: the floating drop's contacts a second, at this heat.
    digits(values[CRACKLE], static_cast<u32>(physics::contacts(p)), 0);
    append(values[CRACKLE], "/s");
    // Groove: the pulses' swing, the longer of each pair's share (50% straight).
    if (p.swing) {
        digits(values[GROOVE], 50u + static_cast<u32>(p.swing * 100 + 32768) / 65536u, 0);
        append(values[GROOVE], "%");
    } else {
        copy(values[GROOVE], header_capacity, "even");
    }
    digits(values[VAPOUR], 20u + static_cast<u32>(p.vapour * 180 + 16384) / 32768u, 0);
    append(values[VAPOUR], "um");
    figure(values[LIFE], physics::lifetime(p, p.size), "s");
    {
        const u32 tenths = (physics::exp2_q16(p.beat + 16 * octave) * 10u + 32768u) >> 16;   // the beat, tenths of a Hz
        if (tenths < 100) digits(values[BEAT], tenths, 1);
        else digits(values[BEAT], (tenths + 5) / 10, 0);
        append(values[BEAT], "Hz");
    }
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, pages);

    // Time.
    state.notes.update(input);
    const bool fresh = !state.initialized || state.track != input.track;
    const bool reduced = input.reduced_motion != 0;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 102);   // motion eases on at most 0.1 s a frame
    if (fresh) {
        state.initialized = 1;
        state.track = input.track;
        state.serial = input.note_serial;
        state.count = 0;
        state.owed = 0;
        state.dice = 0x2545f491u;
        for (u32 k = 0; k < scene::most; ++k) {
            state.drops[k].used = 0;
            state.drops[k].drop.lobes = 0;
            state.drops[k].key = 0;
            state.drops[k].breath = 0;
            state.drops[k].pulse = 0;
            state.drops[k].flash = 0;
        }
        for (u32 j = 0; j < node::sparks; ++j) state.sparks[j].left = 0;
    }

    // Notes land drops: in a free place, or in the oldest drop's.
    u32 landing = fresh ? 0 : input.note_serial - state.serial;
    if (landing > scene::most) landing = scene::most;
    state.serial = input.note_serial;
    for (u32 n = 0; n < landing; ++n) {
        u32 slot = scene::most;
        for (u32 k = 0; k < scene::most && slot == scene::most; ++k)
            if (!state.drops[k].used) slot = k;
        if (slot == scene::most) {
            slot = 0;
            for (u32 k = 1; k < scene::most; ++k)
                if (state.drops[k].order < state.drops[slot].order) slot = k;
        }
        ScreenDrop& d = state.drops[slot];
        physics::land(d.drop, p);
        d.used = 1;
        d.order = ++state.count;
        d.breath = 0;
        d.pulse = 0;
        d.flash = 0;
    }

    // Their lives, a block of 64 samples at a time, as the voices step them.
    // They catch up a second a frame, so a slow screen keeps time with the
    // voices; motion eases on the frame's time, at most 0.1 s.
    state.owed += (fresh ? 0 : clamp(input.elapsed, 0, 1024)) * 689;
    const i32 blocks = state.owed >> 10;
    state.owed &= 1023;
    for (u32 k = 0; k < scene::most; ++k) {
        ScreenDrop& d = state.drops[k];
        if (!d.used) continue;
        for (i32 b = 0; b < blocks && d.drop.lobes; ++b) {
            const u32 events = physics::step(d.drop, p);
            if ((events & (physics::stepped | physics::landed)) && !reduced) {   // a spark, as the sound clicks
                for (u32 j = 0; j < node::sparks; ++j)
                    if (state.sparks[j].left <= 0) {
                        state.sparks[j].left = spark_time;
                        state.sparks[j].drop = k;
                        state.sparks[j].angle = 1900 + next(state.dice) % 1400;
                        break;
                    }
            }
        }
        if (!d.drop.lobes) d.used = 0;
    }
    const bool floating = p.floating >= 16384;

    // The drops.
    const i32 vapour = 205 + (p.vapour * 9011 >> 15);                              // the gap, 0.2 to 9 mm Q10: exaggerated
    // How deep the stars' lobes are drawn: slow beats are big, slow
    // oscillations, the papers' 23 Hz a tremor (Q10, 1.5 to 0.6).
    const i32 swing = 1536 - static_cast<i32>(physics::q16(input.slots[BEAT]) * 922u >> 16);
    // How fast a drop of this size evaporates, as its steam: a tall billowing
    // plume living half a second, smaller to 15 s, none past (Q10, 0 to 1 from 0.5 to 64 s).
    const i32 slow = clamp((physics::float_life(p, p.size) + octave) / 7 >> 6, 0, unit);
    const i32 plume = slow < 717 ? 1024 - (slow * 1000 >> 10) : 0;                // its scale, Q10: 1 to 0.3
    const i32 capillary = static_cast<i32>(physics::exp2_q16(p.lobe + 10 * octave)) * 1382 >> 10;   // l_c, mm Q10
    Point points[64];
    for (u32 k = 0; k < scene::most; ++k) {
        ScreenDrop& d = state.drops[k];
        const u32 rim = node::rim + k, under = node::underside + k;
        if (!d.used) {
            rest(points);
            outline(frame, rim, points, d.key, fresh);
            conceal(frame, rim);
            conceal(frame, under);
            conceal(frame, node::steam + k);
            conceal(frame, node::shadow + k);
            continue;
        }
        const physics::Drop& dr = d.drop;
        // Where it is: its place round the track, carried along by the ratchet.
        i32 angle = site[k];
        if (!reduced) angle += travel_angle(dr.travel);
        const i32 X = scene::track_r * sine(angle + 900), Y = scene::track_y * unit + scene::track_r * sine(angle);   // mm Q10
        const i32 x = scene::cx * unit + (X * scene::scale >> 10);
        const i32 y = scene::cy * unit - ((Y * scene::sin_e >> 10) * scene::scale >> 10);
        // Its size and shape.
        const i32 radius = drop_px(physics::radius(dr, p));                     // Q10 px
        const i32 radius_mm = static_cast<i32>(physics::exp2_q16(physics::radius(dr, p) + 10 * octave));   // Q10
        const i32 height = radius_mm < 2 * capillary ? radius_mm : 2 * capillary;
        const i32 lift = !reduced && dr.hop ? 1536 : 0;                         // a hop over a tooth
        const i32 seed_turn = static_cast<i32>(physics::hash(p.seed, k) % 3600u);
        // The star breathes 16 times slower than it beats, at most 6 times a second.
        // The star breathes at its beat, up to 2 a second, and slower than
        // it beats above that (as the square root), at most 6 a second.
        const i32 rate = physics::beat_rate(dr, p);
        i32 visible = rate > 65536 ? 65536 + (rate - 65536) / 2 : rate;
        if (visible > 169400) visible = 169400;
        if (!reduced) d.breath += physics::exp2_q16(visible + 22 * octave) * static_cast<u32>(dt);
        // Its vapour flashes under it at each pulse, as the sound's vents open:
        // as many to a beat as lobes, at most 12 a second.
        i32 pulses = rate + physics::log2_q16(dr.lobes ? dr.lobes : 1);
        if (pulses > 234959) pulses = 234959;
        const u32 before = d.pulse;
        if (!reduced) d.pulse += physics::exp2_q16(pulses + 22 * octave) * static_cast<u32>(dt);
        if (d.pulse < before) d.flash = 72;
        else d.flash = d.flash > dt ? d.flash - dt : 0;
        const i32 breathing = reduced ? unit : 563 + (461 * sine(static_cast<i32>((d.breath >> 16) * 3600u >> 16) + 900) >> 10);
        if (floating) {
            const u32 n = dr.lobes >= 2 ? dr.lobes : 2;
            const i32 depth = dr.lobes >= 3 ? star_amount : dr.lobes == 2 ? pair_amount : round_amount;
            const i32 amount = (((depth * swing >> 10) * dr.star >> 15) * p.floating >> 15) * breathing >> 10;   // Q10
            // A puddle (twice the capillary length and over) is seen as its
            // rim, 3/4 of its height up; a small drop is a bead, seen round,
            // its middle a radius up. Between the two it rounds.
            const i32 bead = clamp((2 * capillary - radius_mm) * unit / (capillary + 1), 0, unit);   // Q10
            const i32 squash = scene::sin_e + ((unit - scene::sin_e) * bead >> 10);
            const i32 rise = ((height * 3 >> 2) * (unit - bead) >> 10) + (radius_mm * bead >> 10);
            const i32 top = mm_px(rise + vapour + lift);                              // the outline's middle, px Q10
            for (u32 i = 0; i < 64; ++i) {
                const i32 t = static_cast<i32>(i * 225 / 4);
                const i32 r = radius + (radius * (amount * sine(static_cast<i32>(n) * (t - seed_turn) + 900) >> 10) >> 10);
                const i32 px = r * sine(t + 900) >> 10, py = r * sine(t) >> 10;
                points[i].x = px;
                points[i].y = -((py * squash >> 10) + (top * scene::cos_e >> 10));
            }
            outline(frame, rim, points, d.key, fresh);
            put(frame, rim, x, y, unit, 15, stroke);
            const i32 gap = mm_px(vapour + lift) * scene::cos_e >> 10;
            const i32 size = clamp(radius / scene::ring, 16, 2 * unit);
            nudge(frame, under, x, y - gap, size, d.flash ? 15 : 10);   // it flashes as the vapour vents
            // Its shadow stays on the iron, a dark footprint the vapour lifts it off.
            nudge(frame, node::shadow + k, x, y, size, 2, fill_and_stroke);
            // Its steam, from its top, swaying as it billows.
            if (plume) {
                const i32 sway = reduced ? 0 : 40 * sine(static_cast<i32>(dr.age * 3600u / 1033u % 3600u)) >> 10;   // tenths of a degree
                const i32 sx = x, sy = y - (top * scene::cos_e >> 10) - (radius * squash >> 11);
                const Node& was = frame.nodes[node::steam + k];
                const Turn tilt = {sine(sway + 900), sine(sway)};
                if (magnitude(was.x - sx) >= 512 || magnitude(was.y - sy) >= 512 || magnitude(was.scale - plume) >= 16 ||
                    was.sin != tilt.sin || was.paint != stroke) {
                    place(frame, node::steam + k, sx, sy, plume, static_cast<u32>(slow < 360 ? 12 : 9), stroke);
                    if (rotation_valid(tilt.cos, tilt.sin)) turn(frame, node::steam + k, tilt);
                }
            } else {
                conceal(frame, node::steam + k);
            }
        } else {
            // Boiling on the plate: a flattened, ragged puddle, wider than the drop.
            const i32 spread = radius * 21 >> 4;
            const i32 b1 = static_cast<i32>(physics::hash(p.seed, 7 + k) % 3600u), b2 = static_cast<i32>(physics::hash(p.seed, 11 + k) % 3600u);
            const i32 wobble = reduced ? 0 : sine(static_cast<i32>(dr.age * 97 % 3600)) / 16;
            for (u32 i = 0; i < 64; ++i) {
                const i32 t = static_cast<i32>(i * 225 / 4);
                const i32 bump = (sine(3 * t + b1) * 3 + sine(5 * t + b2) * 2 + sine(11 * t + b1 + wobble * 40) ) / 6;   // Q10
                const i32 r = spread + (spread * (bump * 120 >> 10) >> 10);
                points[i].x = r * sine(t + 900) >> 10;
                points[i].y = -((r * sine(t) >> 10) * scene::sin_e >> 10) - (mm_px(307) * scene::cos_e >> 10);
            }
            outline(frame, rim, points, d.key, fresh);
            put(frame, rim, x, y, unit, 15, stroke);
            conceal(frame, under);
            conceal(frame, node::steam + k);
            conceal(frame, node::shadow + k);
        }
    }


    // Spray from the newest boiling drop: six droplets thrown from its edge
    // again every 0.12 s, rising and falling as they fly out.
    u32 boiler = scene::most;
    for (u32 k = 0; k < scene::most; ++k)
        if (state.drops[k].used && !floating && (boiler == scene::most || state.drops[k].order > state.drops[boiler].order))
            boiler = k;
    for (u32 j = 0; j < node::sprays; ++j) {
        const u32 n = node::spray + j;
        if (boiler == scene::most) { conceal(frame, n); continue; }
        const ScreenDrop& d = state.drops[boiler];
        const i32 angle = site[boiler];
        const i32 X = scene::track_r * sine(angle + 900), Y = scene::track_y * unit + scene::track_r * sine(angle);
        const i32 x = scene::cx * unit + (X * scene::scale >> 10);
        const i32 y = scene::cy * unit - ((Y * scene::sin_e >> 10) * scene::scale >> 10);
        const u32 generation = reduced ? 0 : d.drop.age / 80;
        const i32 t = reduced ? 512 : static_cast<i32>((d.drop.age % 80) * 1024 / 80);   // Q10 through the flight
        const i32 a = static_cast<i32>(physics::hash(p.seed + 17, generation * 6 + j) % 3600u);
        const i32 spread = drop_px(physics::radius(d.drop, p)) * 21 >> 4;
        const i32 r = (spread * (1075 + (t * 920 >> 10)) >> 10) + 2 * unit;
        const i32 rise = 4 * (t * (1024 - t) >> 10) * 9;
        put(frame, n, x + (r * sine(a + 900) >> 10), y - ((r * sine(a) >> 10) * scene::sin_e >> 10) - rise, unit,
            static_cast<u32>(12 - (t * 6 >> 10)), stroke);
    }

    // Sparks: a floating drop's brief contacts, as often as they crackle (to
    // a dozen a second), and one at each lobe it gives up.
    if (!reduced && floating) {
        const i32 rate = physics::contacts(p) > 12 ? 12 : physics::contacts(p);
        for (u32 k = 0; k < scene::most; ++k) {
            if (!state.drops[k].used || !dt) continue;
            if (static_cast<i32>(next(state.dice) & 0xffffu) >= rate * dt * 64) continue;
            for (u32 j = 0; j < node::sparks; ++j)
                if (state.sparks[j].left <= 0) {
                    state.sparks[j].left = spark_time;
                    state.sparks[j].drop = k;
                    state.sparks[j].angle = 1900 + next(state.dice) % 1400;
                    break;
                }
        }
    }
    for (u32 j = 0; j < node::sparks; ++j) {
        Spark& s = state.sparks[j];
        const u32 n = node::spark + j;
        if (s.left <= 0 || !state.drops[s.drop].used || reduced) {
            s.left = 0;
            conceal(frame, n);
            continue;
        }
        s.left -= dt;
        const ScreenDrop& d = state.drops[s.drop];
        const i32 angle = site[s.drop] + travel_angle(d.drop.travel);
        const i32 X = scene::track_r * sine(angle + 900), Y = scene::track_y * unit + scene::track_r * sine(angle);
        const i32 x = scene::cx * unit + (X * scene::scale >> 10);
        const i32 y = scene::cy * unit - ((Y * scene::sin_e >> 10) * scene::scale >> 10);
        const i32 r = drop_px(physics::radius(d.drop, p)) + 1536;
        const i32 a = static_cast<i32>(s.angle);
        const i32 ex = r * sine(a + 900) >> 10, ey = -((r * sine(a) >> 10) * scene::sin_e >> 10);
        const Node& was = frame.nodes[n];
        if (was.x == x + ex && was.y == y + ey && was.shade == 13) continue;
        place(frame, n, x + ex, y + ey, unit, 13, stroke);
        // Turned outward, along the screen's direction from the drop's centre.
        const i32 len = static_cast<i32>(root(static_cast<u32>((ex >> 5) * (ex >> 5) + (ey >> 5) * (ey >> 5)))) + 1;
        const Turn out = {(ex >> 5) * unit / len, (ey >> 5) * unit / len};
        if (rotation_valid(out.cos, out.sin)) turn(frame, n, out);
    }

    // The water mark where the next drop lands: the size a new drop takes.
    {
        const i32 X = scene::track_r * sine(site[0] + 900), Y = scene::track_y * unit + scene::track_r * sine(site[0]);
        const i32 x = scene::cx * unit + (X * scene::scale >> 10);
        const i32 y = scene::cy * unit - ((Y * scene::sin_e >> 10) * scene::scale >> 10);
        const i32 scale = clamp(drop_px(p.size + p.lobe) / scene::ring, 16, 2 * unit);
        if (fresh) place(frame, node::stain, x, y, scale, 6, stroke);
        else nudge(frame, node::stain, x, y, scale, 6);
    }
    // Spatter dried on the iron round the landing places, there with no note
    // playing: a mark for each twelfth of the crackle, brighter as it grows.
    {
        const u32 dried = static_cast<u32>(p.crackle * 12 + 16384) >> 15;
        for (u32 j = 0; j < node::marks; ++j) {
            if (j < dried) put(frame, node::mark + j, 0, 0, unit, static_cast<u32>(6 + (p.crackle * 4 >> 15)), fill_and_stroke);
            else conceal(frame, node::mark + j);
        }
    }
    // The ratchet's teeth, as deep as the groove.
    if (p.groove > 300) put(frame, node::teeth, 0, 0, unit, static_cast<u32>(5 + (p.groove * 5 >> 15)), stroke);
    else conceal(frame, node::teeth);
    // The heat: rings tempered into the iron round the burner, one more for
    // each 75 degrees over 100 C, the inner first, glowing brighter as it
    // heats; on a plate under 0 C (liquid nitrogen's), rings of white frost,
    // one more for each 50 degrees under.
    const i32 hot = p.heat < 1500 ? 0 : 1 + (p.heat - 1500) / 750;
    const i32 cold = p.heat >= 0 ? 0 : 1 + (-p.heat) / 500;
    for (u32 j = 0; j < node::heats; ++j) {
        const u32 n = node::heat + j;
        if (static_cast<i32>(j) < hot) put(frame, n, 0, 0, unit, static_cast<u32>(clamp(8 + (p.heat - 1500) / 600 - static_cast<i32>(j), 6, 13)), stroke);
        else if (static_cast<i32>(j) < cold) put(frame, n, 0, 0, unit, 13 - j, stroke);
        else conceal(frame, n);
    }
}
}
