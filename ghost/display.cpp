// Ghost's screen (README, "Screen"): Pepper's ghost, a theatre's stage in
// a few lines. The note's apparition stands in the glass where the sound
// is in the head: across the stage for left and right, upstage behind the
// listener, downstage and larger when near. Every control acts on the
// apparition or the stage.
#include "display.h"
#include "captions.h"
#include "figure.h"
#include <opxy/sample_overview.hpp>

namespace ghost {
namespace {
using namespace opxy::draw;

u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Where a place round the head appears on the stage (Q10), and how much
// dimmer it is for being behind (0..4 shades).
struct Spot { i32 x, y, scale; u32 dim; };
Spot spot(const Place& p) {
    const i32 side = sine(p.angle), front = cosine(p.angle);
    i32 z = 8192 + i32(p.near >> 2) + ((front * 9830) >> 15);                   // Q15: 0 upstage .. 1 at the apron
    z = clamp(z, 0, 32767);
    Spot s;
    s.y = (stage_back + (((front_feet - stage_back) * z) >> 15)) * unit;
    const i32 half = half_back + (((half_front - half_back) * z) >> 15) - 44;
    s.x = (240 + ((side * half) >> 15)) * unit;
    s.scale = 614 + ((870 * z) >> 15);                                         // 0.6 .. 1.45
    s.dim = u32(front < 0 ? (-front * 4) >> 15 : 0);
    return s;
}

// The vowel's mouth, rx and ry in tenths of a pixel: breath (closed), oo, oh, ah, eh, ee.
const opxy::i16 mouth_rx[vowel_count] = {16, 18, 22, 29, 38, 45}, mouth_ry[vowel_count] = {8, 22, 32, 42, 26, 16};

// The apparition and its twin: posed by voice (bowed at breath, singing at
// ee); trembling all round by beat, the tremble running at beat's rate while
// held; its robe frayed as bed rises, into the sample's own waveform or,
// without one, the whisper's ragged gusts, scrolling while held; its hem
// rippling while held.
void shape_figure(Frame& frame, u32 voice, u32 beat, u32 tremble, u32 bed, const SampleOverview& overview, u32 step, bool held) {
    const u32 pose = (voice + 1024) >> 11, shake = (beat + 1024) >> 11, fray = (bed + 1024) >> 11;   // 33 steps each
    const bool sampled = fray && overview.assigned && overview.loaded;
    u32 peaks_key = sampled ? 1 : 0;
    if (sampled) for (u32 i = 0; i < 64; i += 4) peaks_key = peaks_key * 31 + overview.peaks[i];
    const u32 key = hash((pose | shake << 6 | tremble << 12 | fray << 16 | (held ? step : 0) << 22 | u32(held) << 30) ^ peaks_key) | 1;
    if (frame.paths[apparition].key == key) return;
    // The bed's peaks about their mean, scaled so the widest reaches the fray;
    // without a sample, gusts of 0..31 about 16.
    i32 mean = 16, widest = 16;
    if (sampled) {
        mean = 0; widest = 1;
        for (u32 i = 0; i < 64; ++i) mean += overview.peaks[i];
        mean >>= 6;
        for (u32 i = 0; i < 64; ++i) widest = widest > i32(overview.peaks[i]) - mean ? (widest > mean - i32(overview.peaks[i]) ? widest : mean - i32(overview.peaks[i])) : i32(overview.peaks[i]) - mean;
    }
    const i32 t = i32(pose * 1024);                                            // Q15
    const i32 shake_q10 = i32(shake) * 6 * unit / 32;                          // up to 6 px
    const i32 fray_q10 = i32(fray) * 8 * unit / 32;                           // up to 8 px
    // The pose, then each point pushed along the outline's outward normal (an
    // x push slides points along level edges, the hem and raised arms, until
    // they fold back, which the OP-XY draws as streaks). Inward pushes are
    // halved, so concave turns never fold either; the hem keeps its own
    // ripple, up and down, clear of the footlights.
    Point posed[64];
    for (u32 i = 0; i < 64; ++i) {
        const i32 bx = figure_bowed[2 * i], by = figure_bowed[2 * i + 1];
        const i32 sx = figure_singing[2 * i], sy = figure_singing[2 * i + 1];
        posed[i].x = (bx + (((sx - bx) * t) >> 15)) * 256;                    // quarter px to Q10
        posed[i].y = (by + (((sy - by) * t) >> 15)) * 256;
    }
    for (u32 i = 0; i < 64; ++i) {
        i32 x = posed[i].x, y = posed[i].y;
        if (i < hem_points) {
            if (held) y += sine(step * 0x20000000u + i * 0x18000000u) * 6 * 256 / 32767;
        } else {
            // beat: nine waves round the outline, pushing it in and out.
            i32 push = (sine(i * 0x09000000u + tremble * 0x20000000u) * shake_q10) >> 15;
            // bed: the recording's peaks along the robe, scrolling as it plays.
            if (fray) {
                const u32 at = (i + step * 3) & 63;
                // Without a sample, gusts: random strengths, each the mean of two.
                const i32 peak = sampled ? i32(overview.peaks[at]) : i32((hash(at * 0x9e3779b9u + 7) >> 27) + (hash(((at + 1) & 63) * 0x9e3779b9u + 7) >> 27)) >> 1;
                push += (peak - mean) * fray_q10 / widest;
            }
            if (push < 0) push /= 2;
            // The normal: across the chord between the neighbours, turned
            // away from the figure's middle (half its height, on its axis).
            const Point& before = posed[(i + 63) & 63], &after = posed[(i + 1) & 63];
            i32 nx = before.y - after.y, ny = after.x - before.x;
            if (nx * (x >> 4) + ny * ((y + 30 * unit) >> 4) < 0) { nx = -nx; ny = -ny; }
            const i32 ax = nx < 0 ? -nx : nx, ay = ny < 0 ? -ny : ny;
            const i32 length = (ax > ay ? ax + ay / 2 : ay + ax / 2) | 1;     // within 12%
            x += push * nx / length;
            y += push * ny / length;
        }
        frame.paths[apparition].points[i].x = clamp(x, -63 * unit, 63 * unit);
        frame.paths[apparition].points[i].y = clamp(y, -63 * unit, 63 * unit);
    }
    // Where pushes bring a point within an eighth of a pixel of the last, or
    // make the outline turn straight back at it, it and its neighbours return
    // to their places in the pose, whose points are all half a pixel or more
    // apart.
    Point* p = frame.paths[apparition].points;
    for (u32 pass = 0; pass < 8; ++pass) {
        bool moved = false;
        for (u32 i = 0; i < 64; ++i) {
            const u32 before = (i + 63) & 63, after = (i + 1) & 63;
            const Point& a = p[before], &b = p[i], &c = p[after];
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            const bool merged = dx > -unit / 8 && dx < unit / 8 && dy > -unit / 8 && dy < unit / 8;
            const i32 ux = dx >> 4, uy = dy >> 4, vx = (c.x - b.x) >> 4, vy = (c.y - b.y) >> 4;   // 1/64 px
            const i32 cross = ux * vy - uy * vx, dot = ux * vx + uy * vy;
            const i32 lengths = ((ux < 0 ? -ux : ux) + (uy < 0 ? -uy : uy)) * ((vx < 0 ? -vx : vx) + (vy < 0 ? -vy : vy));
            const bool back = dot < 0 && (cross < 0 ? -cross : cross) < lengths / 25;
            if (!merged && !back) continue;
            const u32 near[3] = {before, i, after};
            for (u32 k = 0; k < 3; ++k)
                if (p[near[k]].x != posed[near[k]].x || p[near[k]].y != posed[near[k]].y) { p[near[k]] = posed[near[k]]; moved = true; }
        }
        if (!moved) break;
    }
    for (u32 i = 0; i < 64; ++i) frame.paths[twin].points[i] = p[i];
    frame.paths[apparition].key = frame.paths[twin].key = key;
}

// The mouth: the vowel's shape, on the face. Its radii in tenths of a pixel
// and its height above the feet (Q10), for the twin's mouth too.
struct Mouth { i32 rx, ry, cy; };
Mouth mouth_for(u32 voice) {
    const u32 step = (voice + 1024) >> 11;
    const u32 position = vowel_position(step * 2048 > 65535 ? 65535 : step * 2048);
    const u32 index = position >> 16, part = position & 0xffff, upper = index + 1 < vowel_count ? index + 1 : index;
    Mouth m;
    m.rx = mouth_rx[index] + (((mouth_rx[upper] - mouth_rx[index]) * i32(part >> 1)) >> 15);
    m.ry = mouth_ry[index] + (((mouth_ry[upper] - mouth_ry[index]) * i32(part >> 1)) >> 15);
    const i32 t = i32(step * 1024);
    m.cy = (mouth_bowed[1] + (((mouth_singing[1] - mouth_bowed[1]) * t) >> 15)) * 256;
    return m;
}
// A small ellipse as an outline the OP-XY draws cleanly: 20 points, each a
// quarter of a pixel or more from the next even at 1.6 x 0.8 px, the rest
// repeating the last (a repeated point only pads; points nearer than an
// eighth of a pixel would merge on the device). It starts at the top, where
// its edges are longest, so its closing edge is long too.
void ellipse_points(Frame::path_type& p, i32 cx, i32 cy, i32 rx, i32 ry) {   // rx, ry Q10
    for (u32 i = 0; i < 64; ++i) {
        const u32 k = i < 20 ? i : 19;
        const u32 angle = (k + 5) * 214748365u;                               // twentieths of a turn, from the top
        p.points[i].x = cx + ((cosine(angle) * rx) >> 15);
        p.points[i].y = cy + ((sine(angle) * ry) >> 15);
    }
}
void shape_mouth(Frame& frame, u32 voice) {
    const u32 key = ((voice + 1024) >> 11) + 7;
    if (frame.paths[mouth].key == key) return;
    const Mouth m = mouth_for(voice);
    ellipse_points(frame.paths[mouth], 0, m.cy, m.rx * unit / 10, m.ry * unit / 10);
    frame.paths[mouth].key = key;
}

// The chalk path on the floor: everywhere orbit lets a note stand, at near's
// distance. A mark at the front at 0; an arc round the head as orbit spreads
// the notes; the whole circle once they circle.
void shape_floor(Frame& frame, u32 orbit, u32 near) {
    const u32 o = (orbit + 1024) >> 11, n = (near + 1024) >> 11;
    const u32 key = (o | n << 6) + 3;
    if (frame.paths[floor_path].key == key) return;
    const u32 near16 = n * 2048 > 65535 ? 65535 : n * 2048;
    const u32 orbit16 = o * 2048 > 65535 ? 65535 : o * 2048;
    Frame::path_type& p = frame.paths[floor_path];
    if (!o) {   // a mark where the notes stand: a small ring at the front
        const Spot at = spot(place(0, 0, orbit16, near16));
        ellipse_points(p, (at.x - path_x * unit) / 2, (at.y - path_y * unit) / 2, 7 * unit / 2, unit);
    } else {
        // A ribbon along the arc, a quarter pixel to each side of its middle,
        // square to the way it runs: an edge offset only up and down lies on
        // top of the other where the arc runs up the side of the stage.
        i32 x[32], y[32];
        for (u32 i = 0; i < 32; ++i) {
            const i32 a = i32(i * 2048) - 32768 + 1024;                       // -1..1 of a half turn, Q15
            const u32 angle = u32(a) * 65536u;
            // Spread, a start angle that place() narrows by orbit; circling, a
            // circled angle, near swinging with the side as it does.
            const Spot s = spot(orbit16 > 16384 ? place(0, angle, orbit16, near16) : place(angle, 0, orbit16, near16));
            x[i] = (s.x - path_x * unit) / 2;
            y[i] = (s.y - path_y * unit) / 2;
        }
        for (u32 i = 0; i < 32; ++i) {
            const i32 dx = x[i < 31 ? i + 1 : 31] - x[i ? i - 1 : 0], dy = y[i < 31 ? i + 1 : 31] - y[i ? i - 1 : 0];
            const i32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            const i32 length = ax > ay ? ax + ay * 3 / 8 : ay + ax * 3 / 8;   // near enough
            const i32 nx = length ? -dy * (unit / 4) / length : 0, ny = length ? dx * (unit / 4) / length : unit / 4;
            p.points[i].x = x[i] + nx;
            p.points[i].y = y[i] + ny;
            p.points[63 - i].x = x[i] - nx;
            p.points[63 - i].y = y[i] - ny;
        }
    }
    p.key = key;
}

// Eases value toward target: most of the way in 0.15 s.
i32 ease(i32 value, i32 target, u32 ms) {
    const i32 m = i32(ms > 150 ? 150 : ms);
    return value + (target - value) * m / (m + 30);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.track = ~0u;
    state.start = 0;
    state.circled = state.trade = 0;
    state.presence = 0;
    state.linger = 0;
    state.scale = 0;
    state.beat_ms = 0;
    state.step_ms = state.step = 0;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const SampleOverview& overview = *opxy_sample_overview();
    char values[8][header_capacity] = {};
    captions::fill(input, overview.assigned != 0, values);
    const opxy::u8 pages[2][4] = {{kRing, kVoice, kSplit, kTrade}, {kBeat, kOrbit, kNear, kBed}};
    page_header(state.header, input, frame, captions::titles, values, header_first, pages);

    if (state.track != input.track) {
        const PageHeader header = state.header;
        display_reset(state);
        state.header = header;
        state.track = input.track;
    }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const bool still = input.reduced_motion != 0;
    const bool moving = held && !still;
    const u32 ms = u32(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
    const u32 ring = level(input.slots[kRing]), sung = level(input.slots[kVoice]), split = level(input.slots[kSplit]);
    const u32 trade = level(input.slots[kTrade]), beat = level(input.slots[kBeat]), orbit = level(input.slots[kOrbit]);
    const u32 near = level(input.slots[kNear]), bed = level(input.slots[kBed]);

    if (events.hold_started) {
        // A new apparition: where its note begins round the head.
        state.start = hash(input.note_serial * 0x9e3779b9u);
        state.circled = state.trade = 0;
        state.beat_ms = 0;
        state.scale = 0;   // appears in place, not eased from the last
    }
    if (moving) {
        state.circled += orbit_mhz(orbit) * orbit_ms * ms;
        state.trade += trade_mhz(trade) * orbit_ms * ms;
        state.beat_ms += ms;
        state.step_ms += ms;
        if (state.step_ms >= 100) { state.step_ms -= 100; state.step = (state.step + 1) & 63; }
    }
    // Present while held, easing in; on release it fades over ring's time.
    const u32 ring_time = ring_ms(ring);
    state.linger = ring_time > 2500 ? 2500 : ring_time;
    if (still) state.presence = held ? 65536 : 0;
    else if (held) state.presence = u32(ease(i32(state.presence), 65536, ms));
    else {
        const u32 fall = 65536 * ms / (state.linger ? state.linger : 1);
        state.presence = state.presence > fall ? state.presence - fall : 0;
    }

    // Where it stands, eased; snapped as a note begins.
    const Spot at = spot(place(state.start, state.circled, orbit, near));
    if (!state.scale || still) { state.x = at.x; state.y = at.y; state.scale = at.scale; }
    else { state.x = ease(state.x, at.x, ms); state.y = ease(state.y, at.y, ms); state.scale = ease(state.scale, at.scale, ms); }

    // The beat's phase while held, in eight steps a cycle: the tremble runs,
    // and the halos breathe, at beat's rate.
    const u32 beat_rate = beat_mhz(beat);
    const u32 beat_phase = moving && beat_rate ? state.beat_ms * beat_rate * 4295u : 0;
    const u32 tremble = beat_phase >> 29;
    shape_figure(frame, sung, beat, tremble, bed_share(bed, overview.assigned && overview.loaded), overview, state.step, moving);
    shape_mouth(frame, sung);
    shape_floor(frame, orbit, near);
    const u32 presence = state.presence >> 12;                                 // 0..16
    i32 shade = 3 + i32(presence * 9 / 16) - i32(at.dim);
    shade = clamp(shade, 1, 15);

    // split: the pane's two faces show two apparitions, one at 0 and side by
    // side at the full split, each with its own mouth and halos; trade leans
    // them toward each other and swings them through each other.
    const i32 apart = ((i32(split >> 1) * cosine(state.trade)) >> 15) * 72 / 32 * state.scale >> 10;   // Q10 px, up to 72
    // Both stay between the pilasters: a figure reaches 36 px out at unit
    // size, singing, trembling and frayed.
    const i32 half_apart = apart / 2, reach = (half_apart < 0 ? -half_apart : half_apart) + 36 * state.scale;
    const i32 centre = clamp(state.x, 62 * unit + reach, 418 * unit - reach);
    const bool twinned = apart > unit / 2 || apart < -unit / 2;
    const i32 lean = twinned ? i32(trade * 200 >> 16) * (apart > 0 ? 1 : -1) : 0;   // tenths of a degree
    // Leaning drops a corner of the hem (36 px out, frayed and trembling, at
    // unit size) below the feet: as much lift keeps it off the footlights. It
    // follows trade alone, so it holds as the twins pass through each other.
    const i32 lift = split ? (36 * state.scale * rotation(i32(trade * 200 >> 16)).sin) >> 10 : 0;   // Q10
    const i32 feet = state.y - lift;
    const i32 first_x = centre - half_apart, second_x = centre + half_apart;
    const Turn tilt = rotation(lean), back = rotation(-lean);
    // Each figure over its black silhouette, which hides what stands behind.
    place(frame, mask, first_x, feet, state.scale, 0, keep);
    turn(frame, mask, tilt);
    place(frame, apparition, first_x, feet, state.scale, u32(shade), stroke);
    turn(frame, apparition, tilt);
    place(frame, mouth, first_x, feet, state.scale, u32(shade), stroke);
    turn(frame, mouth, tilt);
    // The face, cy above the feet, riding each figure's turn.
    const Mouth m = mouth_for(sung);
    const i32 cy = m.cy * state.scale >> 10;
    const i32 face_x[2] = {first_x - ((tilt.sin * cy) >> 10), second_x - ((back.sin * cy) >> 10)};
    const i32 face_y[2] = {feet + ((tilt.cos * cy) >> 10), feet + ((back.cos * cy) >> 10)};
    const i32 twin_shade = shade;   // the glass's two faces give images alike
    if (twinned) {
        place(frame, twin_mask, second_x, feet, state.scale, 0, keep);
        turn(frame, twin_mask, back);
        place(frame, twin, second_x, feet, state.scale, u32(twin_shade), stroke);
        turn(frame, twin, back);
        place(frame, twin_mouth, face_x[1], face_y[1], (state.scale * (m.rx + m.ry)) / 65, u32(twin_shade), stroke);
    } else { hide(frame, twin); hide(frame, twin_mouth); hide(frame, twin_mask); }

    // ring: a nimbus round each head, none at 0 to three rings at the
    // longest; they breathe at beat's rate while held. The figure, drawn
    // over them, hides what passes behind it.
    const u32 halos = (ring * 3 + 32768) >> 16;
    const i32 breath = beat_rate && moving ? (sine(beat_phase & 0xe0000000u) * 51) >> 15 : 0;   // +-5%, Q10
    for (u32 k = 0; k < halo_count; ++k) {
        if (k >= halos) { hide(frame, halo_first + k); hide(frame, twin_halo_first + k); continue; }
        const i32 size = clamp((state.scale * ((683 + 307 * i32(k)) * (1024 + breath) >> 10)) >> 10, unit / 4, 2 * unit);   // 10, 14.5 and 19 px at unit size
        const i32 halo_shade = shade - 2 - i32(k);                               // a step down, and dimmer outward
        place(frame, halo_first + k, face_x[0], face_y[0], size, u32(halo_shade < 1 ? 1 : halo_shade), stroke);
        if (twinned) place(frame, twin_halo_first + k, face_x[1], face_y[1], size, u32(halo_shade < 1 ? 1 : halo_shade), stroke);
        else hide(frame, twin_halo_first + k);
    }

    // More held notes: more apparitions, each where its own note began.
    for (u32 k = 0; k < 2; ++k) {
        if (held && input.held_notes >= k + 2) {
            const Spot other = spot(place(hash(state.start + (k + 1) * 0x6c078965u), state.circled, orbit, near));
            place(frame, extra_first + k, other.x, other.y, other.scale, u32(shade > 3 ? shade - 2 : 1), stroke);
        } else hide(frame, extra_first + k);
    }

    // The chalk path, brighter while notes are held.
    place(frame, floor_path, path_x * unit, path_y * unit, 2 * unit, held ? 8 : 6, stroke);

    // No sample: Grains' hint under the apron, the stage playing on above it.
    if (!overview.assigned) hide(frame, hint_cover);
    else place(frame, hint_cover, 0, 0, unit, 0, fill_and_stroke);

}
}
