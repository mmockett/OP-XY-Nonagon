#include "display.h"
#include "plate.h"

// Mach's screen is one object: a photograph after Mach and Salcher's 1887
// schlieren images of a bullet in flight, its round field's edge framing it
// (tools/make_panel.py). Every control acts on something in it:
//
//   mach    the cone: the bow and tail shocks' half-angle, asin(1/M); none
//           below Mach 1, where the sound waves nest ahead of the projectile
//   pass    the listener: an ear trumpet on a post, its mouth under the line
//           of flight, from just under it (3 m) to near the field's lower
//           edge (300 m), on a scale of the distance's logarithm
//   source  the projectile: rifle bullet (organ), round-nose bullet (pad),
//           Minie ball (voice), musket ball (bell), spitzer (siren); the
//           blunter the nose, the further its bow shock stands off
//   dur     the turbulent wake: from a short wisp to a long, broad trail
//   boom    the shocks: off a faint hairline; a soft thump a broad, dim
//           band; the full crack a crisp, bright line; below Mach 1 a bow
//           wave ahead of the nose (the pass's pressure pulse), drawn alike
//   air     the sound-wave rings: the thicker the air, the sooner they fade
//           and the nearer they stop (it absorbs the outer waves)
//   tune    the sound waves' spacing, the source's pitch: 20 px apart at
//           tune's low end, 10 at its top
//   echo    the polished plate under the listener, and the shock it reflects
//
// Idle, the photograph is still: the projectile at its closest pass with the
// sound waves it sent out at equal steps behind it. Turning a control eases it
// there in about 0.15 s. A note launches a flight, fixed at its note-on as the
// engine fixes it: the projectile crosses the field at its Mach number (the
// whole flight spans the field, and sound crosses it on the same scale, so the
// cone reaches the wire as the boom is heard and a thin ring flashes there);
// waves spread from where it was; its wake churns; and two marks on the line
// of flight show where the sound heard now left it, parting forwards and
// backwards after the cone. Releasing every note eases the photograph back.
// The screen cannot hear the engine: it derives the flight from the controls
// and time through flight.h, as the engine does.
namespace mach {
namespace screen {
using opxy::draw::unit;
const i32 settle = 154;              // Q10 seconds: the eases settle
const i32 radius = 58;               // the largest sound wave, pixels
const i32 fastest = 11200;           // the photograph's scale at most 700 px a sound-second, Q4
const i32 flash = 358;               // Q10 seconds a boom's ring takes to spread
// Palette greys (make_panel.py: GREYS).
// Three steps: the projectile and a crack brightest, the field's edge, the
// listener, the plate and a thump a step down, the wake and the waves dimmer.
const u32 bright = 15, metal = 15, ring_near = 10, wire_grey = 10, plate_grey = 10,
          wake_grey = 8, reflected_grey = 9;

// Exponential approach to a target, as the SDK's header eases (halving every
// 21 ms); snaps when the remaining step rounds away.
inline i32 ease(i32 value, i32 target, i32 dt) {
    const i32 left = static_cast<i32>(opxy::draw::page_header_detail::remaining(static_cast<u32>(dt)) >> 6);
    const i32 next = target + opxy::draw::floor_q10((value - target) * left);
    return next == value ? target : next;
}

// The photograph's scale: the flight, M dur sound-seconds long, spans 320 px of
// the field (within 20 .. 700 px a sound-second), and sound crosses it on the
// same scale. The waves' spacing: 20 px * 2^-(tune + 1) / 2, the source's
// pitch rising an octave over tune's travel on the screen.
inline void scale(Scene& s) {
    const i32 span = flight::mul16(s.mach, s.length);                         // M dur, Q16
    s.scale = opxy::draw::clamp(span > 0 ? 335544320 / span : fastest, 320, fastest);   // 320 * 16 / span
    s.wave = (flight::exp2_q16(65536 - ((s.tune + 65536) >> 1)) * 10) >> 6;
}

// The photograph's scene from the controls.
inline Scene scene_of(const opxy::draw::Input& in) {
    Scene s;
    s.mach = flight::mach(in.slots[MACH]);
    s.distance = flight::sound_seconds(flight::pass_metres(in.slots[PASS]));
    s.length = flight::dur(in.slots[DUR]);
    s.tune = flight::tune(in.slots[TUNE]);
    s.echo = flight::echo_level(in.slots[ECHO]) > 0 ? 1 : 0;
    // the plate 4 px (1.5 m) to 23 px (63 m) under the wire: 4 + 3.6 log2(h / 1.5 m)
    s.height = 4096 + ((flight::q16(in.slots[ECHO]) * 19906) >> 16);
    s.boom = flight::amount(in.slots[BOOM]);
    s.source = flight::source(in.slots[SOURCE]);
    // The listener 10 px (3 m) to 80 px (300 m) under the line of flight.
    s.reach = 10 * unit + ((flight::q16(in.slots[PASS]) * 70) >> 6);
    scale(s);
    return s;
}

// Is a ring of radius r (Q10) about (x, y) whole inside the photograph's field
// and the card's window, `margin` pixels in?
inline bool inside(i32 x, i32 y, i32 r, i32 margin) {
    const i32 px = (x >> 10) - plate::centre_x, py = (y >> 10) - plate::centre_y, pr = (r >> 10) + margin;
    if ((y - r) >> 10 < plate::top + margin || (y + r) >> 10 > plate::bottom - margin) return false;
    const i32 room = plate::radius - pr;
    return room > 0 && px * px + py * py <= room * room;
}

// How far (whole pixels, at most `most`) a line from (x, y) Q10 runs along the
// unit direction (dx, dy) Q16 before it leaves the field or the window.
inline i32 reach_edge(i32 x, i32 y, i32 dx, i32 dy, i32 most) {
    i32 best = most;
    const i32 py = y >> 10;
    if (dy < -64) { const i32 t = ((py - plate::top) << 16) / -dy; if (t < best) best = t; }
    if (dy > 64) { const i32 t = ((plate::bottom - py) << 16) / dy; if (t < best) best = t; }
    // The circle: t = -(p.d) + sqrt((p.d)^2 - (|p|^2 - R^2)), pixels.
    const i32 px = (x >> 10) - plate::centre_x, qy = py - plate::centre_y;
    const i32 pd = (px * (dx >> 4) + qy * (dy >> 4)) >> 12;
    const i32 c = px * px + qy * qy - plate::radius * plate::radius;
    const i32 disc = pd * pd - c;
    if (disc > 0) {
        const i32 t = -pd + static_cast<i32>(flight::isqrt(static_cast<u32>(disc)));
        if (t < best) best = t;
    }
    return best < 0 ? 0 : best;
}

// ((k + 0.5) / 16)^1.5, Q16: a shock's points crowd toward its nose.
const i32 crowd[16] = {362, 1881, 4048, 6705, 9775, 13208, 16970, 21033, 25376, 29984, 34840, 39934, 45255, 50793, 56539, 62488};

// A shock: the hyperbola x = b - sqrt(b^2 + (y cot mu)^2) behind its apex,
// asymptotic to the cone's two arms, drawn at twice its points' size: up and
// down are the arms' lengths in pixels, b its stand-off (Q4 pixels), width the
// band it is drawn as (Q10 pixels: a thump broad and soft, a crack fine).
//
// Drawn as the OP-XY draws an outline cleanly: along each arm its points are a
// quarter of a pixel apart at least (an eighth at its half size; the device
// merges closer neighbours), so the two at the nose never meet; and the
// outline starts one point along the upper arm, so it closes over a long
// edge, not over the arm's end (a closing edge under a quarter of a pixel
// draws as a sliver). width must be at least a quarter of a pixel.
inline void shock_points(Frame::path_type& path, i32 mach, i32 b4, i32 up, i32 down, i32 width) {
    const i32 k = flight::mul16(mach, mach) - flight::one;
    const i32 cot = static_cast<i32>(flight::isqrt(static_cast<u32>(k) << 12)) << 2;           // sqrt(M^2 - 1), Q16
    const i32 sin_mu = static_cast<i32>((1u << 31) / static_cast<u32>(mach >> 1));             // 1 / M, Q16
    i32 reach[2][16];                                                          // each arm's points out from the nose, Q10 pixels
    for (u32 a = 0; a < 2; ++a)
        for (u32 at = 0; at < 16; ++at) {
            const i32 yy = ((((a ? down : up) * sin_mu) >> 6) * (crowd[at] >> 6)) >> 10;
            const i32 least = at ? reach[a][at - 1] + 256 : 128;
            reach[a][at] = yy > least ? yy : least;
        }
    for (u32 i = 0; i < 32; ++i) {
        // 16 points up one arm to the nose, 16 down the other; each lands one
        // place earlier, so the outline starts at the upper arm's second point
        const u32 at = i < 16 ? 15 - i : i - 16;
        const i32 yy = reach[i < 16 ? 0 : 1][at];
        const i32 y = i < 16 ? -yy : yy;
        const i32 yc = (((yy >> 4) * (cot >> 4)) >> 8) >> 6;                   // y cot mu, Q4 pixels
        const i32 x = (b4 - static_cast<i32>(flight::isqrt(static_cast<u32>(b4 * b4 + yc * yc)))) << 6;   // Q10
        opxy::draw::Point& outer = path.points[(i + 63) & 63];
        opxy::draw::Point& inner = path.points[62 - i];
        outer.x = opxy::draw::clamp(x >> 1, -64 * unit, 64 * unit);
        outer.y = opxy::draw::clamp(y >> 1, -64 * unit, 64 * unit);
        inner.x = opxy::draw::clamp((x + width) >> 1, -64 * unit, 64 * unit);
        inner.y = outer.y;
    }
}

// The turbulent wake behind the tail: a straight trail `length` pixels long,
// widening as its eddies grow, its two edges billowing out of step, drawn at
// twice its points' size; phase rolls the eddies along.
inline void wake_points(Frame::path_type& path, i32 length, i32 phase) {
    using opxy::draw::sine;
    for (u32 i = 0; i < 32; ++i) {
        const i32 s = static_cast<i32>(i) * length * unit / 31;                // Q10 pixels behind the tail
        const i32 grow = s / length;                                            // 0 .. 1024
        const i32 half = 1000 + ((2600 * grow) >> 10);                          // half its width, Q10: 2 to 7 px wide
        // each edge billows by up to a third of the half-width, in 14 and 19 px eddies
        const i32 up = (half * (1024 + ((sine((s >> 10) * 257 + phase) * 340) >> 10))) >> 10;
        const i32 down = (half * (1024 + ((sine((s >> 10) * 189 + 1800 - phase) * 340) >> 10))) >> 10;
        path.points[i].x = -s >> 1;
        path.points[i].y = opxy::draw::clamp(-up >> 1, -64 * unit, 64 * unit);
        path.points[63 - i].x = (-s - 40) >> 1;
        path.points[63 - i].y = opxy::draw::clamp(down >> 1, -64 * unit, 64 * unit);
    }
}

inline void digits(char*& out, u32 v);
// A frequency in at most four characters: "760", "1.6k", "10k".
inline void hertz(char* out, i32 hz) {
    if (hz < 1000) { digits(out, static_cast<u32>(hz < 0 ? 0 : hz)); *out = 0; return; }
    const u32 tenths = static_cast<u32>(hz + 50) / 100;
    if (tenths < 100) { digits(out, tenths / 10); *out++ = '.'; digits(out, tenths % 10); }
    else digits(out, (tenths + 5) / 10);
    *out++ = 'k';
    *out = 0;
}

inline void digits(char*& out, u32 v) {
    char buf[8];
    u32 n = 0;
    do { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 7);
    while (n) *out++ = buf[--n];
}
// v (Q16) as "a.b" (tenths) or "a.bc" (hundredths) or whole.
inline void decimal(char*& out, i32 v, u32 places) {
    const u32 scale = places == 2 ? 100 : places == 1 ? 10 : 1;
    const u32 n = static_cast<u32>((static_cast<u32>(v) * scale + 32768u) >> 16);
    digits(out, n / scale);
    if (places) {
        *out++ = '.';
        if (places == 2 && n % 100 < 10) *out++ = '0';
        digits(out, n % scale);
    }
}
}

using namespace screen;

inline void display_reset(DisplayState& state) {
    state = DisplayState();
    state.header.reset();
    state.notes.reset();
    state.boom_age = state.echo_age = -1;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const NoteEvents events = state.notes.update(input);
    const i32 dt = clamp(input.elapsed, 0, 102);
    const bool snap = !state.initialized || state.track != input.track || input.reduced_motion;
    const Scene target = scene_of(input);
    if (!state.initialized) {
        // The reshaping outlines' first geometry: small rings, until drawn.
        for (u32 n = 0; n < 4; ++n) {
            for (u32 i = 0; i < Frame::point_count; ++i) {
                const Turn t = rotation(static_cast<i32>(i) * 3600 / 64);
                frame.paths[n].points[i].x = t.cos * 6;
                frame.paths[n].points[i].y = t.sin * 6;
            }
            frame.paths[n].key = ++state.key;
        }
        state.shock[0][2] = state.shock[1][2] = state.shock[2][2] = -1;
        state.wake[0] = -1;
    }

    if (snap) {
        state.eased = target;
        if (!state.initialized || state.track != input.track) {
            state.serial = input.note_serial;
            state.flying = 0;
        }
    } else {
        Scene& e = state.eased;
        e.mach = ease(e.mach, target.mach, dt);
        e.height = ease(e.height, target.height, dt);
        e.length = ease(e.length, target.length, dt);
        e.tune = ease(e.tune, target.tune, dt);
        e.distance = target.distance;
        e.echo = target.echo;
        e.boom = target.boom;
        e.source = target.source;
        // The scale and the waves follow the eased mach, dur and tune; the listener eases on its own.
        scale(e);
        e.reach = ease(e.reach, target.reach, dt);
    }

    // A note launches a flight, fixed at note-on; releasing every note, or
    // the flight's last sound arriving, ends it.
    if (input.note_serial != state.serial) {
        state.serial = input.note_serial;
        if (!input.reduced_motion) {
            state.flying = 1;
            state.flight = target;
            state.elapsed = 0;
            state.frames = 0;
            state.boom_age = state.echo_age = -1;
            state.before = flight::approach(target.length, target.tune);
            state.line = flight::timeline(target.mach, target.distance, target.length, state.before);
            // The boom is heard as the cone arrives; without one, the pass's
            // pulse as the pass is heard (with boom on, the pass in the flight).
            state.boom_at = state.line.has_cone ? state.line.cone
                          : target.boom > 0 && state.before > 0 && target.length > state.before ? target.distance - state.line.start : -1;
            // The plate's image of the flight passes at d + 2h, at the same time.
            state.echo_cone = -1;
            if (target.echo) {
                const i32 h = flight::sound_seconds(flight::height_metres(input.slots[ECHO]));
                const flight::Timeline image = flight::timeline(target.mach, target.distance + 2 * h, target.length, state.before);
                if (image.has_cone) state.echo_cone = image.start + image.cone - state.line.start;
            }
        }
    } else if (state.flying) {
        state.elapsed += dt;
        ++state.frames;
        if (state.boom_age >= 0) state.boom_age += dt;
        if (state.echo_age >= 0) state.echo_age += dt;
    }
    if (state.flying && (events.hold_ended || input.reduced_motion || (state.elapsed << 6) > state.line.length + 22938)) {
        state.flying = 0;
        state.boom_age = state.echo_age = -1;
    }

    const Scene& sc = state.flying ? state.flight : state.eased;
    const i32 cx = plate::centre_x * unit, ly = plate::line_y * unit;
    const i32 wx = cx, wy = ly + sc.reach;                                      // the wire's tip
    const i32 now = state.flying ? state.line.start + (state.elapsed << 6) : 0;  // listener time, Q16 s
    const i32 before = state.flying ? state.before : flight::approach(sc.length, sc.tune);
    const i32 after = sc.length - before;

    // The photograph's scale: pixels a sound-second, Q4, and the projectile's
    // speed in pixels a millisecond, Q14.
    const i32 k = sc.scale;
    const i32 speed = ((k * (sc.mach >> 4)) / 1000) >> 2;
    const u32 shape = sc.source < node::projectiles ? sc.source : 0;

    // --- the projectile ---
    i32 bx = cx;
    bool shown = true;
    if (state.flying) {
        const i32 t_ms = ((now >> 6) * 1000) >> 10;
        const i32 limit = speed > 0 ? 11468800 / speed : 1 << 20;     // keep |x| under 700 px
        bx = cx + clamp((clamp(t_ms, -limit, limit) * speed) >> 4, -800 * unit, 800 * unit);
        shown = now >= -before && now <= after && inside(bx, ly, 0, 30);
    }
    if (state.flying || snap || !state.bullet.r) state.bullet.x = bx;
    else state.bullet.x = ease(state.bullet.x, bx, dt);
    state.bullet.y = ly;
    state.bullet.r = shown ? 1 : 0;
    for (u32 i = 0; i < node::projectiles; ++i) {
        if (i == shape && shown) place(frame, node::projectile + i, state.bullet.x & ~255, ly, unit, metal, fill_and_stroke);
        else hide(frame, node::projectile + i);
    }

    // --- the sound waves: thick air absorbs the outer ones (from 58 px out
    // to 21 at full air) and dims the rest ---
    const i32 air = flight::amount(input.slots[AIR]);
    const i32 extent = radius * unit - ((radius * unit) >> 10) * ((air * 655) >> 15);
    for (u32 i = 0; i < node::rings; ++i) {
        i32 x = 0, r = 0;
        if (state.flying) {
            // Waves leave the projectile on a fixed grid of its own time, one
            // every step; wave i is the i-th newest, so it is drawn near its
            // node's own size (10 (i + 1) px) and its line keeps its weight.
            const i32 step = (sc.wave * 15625) / k;                             // microseconds: the spacing at k
            const i32 t_us = ((now >> 6) * 15625) >> 4;                         // Q16 s to us (to 16 s)
            const i32 newest = t_us >= 0 ? t_us / step : -((-t_us + step - 1) / step);
            const i32 j = newest - static_cast<i32>(i);
            const i32 tau_us = j * step;
            if (tau_us >= -(((before >> 7) * 15625) >> 3) && tau_us <= (((after >> 7) * 15625) >> 3) && tau_us <= t_us) {
                r = (k * (t_us - tau_us)) / 15625;   // Q10 px (k * step is constant, so no overflow)
                x = cx + clamp((((tau_us / 1000) * speed) >> 4), -800 * unit, 800 * unit);
                if (r > radius * unit) r = 0;
            }
        } else {
            // Idle: wave i + 1 sent out i + 1 steps ago, the spacing apart
            // (at least 10 px: closer would blur on the screen).
            r = (static_cast<i32>(i) + 1) * sc.wave;
            x = cx - ((r * (sc.mach >> 4)) >> 12);
        }
        if (r > extent || (r && !inside(x, ly, r, 1))) r = 0;
        Mark& m = state.ring[i];
        if (state.flying || snap || !m.r || !r) { m.x = x; m.r = r; }
        else { m.x = ease(m.x, x, dt); m.r = ease(m.r, r, dt); }
        if (m.r > 0) {
            // Quantized to half a pixel; dimmer with distance, more so in thick air.
            const i32 far = (m.r * 16) / (radius * unit);                     // 0..16
            const i32 grey = ring_near - ((far * (2 * 32768 + 8 * air)) >> 19);
            place(frame, node::ring + i, m.x & ~511, ly, clamp(((m.r & ~511) + 256) / (plate::ring_radius * static_cast<i32>(i + 1)), 1, 2 * unit),
                  static_cast<u32>(clamp(grey, 5, 15)), stroke);
        } else hide(frame, node::ring + i);
    }

    // --- the shocks, above Mach 1: a thump a broad, dim band, a crack a fine,
    // bright line; below it (boom on) a bow wave ahead of the nose, as wide as
    // a shock at Mach 1.05, its arms 10 px long at Mach 0.3 and 28 near 1 ---
    const bool cone = sc.mach > flight::one + 256;
    const bool bow_wave = !cone && sc.boom > 0;
    const i32 shape_mach = cone ? sc.mach : 68813;
    const i32 root = static_cast<i32>(flight::isqrt(static_cast<u32>(flight::mul16(shape_mach, shape_mach) - flight::one) << 12)) << 2;
    const i32 sin_mu = static_cast<i32>((1u << 31) / static_cast<u32>(shape_mach >> 1));
    const i32 cos_mu = flight::div16(root, shape_mach);
    // A ribbon the device outlines with a 1 px line, so it reads as one line:
    // 2.6 px, dim and rounded (its front standing up to 16 px further off) as
    // a thump; 1.4 px, bright and sharp as a crack; a dim hairline off.
    const i32 width = sc.boom <= 0 ? 300 : 400 + (((32768 - sc.boom) * 1200) >> 15);      // Q10 px between its edges
    const u32 shock_grey = sc.boom <= 0 ? 5u : static_cast<u32>(9 + ((sc.boom * 6) >> 15));
    const i32 blunt = sc.boom <= 0 ? 0 : ((32768 - sc.boom) * 256) >> 15;                // Q4 px: up to 16
    const i32 look = (static_cast<i32>(shape) << 16) + width + (blunt << 12);
    for (u32 which = 0; which < 2; ++which) {
        const u32 n = which ? node::tail : node::bow;
        const i32 standoff = plate::standoff[shape] + blunt + (bow_wave ? 48 : 0);           // Q4 px: a bow wave 3 px further off
        const i32 offset = which ? plate::tail[shape] : plate::nose[shape] + (standoff << 6);
        const i32 ax = state.bullet.x + offset;
        i32* last = state.shock[which];
        if (!(cone || (bow_wave && !which)) || !state.bullet.r || !inside(ax, ly, 0, 2)) { hide(frame, n); last[2] = -1; continue; }
        const i32 most = bow_wave ? 10 + ((18 * sc.mach) >> 16) : which ? 100 : 124;
        const i32 up = reach_edge(ax, ly, -cos_mu, -sin_mu, most) & ~3, down = reach_edge(ax, ly, -cos_mu, sin_mu, most) & ~3;
        if (up < 8 || down < 8) { hide(frame, n); last[2] = -1; continue; }
        if (last[2] != up || last[3] != down || state.shock_mach != shape_mach || state.shock_look != look) {
            shock_points(frame.paths[n], shape_mach, which ? 8 + (blunt >> 1) : standoff, up, down, which && width > 400 ? (width * 3) >> 2 : width);
            frame.paths[n].key = ++state.key;
            last[2] = up; last[3] = down;
        }
        place(frame, n, ax & ~255, ly, 2 * unit, which ? (shock_grey > 8 ? shock_grey - 3 : 5) : shock_grey, stroke);
    }
    state.shock_mach = shape_mach;
    state.shock_look = look;

    // --- the plate, and the shock it reflects ---
    if (sc.echo) place(frame, node::plate, cx, (wy + sc.height) & ~511, unit, plate_grey, stroke);
    else hide(frame, node::plate);
    bool reflected = false;
    const i32 py = wy + sc.height;
    if (sc.echo && cone && state.shock[0][2] >= 0) {
        const i32 ax = state.bullet.x + plate::nose[shape] + (plate::standoff[shape] << 6);
        // pixels along the lower arm (-cos mu, sin mu) to the plate, Q10
        const i32 along = py > ly ? static_cast<i32>((static_cast<u32>(py - ly) << 14) / static_cast<u32>(sin_mu >> 2)) : -1;
        if (along > 0 && (along >> 10) <= state.shock[0][3]) {
            const i32 gx = ax - ((along >> 4) * (cos_mu >> 4) >> 8);
            if (gx > cx - 150 * unit && gx < cx + 150 * unit) {
                // From where the cone meets the plate, back up as its mirror.
                const i32 len = reach_edge(gx, py, -cos_mu, -sin_mu, 120) & ~3;
                i32* last = state.shock[2];
                if (len >= 12) {
                    if (last[2] != len || last[3] != (cos_mu >> 8)) {
                        // A ribbon half a pixel wide, starting one point along
                        // its edge so it closes over a long edge (as a shock).
                        Frame::path_type& path = frame.paths[node::reflected];
                        for (u32 i = 0; i < 32; ++i) {
                            const i32 a = (len * unit * static_cast<i32>(i)) / 31;
                            const i32 x = -(((a >> 4) * (cos_mu >> 4)) >> 8), y = -(((a >> 4) * (sin_mu >> 4)) >> 8);
                            path.points[(i + 63) & 63].x = x >> 1;
                            path.points[(i + 63) & 63].y = y >> 1;
                            path.points[62 - i].x = (x + ((500 * sin_mu) >> 16)) >> 1;
                            path.points[62 - i].y = (y - ((500 * cos_mu) >> 16)) >> 1;
                        }
                        path.key = ++state.key;
                        last[2] = len; last[3] = cos_mu >> 8;
                    }
                    place(frame, node::reflected, gx & ~255, py & ~511, 2 * unit, reflected_grey, stroke);
                    reflected = true;
                }
            }
        }
    }
    if (!reflected) { hide(frame, node::reflected); state.shock[2][2] = -1; }

    // --- the wake: as long as the flight, its eddies rolling while it flies ---
    {
        const i32 length = 30 + ((flight::log2_q16(static_cast<u32>(sc.length)) - (15 << 16)) * 24 >> 16);   // 30 .. 126 px
        const i32 phase = state.flying ? static_cast<i32>((state.frames >> 2) * 470 % 3600) : 0;            // a step every 4 frames
        const i32 tx = state.bullet.x + plate::tail[shape];
        const i32 room = reach_edge(tx, ly, -65536, 0, length);
        if (state.bullet.r && room >= 12) {
            if (state.wake[0] != room || state.wake[1] != phase) {
                wake_points(frame.paths[node::wake], room, phase);
                frame.paths[node::wake].key = ++state.key;
                state.wake[0] = room; state.wake[1] = phase;
            }
            place(frame, node::wake, tx & ~255, ly, 2 * unit, wake_grey, stroke);
        } else { hide(frame, node::wake); state.wake[0] = -1; }
    }

    // --- the listener, and the booms' flashes at its ear ---
    place(frame, node::wire, wx, wy & ~511, unit, wire_grey, fill_and_stroke);
    if (state.flying && state.boom_at >= 0 && state.boom_age < 0 && (state.elapsed << 6) >= state.boom_at) state.boom_age = 0;
    if (state.flying && state.echo_cone >= 0 && state.echo_age < 0 && (state.elapsed << 6) >= state.echo_cone) state.echo_age = 0;
    const i32 age = state.boom_age >= 0 && state.boom_age < flash ? state.boom_age
                  : state.echo_age >= 0 && state.echo_age < flash ? state.echo_age : -1;
    if (age >= 0) {
        const i32 s = 614 + (age * 1434) / flash;                     // 0.6 .. 2 of its 10 px
        place(frame, node::glow, wx, (wy - unit) & ~511, s & ~63, static_cast<u32>(15 - (age * 9) / flash), stroke);
    } else hide(frame, node::glow);

    // --- where the sound heard now left the projectile ---
    // The roots of t = tau + |r(tau)|, in Q10 seconds: forwards, and above
    // Mach 1 (after the cone) backwards; a mark on the line at each, while it
    // lies in the flight.
    {
        u32 marks = 0;
        if (state.flying) {
            const i32 s10 = now >> 6, d10 = sc.distance >> 6;
            const i32 ms = ((sc.mach >> 4) * s10) >> 12;                           // M s, Q10
            const i32 k10 = (flight::mul16(sc.mach, sc.mach) - flight::one) >> 6;  // M^2 - 1, Q10
            const i32 d2 = d10 * d10;                                               // Q20
            // M^2 s^2 - (M^2 - 1) d^2, Q20 (unsigned: M s reaches 48 s)
            const u32 square = static_cast<u32>(ms) * static_cast<u32>(ms), part = static_cast<u32>(k10 < 0 ? -k10 : k10) * static_cast<u32>(d2 >> 10);
            const bool real = ms >= 0 && (k10 <= 0 || square >= part);
            if (real && (k10 <= 0 || s10 > 0)) {
                const i32 rt = static_cast<i32>(flight::isqrt(k10 <= 0 ? square + part : square - part));
                i32 sigma[2] = {0, 0};
                u32 count = 0;
                if (s10 + rt > 0) sigma[count++] = (s10 * s10 - d2) / (s10 + rt);        // forwards
                if (k10 > 0) sigma[count++] = -(((s10 + rt) << 10) / k10);              // backwards
                for (u32 j = 0; j < count; ++j) {
                    if (sigma[j] < -(before >> 6) || sigma[j] > (after >> 6)) continue;
                    const i32 hx = cx + clamp((sigma[j] * speed) >> 4, -800 * unit, 800 * unit);
                    if (!inside(hx, ly, 0, 4)) continue;
                    place(frame, node::heard + marks, hx & ~511, ly, unit, bright, stroke);
                    ++marks;
                }
            }
        }
        for (u32 j = marks; j < node::heards; ++j) hide(frame, node::heard + j);
    }

    // --- the header: real units ---
    static const char* const titles[8] = {"mach", "pass", "source", "dur", "boom", "air", "tune", "echo"};
    static const char* const names[SOURCES] = {"organ", "pad", "voice", "bell", "siren"};
    char values[8][header_capacity] = {};
    {
        // mach: the Mach number itself, "1.67" (with an M it would run into
        // the next column in the OP-XY's font)
        char* o = values[MACH];
        decimal(o, flight::mach(input.slots[MACH]), 2);
        *o = 0;
        o = values[PASS];
        const i32 metres = flight::pass_metres(input.slots[PASS]);
        decimal(o, metres, metres < 10 * flight::one ? 1 : 0);
        *o++ = 'm'; *o = 0;
        copy(values[SOURCE], header_capacity, names[flight::source(input.slots[SOURCE])]);
        o = values[DUR];
        decimal(o, flight::dur(input.slots[DUR]), 1);
        *o++ = 's'; *o = 0;
        // boom: the shock's rise, 100 Hz * 100^boom (a thump low, a crack high)
        const i32 b = flight::amount(input.slots[BOOM]);
        if (b <= 0) copy(values[BOOM], header_capacity, "off");
        else hertz(values[BOOM], (100 * flight::exp2_q16((b * 13607) >> 10)) >> 16);
        // air: the corner of its low-pass at the closest pass
        const i32 corner = flight::air_hertz(flight::amount(input.slots[AIR]), metres);
        if (corner >= 20000) copy(values[AIR], header_capacity, "open");
        else hertz(values[AIR], corner);
        // tune: where the flight's end settles against the key, semitones:
        // 0 at tune's top, where it lands on the key
        const i32 u = flight::tune(input.slots[TUNE]), len = flight::dur(input.slots[DUR]);
        const i32 span = flight::tune_span(flight::mach(input.slots[MACH]), flight::sound_seconds(metres), len, flight::approach(len, u));
        const i32 st = (flight::mul16(span, (65536 - u) >> 1) * 12 + 32768) >> 16;
        o = values[TUNE];
        if (st > 0) *o++ = '-';
        digits(o, static_cast<u32>(st));
        *o++ = 's'; *o++ = 't'; *o = 0;
        if (flight::echo_level(input.slots[ECHO]) <= 0) copy(values[ECHO], header_capacity, "off");
        else {
            o = values[ECHO];
            const i32 h = flight::height_metres(input.slots[ECHO]);
            decimal(o, h, h < 10 * flight::one ? 1 : 0);
            *o++ = 'm'; *o = 0;
        }
    }
    page_header(state.header, input, frame, titles, values, node::header);

    state.initialized = 1;
    state.track = input.track;
}
}
