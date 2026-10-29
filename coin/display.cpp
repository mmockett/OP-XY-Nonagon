#include "display.h"
#include "plate.h"

// Coin's screen: the Euler's Disk toy, the whole of it, engraved in greys and
// fine line (tools/make_panel.py): a heavy chrome-steel disk spinning on its
// concave mirror, seen low, at 16 degrees, with its reflection.
//
// The note's first disk is four reshaping outlines, projected here each
// frame: its face's rim, its edge, its reflection (the mirror flattens it) and
// what is engraved on its face. As it settles its tilt flattens, 2^(-2L) of
// its throw (Omega^2 x tilt is constant), its wobble quickens and its contact
// point races round the rim: a glint, with motion lines lit behind it along
// the contact path. The rates on screen are the sound's slowed to be seen:
// 0.6 turns a second at the throw, rising 0.55 octave an octave, at most 5;
// past 3 a second the lines blur into a ring. Its face turns slowly the other
// way and slows as the whirr rises, as a real disk's does. When it leaves the
// surface it falls flat in 0.15 s; its ring's ripples flare outward and fade
// over the ring's time; then it is still.
//
// Every control acts on the toy. Spin sets the lean the disk is thrown at;
// size its radius and thickness, a coin to a heavy disk; disk its metal (or
// glass, or wood) and what is engraved on it; base the base's own marks, and
// the reflection shows only on the mirror; wobble the eccentric weight on the
// rim, a dashed ghost of the rim rocked further over, the rattle and scuffs
// on the path; air the streaks of air squeezed from under the low edge; ring
// the ripples across the glass, more, wider and brighter as it rises; many
// the companion disks spinning beside it. Between
// notes, turning a control stands the disks up at their throw, still, to show
// it; after a note they lie flat until the next.
//
// The screen cannot hear the voices: it keeps its own disks' lives, stepped
// with the voices' own code (spin.h), for each 64 samples of time since the
// note. Integer arithmetic only.
namespace coin {
namespace screen {
using namespace opxy::draw;
const u32 budget = 6;           // re-scaled and swapped nodes a frame, at most
const i32 fade_step = 5190000;  // spin at hold with no note held: the disks settle in 0.3 s
// Each size's radius (px) and half its thickness (Q10 of the radius): a thin
// coin to the toy's own heavy disk, a third of its radius thick, and heavier.
const i32 radius_of[spin::sizes] = {44, 56, 70, 79, 88};
const i32 half_of[spin::sizes] = {82, 113, 154, 174, 195};
// The companions: each one's way round from the note's disk (tenths of a
// degree) and its size against it (Q10).
const i32 companion_at[3] = {1750, 50, 2330}, companion_size[3] = {451, 420, 389};
// Shades: each disk material's rim, edge, face and reflection.
const u32 rim_shade[4] = {15, 13, 11, 12}, edge_shade[4] = {11, 9, 7, 8}, face_shade[4] = {10, 11, 8, 9},
          reflection_shade[4] = {8, 7, 6, 7};

inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
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
// atan2(y, x) in tenths of a degree, 0..3599 (to a tenth of a degree or so).
inline i32 angle_of(i32 x, i32 y) {
    if (!x && !y) return 0;
    const i32 ax = magnitude(x), ay = magnitude(y);
    const bool steep = ay > ax;
    const i32 low = steep ? ax : ay, high = steep ? ay : ax;
    const i32 z = (low >> 4 << 10) / ((high >> 4) + 1);
    i32 a = (450 * z >> 10) + ((156 * z >> 10) * (1024 - z) >> 10);
    if (steep) a = 900 - a;
    if (x < 0) a = 1800 - a;
    if (y < 0) a = 3600 - a;
    return a >= 3600 ? a - 3600 : a;
}
// A Q32 turn in tenths of a degree.
inline i32 tenths(u32 turn) { return static_cast<i32>((turn >> 16) * 3600u >> 16); }
// value eased toward target by `keep` (Q16: the share of the distance left).
inline i32 ease(i32 value, i32 target, u32 keep) {
    return target + static_cast<i32>((static_cast<i32>(value - target) >> 4) * static_cast<i32>(keep >> 4) >> 8);
}
// v x 2^(e / 65536) for small e of either sign.
inline u32 times(u32 v, i32 e) {
    const i32 k = spin::whole(e);
    const u32 m = (v >> 14) * (spin::exp2_mantissa(e) >> 16);
    return k >= 0 ? m << k : m >> -k;
}

// The disk's pose: its radius, half its thickness and its centre's height
// (Q10 px), and the sines and cosines of its tilt and of the contact's way round (Q10).
struct Pose { i32 radius, half, middle, sa, ca, st, ct; };
// A point about the mirror's centre on screen, Q10 px.
inline Point project(i32 x, i32 y, i32 z) {
    const i32 depth = (y * scene::cos_e - z * scene::sin_e) >> 14;
    const i32 k = (scene::distance << 20) / (scene::distance * unit + depth);
    const Point p = {x * k >> 10, -(((y * scene::sin_e + z * scene::cos_e) >> 14) * k >> 10)};
    return p;
}
// A point on the disk: at angle phi (tenths) round its axis from the contact,
// height s along it from the middle and radius r (Q10 px); or its reflection
// in the concave mirror, flattened.
inline Point lay(const Pose& p, i32 phi, i32 s, i32 r, bool mirrored) {
    const i32 cp = sine(phi + 900), sp = sine(phi);
    const i32 rc = r * cp >> 10, rs = r * sp >> 10;
    // Toward the contact (cos tilt, down sin tilt), across it, and the axis (sin tilt, up cos tilt).
    const i32 along = (rc * p.ca >> 10) + (s * p.sa >> 10);
    const i32 x = (along * p.ct >> 10) - (rs * p.st >> 10);
    const i32 y = (along * p.st >> 10) + (rs * p.ct >> 10);
    const i32 z = p.middle + (s * p.ca >> 10) - (rc * p.sa >> 10);
    return project(x, y, mirrored ? -(z * scene::mirrored >> 10) : z);
}
// Sets outline p (drawn at twice its points about the mirror's centre) when a
// point has moved half a pixel, or `force`.
inline void outline(Frame& frame, u32 p, Point* points, u32& key, bool force) {
    Frame::path_type& path = frame.paths[p];
    bool moved = force || !path.key;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        points[i].x >>= 1;
        points[i].y >>= 1;
        if (i && points[i].x == points[i - 1].x && points[i].y == points[i - 1].y) ++points[i].y;   // distinct
        if (magnitude(points[i].x - path.points[i].x) >= 256 || magnitude(points[i].y - path.points[i].y) >= 256)
            moved = true;
    }
    if (!moved) return;
    for (u32 i = 0; i < Frame::point_count; ++i) path.points[i] = points[i];
    key = key + 1 ? key + 1 : 1;
    path.key = key;
}
// Places a node upright when it is not already so; returns 1 if it changed.
inline u32 put(Frame& frame, u32 n, i32 x, i32 y, i32 scale, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == unit && d.sin == 0 && d.shade == grey && d.paint == paint)
        return 0;
    place(frame, n, x, y, scale, grey, paint);
    return 1;
}
inline u32 conceal(Frame& frame, u32 n) {
    return put(frame, n, hidden_x * unit, hidden_y * unit, unit, 0, keep);
}
// Places a node only when it has moved a pixel or changed shade.
inline void nudge(Frame& frame, u32 n, i32 x, i32 y, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.scale == unit && d.shade == grey && d.paint == paint && d.cos == unit &&
        magnitude(d.x - x) < unit && magnitude(d.y - y) < unit) return;
    place(frame, n, x, y, unit, grey, paint);
}

