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

// Whether an outline turns back on itself at b, to within about 3 degrees: the
// device's stroke can draw a turn that sharp as a long spike, and 2.5 degrees is
// the sharpest seen to draw well. Each edge is brought down on its own to keep
// its products in 32 bits, which leaves the angle as it was.
inline bool turns_back(const Point& a, const Point& b, const Point& c) {
    i32 ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
    while (ux > 4095 || ux < -4095 || uy > 4095 || uy < -4095) { ux /= 2; uy /= 2; }
    while (vx > 4095 || vx < -4095 || vy > 4095 || vy < -4095) { vx /= 2; vy /= 2; }
    const i32 cross = ux * vy - uy * vx, dot = ux * vx + uy * vy;
    return dot < 0 && (cross < 0 ? -cross : cross) * 20 < -dot;
}

// The overview bin (0..96) a frame falls in, without a 64-bit product.
u32 bin_of(u32 at, u32 frames) {
    if (!frames) return 0;
    const u32 bin = frames < (1u << 25) ? at * overview_bins / frames : at / (frames / overview_bins);
    return bin < overview_bins ? bin : overview_bins;
}
// The bins the bed plays: those between the start and end markers.
void played_bins(const SampleOverview& o, u32& first, u32& count) {
    const u32 end = o.end && o.end <= o.frames ? o.end : o.frames, begin = o.begin < end ? o.begin : 0;
    first = bin_of(begin, o.frames);
    if (first >= overview_bins) first = overview_bins - 1;
    const u32 last = end ? bin_of(end - 1, o.frames) + 1 : 1;
    count = last > first ? last - first : 1;
}

// The robe's 64 peaks from the overview's min/max bins between the markers,
// the part of the sample the bed plays (all 96 on an untrimmed sample): each
// one's largest magnitude, 0..255.
i32 peak_of(const SampleOverview& o, u32 column, u32 first, u32 count) {
    i32 most = 0;
    const u32 b0 = first + column * count / 64;
    u32 b1 = first + (column + 1) * count / 64;
    if (b1 <= b0) b1 = b0 + 1;
    for (u32 b = b0; b < b1; ++b) {
        const i32 low = o.low[b], high = o.high[b];
        const i32 size = high > -low ? high : -low;
        if (size > most) most = size;
    }
    const i32 scaled = most >> 7;
    return scaled > 255 ? 255 : scaled;
}

