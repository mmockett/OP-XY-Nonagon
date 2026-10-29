// Ghost's screen (README, "Screen"): Pepper's ghost, an engraved plate of a
// theatre's stage. The note's apparition stands in the glass where the sound
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
    i32 z = 8192 + i32(p.near >> 2) + ((front * 9830) >> 15);                   // Q15: 0 upstage .. 1 at the footlights
    z = clamp(z, 0, 32767);
    Spot s;
    s.y = (stage_back + (((stage_front - 4 - stage_back) * z) >> 15)) * unit;
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
// held; its robe frayed into the bed's own waveform as bed rises, scrolling
// while held; its hem rippling while held.
void shape_figure(Frame& frame, u32 voice, u32 beat, u32 tremble, u32 bed, const SampleOverview& overview, u32 step, bool held) {
    const u32 pose = (voice + 1024) >> 11, shake = (beat + 1024) >> 11, fray = (bed + 1024) >> 11;   // 33 steps each
    const bool sampled = fray && overview.assigned && overview.loaded;
    u32 peaks_key = 0;
    if (sampled) for (u32 i = 0; i < 64; i += 4) peaks_key = peaks_key * 31 + overview.peaks[i];
    const u32 key = hash((pose | shake << 6 | tremble << 12 | (sampled ? fray : 0) << 16 | (held ? step : 0) << 22 | u32(held) << 30) ^ peaks_key) | 1;
    if (frame.paths[apparition].key == key) return;
    // The bed's peaks about their mean, scaled so the widest reaches the fray.
    i32 mean = 0, widest = 1;
    if (sampled) {
        for (u32 i = 0; i < 64; ++i) mean += overview.peaks[i];
        mean >>= 6;
        for (u32 i = 0; i < 64; ++i) widest = widest > i32(overview.peaks[i]) - mean ? (widest > mean - i32(overview.peaks[i]) ? widest : mean - i32(overview.peaks[i])) : i32(overview.peaks[i]) - mean;
    }
    const i32 t = i32(pose * 1024);                                            // Q15
    const i32 shake_q10 = i32(shake) * 4 * unit / 32;                          // up to 4 px
    const i32 fray_q10 = sampled ? i32(fray) * 8 * unit / 32 : 0;             // up to 8 px
    for (u32 i = 0; i < 64; ++i) {
        const i32 bx = figure_bowed[2 * i], by = figure_bowed[2 * i + 1];
        const i32 sx = figure_singing[2 * i], sy = figure_singing[2 * i + 1];
        i32 x = (bx + (((sx - bx) * t) >> 15)) * 256, y = (by + (((sy - by) * t) >> 15)) * 256;   // quarter px to Q10
        const i32 outward = x < 0 ? -1 : 1;
        // beat: nine waves round the outline, pushing it in and out.
        x += outward * ((sine(i * 0x09000000u + tremble * 0x20000000u) * shake_q10) >> 15);
        // bed: the recording's peaks along the robe, scrolling as it plays.
        if (sampled) x += outward * ((i32(overview.peaks[(i + step * 3) & 63]) - mean) * fray_q10 / widest);
        if (held && i < hem_points) y += sine(step * 0x20000000u + i * 0x18000000u) * 6 * 256 / 32767;
        frame.paths[apparition].points[i].x = frame.paths[twin].points[i].x = clamp(x, -63 * unit, 63 * unit);
        frame.paths[apparition].points[i].y = frame.paths[twin].points[i].y = y;
    }
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
void shape_mouth(Frame& frame, u32 voice) {
    const u32 key = ((voice + 1024) >> 11) + 7;
    if (frame.paths[mouth].key == key) return;
    const Mouth m = mouth_for(voice);
    for (u32 i = 0; i < 64; ++i) {
        frame.paths[mouth].points[i].x = (cosine(i << 26) * m.rx * unit / 10) >> 15;
        frame.paths[mouth].points[i].y = m.cy + ((sine(i << 26) * m.ry * unit / 10) >> 15);
    }
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
        for (u32 i = 0; i < 64; ++i) {
            p.points[i].x = (at.x - path_x * unit) / 2 + ((cosine(i << 26) * 7 * unit) >> 16);
            p.points[i].y = (at.y - path_y * unit) / 2 + ((sine(i << 26) * 2 * unit) >> 16);
        }
    } else {
        // A ribbon along the arc: out along its front edge, back along its rear.
        for (u32 i = 0; i < 32; ++i) {
            const i32 a = i32(i * 2048) - 32768 + 1024;                       // -1..1 of a half turn, Q15
            const u32 angle = u32(a) * 65536u;
            // Spread, a start angle that place() narrows by orbit; circling, a
            // circled angle, near swinging with the side as it does.
            const Spot s = spot(orbit16 > 16384 ? place(0, angle, orbit16, near16) : place(angle, 0, orbit16, near16));
            const i32 x = (s.x - path_x * unit) / 2, y = (s.y - path_y * unit) / 2;
            p.points[i].x = x;
            p.points[i].y = y - unit / 4;
            p.points[63 - i].x = x;
            p.points[63 - i].y = y + unit / 4;
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
    shape_figure(frame, sung, beat, tremble, bed, overview, state.step, moving);
    shape_mouth(frame, sung);
    shape_floor(frame, orbit, near);
    const u32 presence = state.presence >> 12;                                 // 0..16
    i32 shade = 3 + i32(presence * 9 / 16) - i32(at.dim);
    shade = clamp(shade, 1, 15);

    // split: the pane's two faces show two apparitions, one at 0 and side by
    // side at the full split, each with its own mouth and halos; trade leans
    // them toward each other and swings them through each other.
    const i32 apart = ((i32(split >> 1) * cosine(state.trade)) >> 15) * 72 / 32 * state.scale >> 10;   // Q10 px, up to 72
    const i32 half_apart = apart / 2, reach = (half_apart < 0 ? -half_apart : half_apart) + 22 * state.scale;
    const i32 centre = clamp(state.x, 64 * unit + reach, 416 * unit - reach);   // both stay on the stage
    const bool twinned = apart > unit / 2 || apart < -unit / 2;
    const i32 lean = twinned ? i32(trade * 200 >> 16) * (apart > 0 ? 1 : -1) : 0;   // tenths of a degree
    const i32 first_x = centre - half_apart, second_x = centre + half_apart;
    place(frame, apparition, first_x, state.y, state.scale, u32(shade), stroke);
    turn(frame, apparition, rotation(lean));
    place(frame, mouth, first_x, state.y, state.scale, u32(shade), stroke);
    turn(frame, mouth, rotation(lean));
    const i32 twin_shade = shade > 2 ? shade - 1 : 1;
    if (twinned) {
        const Turn back = rotation(-lean);
        place(frame, twin, second_x, state.y, state.scale, u32(twin_shade), stroke);
        turn(frame, twin, back);
        // Its mouth rides the turn: the face is cy above the feet.
        const Mouth m = mouth_for(sung);
        const i32 cy = m.cy * state.scale >> 10;
        place(frame, twin_mouth, second_x - ((back.sin * cy) >> 10), state.y + ((back.cos * cy) >> 10),
              (state.scale * (m.rx + m.ry)) / 65, u32(twin_shade), stroke);
    } else { hide(frame, twin); hide(frame, twin_mouth); }

    // ring: halos round each, none at 0 to four at the longest; they breathe
    // at beat's rate while held.
    const u32 halos = (ring * 4 + 32768) >> 16;
    const i32 breath = beat_rate && moving ? (sine(beat_phase & 0xe0000000u) * 51) >> 15 : 0;   // +-5%, Q10
    for (u32 k = 0; k < halo_count; ++k) {
        if (k >= halos) { hide(frame, halo_first + k); hide(frame, twin_halo_first + k); continue; }
        const i32 size = clamp((state.scale * (635 + 205 * i32(k) + breath)) >> 10, unit / 4, 2 * unit);
        const i32 halo_shade = shade - 1 - i32(k), y = state.y - 34 * state.scale;   // round the upper body
        place(frame, halo_first + k, first_x, y, size, u32(halo_shade < 1 ? 1 : halo_shade), stroke);
        if (twinned) place(frame, twin_halo_first + k, second_x, y, size, u32(halo_shade < 2 ? 1 : halo_shade - 1), stroke);
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

    // bed asks for a sample and none is loaded: the hint shows in the doorway.
    if (bed && !overview.assigned) hide(frame, hint_cover);
    else place(frame, hint_cover, 0, 0, unit, 0, fill_and_stroke);
}
}