// The header's captions, in the controls' units.
// Hundredths of a second as 0.45s, 2.7s or 12s.
inline void caption_seconds(char* out, u32 h) {
    char t[8];
    u32 at = 0;
    if (h < 100) {
        t[at++] = '0'; t[at++] = '.';
        t[at++] = static_cast<char>('0' + h / 10); t[at++] = static_cast<char>('0' + h % 10);
    } else if (h < 1000) {
        const u32 tenths_of = (h + 5) / 10;
        t[at++] = static_cast<char>('0' + tenths_of / 10 % 10); t[at++] = '.';
        t[at++] = static_cast<char>('0' + tenths_of % 10);
    } else {
        const u32 s = (h + 50) / 100;
        t[at++] = static_cast<char>('0' + s / 10); t[at++] = static_cast<char>('0' + s % 10);
    }
    t[at++] = 's';
    t[at] = 0;
    copy(out, header_capacity, t);
}
inline void caption_spin(char* out, i32 spin) {
    if (spin::held(spin)) copy(out, header_capacity, "hold");
    else caption_seconds(out, spin::settle_hundredths(spin));
}
// Air as the rate's exponent: -.33 (rolling friction) to -.17 (viscous air).
inline void caption_air(char* out, i32 air) {
    const u32 h = (static_cast<u32>(spin::exponent(air)) * 100u + 32768u) >> 16;
    const char t[5] = {'-', '.', static_cast<char>('0' + h / 10), static_cast<char>('0' + h % 10), 0};
    copy(out, header_capacity, t);
}
// The throw's lean, tenths of a degree: 4 degrees for the shortest spin to 28
// for the longest, and at hold.
inline i32 lean_of(i32 spin) {
    return spin::held(spin) ? 280 : 40 + static_cast<i32>(240u * spin::settle_position(spin) >> 16);
}
// A disk's lean as it settles: its throw's, falling as 2^(-2L).
inline i32 tilt_of(const ScreenDisk& d, i32 lean, i32 n) {
    return static_cast<i32>(times(static_cast<u32>(lean) << 14, -2 * spin::rise(d.life, n)) >> 14);
}
// Its contact's speed on screen, Q32 turns a Q10 second: 0.6 turns a second at
// the throw, rising 0.55 octave for each octave of the sound's, at most 5.
inline u32 laps_of(const ScreenDisk& d, i32 n) {
    i32 rise = spin::rise(d.life, n);
    rise = rise > 364000 ? 364000 : rise;
    return times(2516582u, rise * 9 / 16 - rise / 64);
}
}