// The apparition and its twin: posed by voice (bowed at breath, singing at
// ee); trembling all round by beat, the tremble running at beat's rate while
// held; its robe frayed as bed rises, into the sample's own waveform or,
// without one, the whisper's ragged gusts, scrolling while held; its hem
// rippling while held.
void shape_figure(Frame& frame, u32 voice, u32 beat, u32 tremble, u32 bed, const SampleOverview& overview, u32 step, bool held) {
    const u32 pose = (voice + 1024) >> 11, shake = (beat + 1024) >> 11, fray = (bed + 1024) >> 11;   // 33 steps each
    const bool sampled = fray && overview.assigned && overview.loaded;
    u32 first = 0, count = overview_bins;
    if (sampled) played_bins(overview, first, count);
    const u32 peaks_key = sampled ? overview.generation * 2654435761u ^ (first << 8 | count) : 0;
    const u32 key = hash((pose | shake << 6 | tremble << 12 | fray << 16 | (held ? step : 0) << 22 | u32(held) << 30) ^ peaks_key) | 1;
    if (frame.paths[apparition].key == key) return;
    // The bed's peaks about their mean, scaled so the widest reaches the fray;
    // without a sample, gusts of 0..31 about 16.
    i32 mean = 16, widest = 16;
    i32 peaks[64];
    if (sampled) {
        mean = 0; widest = 1;
        for (u32 i = 0; i < 64; ++i) { peaks[i] = peak_of(overview, i, first, count); mean += peaks[i]; }
        mean >>= 6;
        for (u32 i = 0; i < 64; ++i) widest = widest > peaks[i] - mean ? (widest > mean - peaks[i] ? widest : mean - peaks[i]) : peaks[i] - mean;
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
                const i32 peak = sampled ? peaks[at] : i32((hash(at * 0x9e3779b9u + 7) >> 27) + (hash(((at + 1) & 63) * 0x9e3779b9u + 7) >> 27)) >> 1;
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
    // Where pushes bring a point within an eighth of a screen pixel of the last
    // (7/32 px here, since the figure draws as small as 0.6 scale), or
    // make the outline turn back on itself at it, it and its neighbours return
    // to their places in the pose, whose points are all a pixel or more apart
    // and whose turns are all wide. Each pass returns at least one point, so
    // this ends clean.
    Point* p = frame.paths[apparition].points;
    for (u32 pass = 0; pass < 64; ++pass) {
        bool moved = false;
        for (u32 i = 0; i < 64; ++i) {
            const u32 before = (i + 63) & 63, after = (i + 1) & 63;
            const Point& a = p[before], &b = p[i], &c = p[after];
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            const i32 apart = unit * 7 / 32;
            const bool merged = dx > -apart && dx < apart && dy > -apart && dy < apart;
            if (!merged && !turns_back(a, b, c)) continue;
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

// --- sample view (to be shared) ---
// A sample engine's own view of its sample, as a sampler draws one: a column
// for each of SampleOverview's 96 bins, from the bin's smallest value to its
// largest (both signs), on a faint centre line, lit within the trim and dim
// outside it; the trim's start and end markers; and, while Shift is held, a
// hint that Shift and the Sample key swap the sample. Only SampleOverview and
// the frame API, in 32-bit arithmetic. Each machine's tools/make_panel.py
// authors what it places:
// - the centre line, a node: a thin ribbon as long as the slot, about its middle;
// - the 96 columns, the nodes after it: each a rectangle with a fill,
//   4.8 px wide and twice sample_column_half tall, about its centre,
//   closing over a long side. Scaled to a column 2 px tall it is still 0.15 px
//   wide (its points stay an eighth of a pixel apart on the device); to one
//   16 px tall, 1.2 px;
// - the start and end markers, two nodes with a fill, about the slot's centre line;
// - the hint: sample_hint_nodes nodes, the Sample key's icon (Grains': its
//   outline, its circle and its bars),
//   authored with their own paint about the hint's top left corner, and two
//   text slots, "swap" (10 px, #CBCBCB) and "shift +" (10 px, #7D7D7D), the
//   empty state's greys.
const u32 sample_columns = 96;            // overview_bins
const i32 sample_column_half = 32;        // px, as authored
const u32 sample_hint_nodes = 3;
const i32 sample_hint_width = 52, sample_hint_height = 18;   // px, from its top left corner

// Where a machine puts its sample: Q10 px and palette shades.
struct SampleSlot {
    i32 x0, width;      // the slot's left end and width (the whole sample)
    i32 y, half;        // its centre line, and the furthest a column reaches from it
    u32 lit, dim, line; // columns within the trim, outside it; the centre line
};

u32 sample_mix(u32 h, u32 v) {
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}
u32 sample_root(u32 n) {
    u32 r = 0;
    for (u32 bit = 1u << 15; bit; bit >>= 1)
        if ((r + bit) * (r + bit) <= n) r += bit;
    return r;
}
// at / frames of span (Q10, under 2^21), without a 64-bit product.
i32 sample_along(i32 span, u32 at, u32 frames) {
    while (frames >= 65536) { at >>= 1; frames >>= 1; }
    if (!frames) return 0;
    if (at > frames) at = frames;
    return i32((u32(span >> 4) * at / frames) << 4);
}
// How far a bin's value v (-32768..32767) reaches from the centre line, Q10
// px, up for positive values: as the root of |v| against the loudest bin (so
// quiet stretches still show), the loudest reaching 85% of the way at a
// gain of 0, scaled by louder (Q10), never past half.
i32 sample_reach(i32 v, i32 loudest, i32 half, i32 louder) {
    const u32 size = u32(v < 0 ? -v : v);
    const u32 r = sample_root(size * 65536u / u32(loudest));   // 0..256
    i32 h = i32(r) * (half * 87 / 102) >> 8;
    h = (h * louder) >> 10;
    if (h > half) h = half;
    return v < 0 ? -h : h;
}

// The sample in its slot: the centre line (node line) and a column for each
// bin (the 96 nodes after it), redrawn only when the sample, its loading, its
// trim or its gain changes (key holds what was last drawn), at most budget
// columns a frame, from the left (every one at once with snap). The gain is
// stock's steps, -30..+20: a column is twice as tall each 10 steps up, within
// the slot. A bin too quiet for a column 2 px tall leaves the centre line.
// With no sample assigned, all of it goes.
template <class F>
void place_waveform(F& frame, u32 line, const SampleOverview& o, const SampleSlot& s, u32& key, bool snap, u32 budget) {
    if (!o.assigned) {
        for (u32 n = line; n <= line + sample_columns; ++n) hide(frame, n);
        key = 0;
        return;
    }
    const i32 gain = o.gain == gain_unavailable ? 0 : clamp(o.gain, -30, 20);
    const u32 frames = o.frames ? o.frames : 1;
    const u32 end = o.end && o.end <= frames ? o.end : frames, begin = o.begin < end ? o.begin : 0;
    const u32 want = sample_mix(sample_mix(sample_mix(sample_mix(o.generation, o.loaded), u32(gain + 30)), begin), end) | 1;
    if (key == want) return;
    place(frame, line, s.x0 + s.width / 2, s.y, unit, s.line, fill_and_stroke);
    // 2^(gain / 10), Q10: whole doublings, and the rest of one linearly (near enough to see).
    const i32 g = gain + 30, louder = (128 << (g / 10)) * (10 + g % 10) / 10;   // 128 at -30 .. 4096 at +20
    i32 loudest = 1;
    if (o.loaded)
        for (u32 b = 0; b < sample_columns; ++b) {
            const i32 high = o.high[b], low = -i32(o.low[b]);
            loudest = high > loudest ? high : loudest;
            loudest = low > loudest ? low : loudest;
        }
    bool done = true;
    for (u32 b = 0; b < sample_columns; ++b) {
        const u32 node = line + 1 + b;
        const i32 top = s.y - (o.loaded ? sample_reach(o.high[b], loudest, s.half, louder) : 0);
        const i32 bottom = s.y - (o.loaded ? sample_reach(o.low[b], loudest, s.half, louder) : 0);
        const Node& n = frame.nodes[node];
        i32 x = hidden_x * unit, y = hidden_y * unit, scale = unit;
        u32 shade = 0;
        Paint paint = keep;
        if (bottom - top >= 2 * unit) {
            // Its middle within the trim: lit.
            const u32 middle = frames / sample_columns * b + frames / sample_columns / 2;
            x = s.x0 + s.width * i32(2 * b + 1) / i32(2 * sample_columns);
            y = (top + bottom) / 2;
            scale = (bottom - top) / (2 * sample_column_half);
            shade = middle >= begin && middle < end ? s.lit : s.dim;
            paint = fill_and_stroke;
        }
        if (n.x == x && n.y == y && n.scale == scale && n.cos == unit && n.sin == 0 && (paint == keep || (n.shade == shade && n.paint == paint)))
            continue;
        if (!snap) {
            if (!budget) { done = false; continue; }
            --budget;
        }
        place(frame, node, x, y, scale, shade, paint);
    }
    if (done) key = want;
}

// The trim's start and end markers (nodes first and first + 1), on the
// sample's start and end within the slot, in shade.
template <class F>
void place_trim_marks(F& frame, u32 first, const SampleOverview& o, const SampleSlot& s, u32 shade) {
    const u32 frames = o.frames ? o.frames : 1;
    const u32 end = o.end && o.end <= frames ? o.end : frames, begin = o.begin < end ? o.begin : 0;
    place(frame, first, s.x0 + sample_along(s.width, begin, frames), s.y, unit, shade, fill_and_stroke);
    place(frame, first + 1, s.x0 + sample_along(s.width, end, frames), s.y, unit, shade, fill_and_stroke);
}

// While shown (Shift held, with a sample), the hint that Shift and the Sample
// key swap the sample, its top left corner at (x, y), Q10 px: "swap" over
// "shift +", and the key's icon beside both (nodes first..first + 2, text
// slots text and text + 1). Otherwise it waits off screen, so it changes only
// as Shift goes down and up.
template <class F>
void place_swap_hint(F& frame, u32 first, u32 text_slot, i32 x, i32 y, bool shown) {
    for (u32 k = 0; k < sample_hint_nodes; ++k) {
        if (shown) place(frame, first + k, x, y, unit, 0, keep);
        else hide(frame, first + k);
    }
    text(frame, text_slot, shown ? x : hidden_x * unit, shown ? y + 15 * unit / 2 : hidden_y * unit, unit, shown ? "swap" : "");
    text(frame, text_slot + 1, shown ? x : hidden_x * unit, shown ? y + 18 * unit : hidden_y * unit, unit, shown ? "shift +" : "");
}
// --- end of sample view ---
}

// The sample in the slot under the apron, where the hint is with none
// (README, "Screen"): the sample view's columns, lit within the trim and dim
// outside it, at most 12 changing a frame while the sample, its trim or its
// gain changes; the trim's markers, lit while Shift edits them; and while
// Shift is held, the swap hint beside the slot's right end.
void place_strip(DisplayState& state, Frame& frame, const SampleOverview& o, bool shift, bool snap) {
    const SampleSlot slot = {strip_x0 * unit, (strip_x1 - strip_x0) * unit, strip_y * unit, 8 * unit, 9, 4, 3};
    place_waveform(frame, sample_line, o, slot, state.wave_key, snap, 12);
    if (o.assigned) place_trim_marks(frame, mark_first, o, slot, shift ? 15 : 8);
    else { hide(frame, mark_first); hide(frame, mark_first + 1); }
    place_swap_hint(frame, swap_first, swap_text, swap_x * unit, swap_y * unit, shift && o.assigned);
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
    state.wave_key = 0;
    state.drawn = 0;
    state.swell = Swell{0, 0, 0};
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const SampleOverview& overview = *opxy_sample_overview();
    char values[8][header_capacity] = {};
    captions::fill(input, overview.assigned != 0, values);
    const opxy::u8 pages[2][4] = {{kRing, kVoice, kSplit, kTrade}, {kBeat, kOrbit, kNear, kBed}};
    const char* own[8];
    for (u32 i = 0; i < 8; ++i) own[i] = captions::titles[i];
    captions::shift_row(input, pages, overview, own, values);
    const char* const titles[8] = {own[0], own[1], own[2], own[3], own[4], own[5], own[6], own[7]};
    page_header(state.header, input, frame, titles, values, header_first, pages);

    if (state.track != input.track) {
        const PageHeader header = state.header;
        display_reset(state);
        state.header = header;
        state.track = input.track;
    }
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const bool still = input.reduced_motion != 0;
    const u32 ms = u32(clamp(input.elapsed, 0, 102)) * 1000 >> 10;
    // With the track's amp envelope known, the figure follows it: it swells in
    // over the attack, holds at the sustain and fades over the release, still
    // circling while it sounds. Unknown, it eases in and fades over ring's time.
    follow(state.swell, input.amp, held, events.hold_started, still, u32(clamp(input.elapsed, 0, 256)) * 15625u >> 4);
    const bool enveloped = envelope_known(input.amp) && !still;
    const bool moving = (held || (enveloped && state.swell.level)) && !still;
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
    else if (enveloped) state.presence = state.swell.level;
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
    place_strip(state, frame, overview, input.shift != 0, !state.drawn || still);
    state.drawn = 1;

}
}