namespace screen {
// Places a node turned, when it is not already so.
inline void put_turned(Frame& frame, u32 n, i32 x, i32 y, i32 scale, Turn t, u32 grey) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == t.cos && d.sin == t.sin && d.shade == grey && d.paint == stroke)
        return;
    place(frame, n, x, y, scale, grey, stroke);
    turn(frame, n, t);
}
// A rough fingerprint of every control, to see one turn.
inline u32 fingerprint(const i32* slots) {
    u32 f = 0;
    for (u32 k = 0; k < 8; ++k) f = f * 131u + (static_cast<u32>(slots[k] < 0 ? 0 : slots[k]) >> 20);
    return f;
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

    // The controls.
    const bool hand = spin::held(slots[SPIN]);
    const i32 n = spin::exponent(slots[AIR]);
    const u32 size = spin::choice(slots[SIZE], spin::sizes);
    const i32 cut = spin::top(slots[SIZE]);
    const i32 cut_life = spin::life_at_cut(cut, n);
    const u32 material = spin::choice(slots[DISK], 4), ground = spin::choice(slots[BASE], 3);
    const u32 wobble = spin::q16(slots[WOBBLE]);
    const u32 air = spin::q16(slots[AIR]), ring = spin::q16(slots[RING]);
    const i32 lean = lean_of(slots[SPIN]);

    // The header, in the controls' units.
    const char* const titles[8] = {"Spin", "Size", "Disk", "Base", "Wobble", "Air", "Ring", "Many"};
    const char* const sizes[5] = {"1:2", "1:4", "1:8", "1:16", "1:32"};
    const char* const disks[4] = {"steel", "brass", "glass", "wood"};
    const char* const bases[3] = {"glass", "wood", "stone"};
    char values[8][header_capacity];
    caption_spin(values[SPIN], slots[SPIN]);
    copy(values[SIZE], header_capacity, sizes[size]);
    copy(values[DISK], header_capacity, disks[material]);
    copy(values[BASE], header_capacity, bases[ground]);
    header_amount(values[WOBBLE], header_capacity, slots[WOBBLE]);
    caption_air(values[AIR], slots[AIR]);
    caption_seconds(values[RING], spin::after_hundredths(slots[RING]));
    const char many[2] = {static_cast<char>('0' + spin::count(slots[MANY])), 0};
    copy(values[MANY], header_capacity, many);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, pages);

    // Time.
    state.notes.update(input);
    const bool fresh = !state.initialized || state.track != input.track;
    const bool reduced = input.reduced_motion != 0;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 102);
    const u32 keep = fresh || reduced ? 0u : page_header_detail::remaining(static_cast<u32>(dt));
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const i32 radius = radius_of[size] * unit;
    const u32 print = fingerprint(slots);
    if (fresh) {
        state.initialized = 1;
        state.track = input.track;
        state.serial = input.note_serial;
        for (u32 k = 0; k < spin::most; ++k) {
            ScreenDisk& d = state.disk[k];
            d.rolling = 0;
            d.life = 0;
            d.precession = static_cast<u32>(k) * 0x51000000u;   // the first leans to the right
            d.tilt = lean;
        }
        state.count = spin::count(slots[MANY]);
        state.poised = 1;
        state.owed = 0;
        state.turn = 0;
        state.radius = radius;
        state.since = 1 << 20;
        state.seed = 0x2545f491u;
        state.frames = 0;
        state.breath = 0;
        if (!state.key) state.key = 1;
    }
    ++state.frames;

    // A note throws its disks, all their life left. Between notes, a turned
    // control stands them at their throw, to show it.
    bool any = false;
    for (u32 k = 0; k < spin::most; ++k) any = any || state.disk[k].rolling;
    if (!fresh && input.note_serial != state.serial) {
        state.count = spin::count(slots[MANY]);
        for (u32 k = 0; k < spin::most; ++k) {
            state.disk[k].rolling = k < state.count;
            state.disk[k].life = k < state.count ? spin::one : 0;
        }
        state.poised = 0;
        state.owed = 0;
        state.since = 1 << 20;
        any = true;
    } else if (!fresh && !any && print != static_cast<u32>(state.spin)) {
        state.poised = 1;
        state.count = spin::count(slots[MANY]);
    }
    state.serial = input.note_serial;
    state.spin = static_cast<i32>(print);

    // Their lives, a block of 64 samples at a time, as the voices step them.
    // Kept at hold while a note is held; with none held they settle in 0.3 s,
    // as the amp envelope's release takes the sound away. Lives catch up a
    // second a frame, so a slow screen keeps time with the voices; motion
    // eases on the frame's time, at most 0.1 s.
    state.owed += (fresh ? 0 : clamp(input.elapsed, 0, 1024)) * 11025 / 16;
    const i32 blocks = state.owed >> 10;
    state.owed &= 1023;
    const bool kept = hand && held;
    const i32 step = hand ? fade_step : spin::life_step(slots[SPIN]);
    const u32 was = state.disk[0].rolling;
    any = false;
    for (u32 k = 0; k < spin::most; ++k) {
        ScreenDisk& d = state.disk[k];
        if (!d.rolling) continue;
        const i32 own = spin::disk_step(step, k);
        if (kept) {
            for (i32 b = 0; b < blocks; ++b) d.life = spin::next_life(d.life, own, true);
        } else if (blocks && (spin::rise(d.life, n) >= cut || d.life <= cut_life ||
                              blocks >= (d.life - cut_life + own - 1) / own)) {
            d.life = 0;   // the step that reaches the cut, as the voice's
            d.rolling = 0;
        } else {
            d.life -= own * blocks;
        }
        any = any || d.rolling;
    }
    const ScreenDisk& lead = state.disk[0];
    if (was && !lead.rolling) state.since = 0;
    state.since = state.since + dt > 1 << 20 ? 1 << 20 : state.since + dt;

    // Each disk's lean and way round: settling, standing at its throw, or flat.
    for (u32 k = 0; k < spin::most; ++k) {
        ScreenDisk& d = state.disk[k];
        const i32 target = d.rolling ? tilt_of(d, lean, n) : state.poised ? lean : 0;
        d.tilt = keep ? ease(d.tilt, target, keep) : target;
        if (magnitude(d.tilt - target) < 2) d.tilt = target;
        if (d.rolling && !reduced) d.precession += laps_of(d, n) * static_cast<u32>(dt);
    }
    state.radius = keep ? ease(state.radius, radius, keep) : radius;
    if (magnitude(state.radius - radius) < 64) state.radius = radius;
    const i32 rise = lead.rolling ? spin::rise(lead.life, n) : 0;
    const u32 laps = lead.rolling ? laps_of(lead, n) : 0;
    if (lead.rolling && !reduced) state.turn -= times(1048576u, -2 * rise) * static_cast<u32>(dt);
    i32 jitter = 0;
    if (lead.rolling && !reduced && wobble > 600) {
        state.seed ^= state.seed << 13;
        state.seed ^= state.seed >> 17;
        state.seed ^= state.seed << 5;
        jitter = static_cast<i32>((state.seed >> 20) * wobble >> 16) - static_cast<i32>(2048u * wobble >> 16);   // +-2 px
    }

    Pose pose;
    pose.radius = state.radius;
    pose.half = state.radius * half_of[size] >> 10;
    pose.sa = sine(lead.tilt);
    pose.ca = sine(lead.tilt + 900);
    const i32 way = tenths(lead.precession);
    pose.st = sine(way);
    pose.ct = sine(way + 900);
    pose.middle = (pose.half * pose.ca >> 10) + (pose.radius * pose.sa >> 10) + jitter;
    const i32 face = tenths(state.turn);

    // The note's disk: its four outlines.
    const i32 cx = scene::cx * unit, cy = scene::cy * unit;
    Point points[64];
    for (u32 i = 0; i < 64; ++i) points[i] = lay(pose, static_cast<i32>(i * 225 / 4), pose.half, pose.radius, false);
    outline(frame, node::disk, points, state.key, fresh);
    // The edge: the side facing the camera, between its two rims, where the
    // rim's outward normal turns toward it.
    const i32 a = -(((pose.ca * pose.st >> 10) * scene::cos_e + pose.sa * scene::sin_e) >> 14);
    const i32 b = -((pose.ct * scene::cos_e) >> 14);
    const i32 front = angle_of(a, b);
    for (u32 i = 0; i < 32; ++i) {
        const i32 phi = front - 900 + static_cast<i32>(i * 1800 / 31);
        points[i] = lay(pose, phi, -pose.half, pose.radius, false);
        points[63 - i] = lay(pose, phi, pose.half, pose.radius, false);
    }
    outline(frame, node::edge, points, state.key, fresh);
    // The reflection: the underside's rim, in the mirror.
    for (u32 i = 0; i < 64; ++i) points[i] = lay(pose, static_cast<i32>(i * 225 / 4), -pose.half, pose.radius, true);
    outline(frame, node::reflection, points, state.key, fresh);
    // The face: what is engraved on it; through glass, the underside's rim.
    for (u32 i = 0; i < 64; ++i)
        points[i] = material == 2 ? lay(pose, static_cast<i32>(i * 225 / 4), -pose.half, pose.radius, false)
                                  : lay(pose, plate::face_angle[material][i] + face, pose.half,
                                        pose.radius * plate::face_radius[material][i] >> 10, false);
    outline(frame, node::face, points, state.key, fresh);
    put(frame, node::disk, cx, cy, 2 * unit, rim_shade[material], stroke);
    put(frame, node::edge, cx, cy, 2 * unit, edge_shade[material], stroke);
    put(frame, node::face, cx, cy, 2 * unit, face_shade[material], stroke);
    if (ground == 0) put(frame, node::reflection, cx, cy, 2 * unit, reflection_shade[material], stroke);
    else conceal(frame, node::reflection);

    // The contact point (hidden when the disk is in front of it), and the
    // eccentric weight on the face.
    if (lead.rolling && pose.st < 300) {
        const Point p = lay(pose, 0, -pose.half, pose.radius, false);
        nudge(frame, node::glint, cx + p.x, cy + p.y, 14, stroke);
    } else {
        conceal(frame, node::glint);
    }
    // Wobble's eccentric weight: a lead balance weight clamped to the rim,
    // carried round by the disk's turn, 0.7 to 2 times its size as wobble
    // rises; hidden when the turn takes it to the back.
    const i32 at = front + 400 - face;
    const i32 facing = sine(at - front + 900);   // cos of its angle from the camera's side
    if (wobble > 1300 && facing > 200) {
        const Point p = lay(pose, at, 0, pose.radius, false);
        const i32 heavy = 717 + static_cast<i32>(wobble * 1331u >> 16);   // 0.7 to 2, Q10
        put(frame, node::weight, cx + p.x, cy + p.y, heavy, 14, stroke);
    } else {
        conceal(frame, node::weight);
    }
    // Wobble's rattle: a dashed ghost of the face's rim, where the eccentric
    // weight throws it, hopped up and rocked further over: none at 0, 6 px
    // and 5 degrees at 50, 12 px and 10 degrees at 100, brighter as it rises. It rattles with the disk, a third of its
    // dashes a frame; gone when the disk lies flat.
    const bool rocking = wobble > 1300 && (lead.rolling || state.poised);
    if (rocking) {
        Pose ghost = pose;
        const i32 swing = static_cast<i32>(wobble * 100u >> 16), hop = static_cast<i32>(wobble * 12u >> 6);   // tenths, Q10 px
        ghost.sa = sine(lead.tilt + swing);
        ghost.ca = sine(lead.tilt + swing + 900);
        ghost.middle = (ghost.half * ghost.ca >> 10) + (ghost.radius * ghost.sa >> 10) + hop + jitter;
        const u32 grey = 8 + (wobble * 6u >> 16);
        for (u32 j = 0; j < node::ghosts; ++j) {
            if (!fresh && (j + state.frames) % 3) continue;
            const i32 phi = static_cast<i32>(j) * 300;
            const Point p0 = lay(ghost, phi, ghost.half, ghost.radius, false),
                        p1 = lay(ghost, phi + 180, ghost.half, ghost.radius, false);
            const i32 dx = (p1.x - p0.x) >> 2, dy = (p1.y - p0.y) >> 2;
            const i32 length = static_cast<i32>(root(static_cast<u32>(dx * dx + dy * dy))) + 1;
            const Turn along = {dx * unit / length, dy * unit / length};
            put_turned(frame, node::ghost + j, cx + ((p0.x + p1.x) >> 1), cy + ((p0.y + p1.y) >> 1),
                       clamp(4 * length / scene::dash, 64, 2 * unit), along, grey);
        }
    } else {
        for (u32 j = 0; j < node::ghosts; ++j)
            if (fresh || (j + state.frames) % 3 == 0) conceal(frame, node::ghost + j);
    }

    // The contact path, as motion lines 7 px outside the rim, its back hidden
    // by the disk: lit behind the contact point, or a blur past 3 turns a
    // second; the scuffs the rattle leaves, more as wobble rises, brightening
    // as the contact passes.
    // Re-scaling for a new size and swapping the base's marks share a budget.
    u32 spare = budget;
    const i32 scale = (radius + 7 * unit) / scene::reference;
    const u32 head = static_cast<u32>(way / 150);
    const bool blur = laps > 3u * 4194304u;   // 3 turns a second
    for (u32 j = 0; j < node::trails; ++j) {
        if (j >= 1 && j <= 10) { conceal(frame, node::trail + j); continue; }   // behind the disk
        u32 grey = 2;
        if (lead.rolling && !reduced) {
            const u32 behind = (head + node::trails - j) % node::trails;
            grey = blur ? 5 : behind == 0 ? 13 : behind == 1 ? 9 : behind == 2 ? 6 : behind == 3 ? 4 : 2;
        }
        const u32 n_ = node::trail + j;
        if (frame.nodes[n_].scale != scale && frame.nodes[n_].scale) {
            if (!spare) { shade(frame, n_, grey, stroke); continue; }
            --spare;
        }
        put(frame, n_, cx, cy, scale, grey, stroke);
    }
    const u32 scuffs = (wobble * 8 + 32768) >> 16;
    for (u32 j = 0; j < node::scuffs; ++j) {
        const u32 n_ = node::scuff + j;
        if (j >= scuffs) { conceal(frame, n_); continue; }
        const u32 at = (196 + 19 * j + (j * 7) % 5) / 15;
        const u32 behind = (head + node::trails - at) % node::trails;
        const u32 grey = lead.rolling && !reduced && behind < 2 ? 14 : 10;
        if (frame.nodes[n_].scale != scale && frame.nodes[n_].x == cx) {
            if (!spare) { shade(frame, n_, grey, stroke); continue; }
            --spare;
        }
        put(frame, n_, cx, cy, scale, grey, stroke);
    }
    // The base's own marks: streaks on the mirror, growth rings in wood, specks in stone.
    for (u32 j = node::mirror_mark; j < node::speck + node::specks; ++j) {
        const u32 group = j < node::grain ? 0 : j < node::speck ? 1 : 2;
        const bool show = group == ground;
        const Node& d = frame.nodes[j];
        const bool shown = d.scale == unit && d.x == 0 && d.y == 0;
        if (show == shown && d.scale) continue;
        if (!spare && d.scale) continue;
        if (d.scale) --spare;
        if (show) put(frame, j, 0, 0, unit, group == 2 && j < node::speck + 8 ? 7 : group == 1 ? 6 : 8,
                      group == 2 && j < node::speck + 8 ? fill_and_stroke : stroke);
        else conceal(frame, j);
    }

    // Many's companions, beside it on the mirror, each a little smaller and
    // its own size: placed, scaled, rolled by their lean toward the camera's
    // left or right and lifted by it. While they spin, each moves on its own
    // third of the frames (and so does a change to them), never all at once.
    for (u32 k = 1; k < spin::most; ++k) {
        const bool step_now = fresh || (state.frames + k) % 3 == 0;
        const u32 at = node::companion + 4 * (k - 1);
        if (k >= state.count) {
            for (u32 j = 0; j < 4; ++j) conceal(frame, at + j);
            continue;
        }
        if (!step_now) continue;
        const ScreenDisk& d = state.disk[k];
        i32 rc = state.radius * companion_size[k - 1] >> 10;
        const i32 room = (190 * unit - state.radius - 8 * unit) / 2;
        rc = rc < room ? rc : room;
        const i32 reach = state.radius + rc + 8 * unit, angle = companion_at[k - 1];
        const i32 x = reach * sine(angle + 900) >> 10, y = reach * sine(angle) >> 10;
        const Point p = project(x, y, 0), q = project(x + rc, y, 0);
        const i32 across = q.x - p.x;   // its radius on screen, Q10 px
        const i32 scale_ = clamp(across / scene::companion, 16, 2 * unit);
        const i32 way_ = tenths(d.precession);
        const i32 roll = d.tilt * sine(way_ + 900) >> 10;
        const i32 lift = ((across * sine(d.tilt) >> 10) * scene::cos_e) >> 14;
        const Turn tipped = rotation(roll), mirror = rotation(-roll);
        put_turned(frame, at, cx + p.x, cy + p.y - lift, scale_, tipped, rim_shade[material]);
        put_turned(frame, at + 1, cx + p.x, cy + p.y - lift, scale_, tipped, edge_shade[material]);
        if (ground == 0) put_turned(frame, at + 2, cx + p.x, cy + p.y + (lift * 2 >> 2), scale_, mirror, reflection_shade[material] - 1);
        else conceal(frame, at + 2);
        if (d.rolling && !reduced) {
            const Point c = project(x + (rc * sine(way_ + 900) >> 10), y + (rc * sine(way_) >> 10), 0);
            nudge(frame, at + 3, cx + c.x, cy + c.y, 13, stroke);
        } else {
            conceal(frame, at + 3);
        }
    }

    // Air squeezed from under the low edge: long streaks fanning out from the
    // contact, more and longer as air rises, drifting outward on every sixth
    // frame while it rolls; still at the throw; gone when it lies flat.
    const u32 streaks = (air * 5 + 32768) >> 16;
    const i32 gust = 768 + static_cast<i32>(air * 666u >> 16);   // 0.75 to 1.4, Q10
    const bool breathing = lead.rolling && !reduced;
    if (breathing) state.breath += laps * static_cast<u32>(dt) * 2u;
    const bool step_air = fresh || !breathing || state.frames % 6 == 1;
    for (u32 j = 0; j < node::airs; ++j) {
        const u32 n_ = node::air + j;
        if (j >= streaks || (!lead.rolling && !state.poised)) { conceal(frame, n_); continue; }
        if (!step_air) continue;
        const i32 drift = static_cast<i32>(((state.breath >> 16) * 6144u) >> 16);   // 0 to 6 px, round and round
        const i32 out = state.radius + (5 + 6 * static_cast<i32>(j)) * unit + drift;
        const i32 angle = way + (static_cast<i32>(j) - 2) * 190;
        const i32 c = sine(angle + 900), s = sine(angle);
        if (s > 300) { conceal(frame, n_); continue; }   // behind the disk
        const Point p = project(out * c >> 10, out * s >> 10, 0);
        const i32 dx = c, dy = -(s * scene::sin_e >> 14);
        const i32 length = static_cast<i32>(root(static_cast<u32>(dx * dx + dy * dy))) + 1;
        const Turn outward = {dx * unit / length, dy * unit / length};
        put_turned(frame, n_, cx + p.x, cy + p.y, gust, outward, breathing ? 13 : 11);
    }

    // The ring's ripples across the mirror in front of the disk, as many as
    // ring sets, from just beyond its rim out to the mirror's edge: none for a dead
    // slap, eight reaching the glass's edge for a 4 s bell, brighter as ring
    // grows and each a little dimmer than the one inside it. As the disk
    // falls flat they flare, outward a frame a ripple, and fade over the
    // ring's time.
    const u32 ripples = (ring * 8 + 32768) >> 16;
    const i32 ringing = static_cast<i32>(spin::after_hundredths(slots[RING]) * 1024u / 100u);   // Q10 s
    const u32 steady = 6 + (ring * 6u >> 16);
    const i32 gap = ((scene::reach - 16) * unit - radius) / 7;
    for (u32 j = 0; j < node::ripples; ++j) {
        const u32 n_ = node::ripple + j;
        if (j >= ripples) { conceal(frame, n_); continue; }
        const i32 r = radius + 16 * unit + static_cast<i32>(j) * gap;
        const i32 authored = scene::ripple * unit + static_cast<i32>(j) * scene::ripple_step;
        const i32 since = state.since - 34 * static_cast<i32>(j);
        u32 bright = steady;
        if (!lead.rolling && since >= 0 && since < ringing)
            bright += static_cast<u32>(static_cast<i32>(15 - steady) * (ringing - since) / ringing);
        const u32 dim = (3 * j + 4) / 8;
        put(frame, n_, cx, cy, (r << 10) / authored, bright > dim + 5 ? bright - dim : 5, stroke);
    }
}
}
