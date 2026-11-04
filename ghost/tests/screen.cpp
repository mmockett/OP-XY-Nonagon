// Ghost's screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Every control through 0..127 with a
// note held, random settings with several notes, and the apparition's own shape
// over pose, tremble, fray and scroll, with a sample and without: every frame
// valid, and every shown outline as the OP-XY draws one well. Neighbouring
// points at least an eighth of a pixel apart, a closing edge of at least a
// quarter, and no turn sharper than 2.5 degrees from straight back: the device's
// stroke can draw a sharper one as a long spike, and the browser's preview
// doesn't show it.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
opxy::draw::SampleOverview overview;
}
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

long failures = 0, frames_checked = 0;
double sharpest = 180;
ghost::DisplayState state;
ghost::Frame frame;

void fail(const char* what, const char* when, u32 p, u32 at) {
    if (failures++ < 12) std::printf("FAIL %s: path %u %s (point %u)\n", when, p, what, at);
}
void check_path(u32 p, const char* when) {
    Point q[64];
    u32 m = 0;
    for (u32 i = 0; i < 64; ++i) {
        const Point& r = frame.paths[p].points[i];
        if (!m || r.x != q[m - 1].x || r.y != q[m - 1].y) q[m++] = r;   // a repeated last point only pads
    }
    for (u32 i = 0; i < m; ++i) {
        const Point& a = q[i], &b = q[(i + 1) % m], &c = q[(i + 2) % m];
        const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
        if (i + 1 < m && std::fabs(ux) < unit / 8 && std::fabs(uy) < unit / 8) fail("has points within 1/8 px", when, p, i);
        if (i + 1 == m && std::hypot(ux, uy) < unit / 4) fail("closes over less than 1/4 px", when, p, i);
        const double dot = ux * vx + uy * vy, cross = std::fabs(ux * vy - uy * vx);
        if (dot < 0) {
            const double angle = std::atan2(cross, -dot) * 180 / 3.14159265358979;
            if (angle < sharpest) sharpest = angle;
            if (angle < 2.5) fail("turns within 2.5 degrees of straight back", when, p, (i + 1) % m);
        }
    }
}
void check_frame(const char* when) {
    ++frames_checked;
    if (!valid(frame)) fail("the frame is not valid", when, 0, 0);
    for (u32 p = 0; p < ghost::Frame::path_count; ++p)
        if (frame.nodes[p].x < hidden_x * unit) check_path(p, when);
}
void load(bool sampled) {
    std::memset(&overview, 0, sizeof overview);
    if (!sampled) return;
    overview.assigned = overview.loaded = 1;
    overview.frames = 264600;
    overview.rate = 44100;
    overview.end = 264600;
    overview.generation = 1;
    for (u32 i = 0; i < 96; ++i) { const opxy::i16 peak = opxy::i16((60 + (i * 53 % 29) * 4 + ((i / 8) % 2) * 50) << 7); overview.high[i] = peak; overview.low[i] = opxy::i16(-peak); }
}
i32 q31(double v) { return v >= 1 ? 0x7fffffff : i32(v * 2147483647.0); }
void play(const double* c, u32 notes, int frames, const char* when) {
    std::memset(&frame, 0, sizeof frame);
    Input input;
    std::memset(&input, 0, sizeof input);
    ghost::display_reset(state);
    input.track = 1;
    input.elapsed = 17;
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.shown[k] = opxy::u8(k); input.parameters[k] = input.slots[k]; }
    ghost::display(state, input, frame);
    check_frame(when);
    input.note_serial += notes;
    input.held_notes = notes;
    for (int t = 0; t < frames; ++t) {
        ghost::display(state, input, frame);
        check_frame(when);
    }
    input.note_off_serial += notes;
    input.held_notes = 0;
    for (int t = 0; t < 30; ++t) {
        ghost::display(state, input, frame);
        check_frame(when);
    }
}
u32 seed = 0x2545f491u;
u32 next() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
}

int main() {
    const double standard[8] = {0.6, 0.6, 0.6, 0.4, 0.2, 0.2, 0.4, 0.5};
    const char* const names[8] = {"ring", "voice", "split", "trade", "beat", "orbit", "near", "bed"};
    for (int sampled = 0; sampled < 2; ++sampled) {
        load(sampled != 0);
        for (int k = 0; k < 8; ++k)
            for (int v = 0; v < 128; ++v) {
                double c[8];
                std::memcpy(c, standard, sizeof c);
                c[k] = v / 127.0;
                char when[48];
                std::snprintf(when, sizeof when, "%s at %d%s", names[k], v, sampled ? ", sampled" : "");
                play(c, 1, 90, when);
            }
        for (int r = 0; r < 300; ++r) {
            double c[8];
            for (int k = 0; k < 8; ++k) {
                const u32 x = next() % 6;
                c[k] = x == 0 ? 0 : x == 1 ? 1 : (next() % 128) / 127.0;
            }
            char when[48];
            std::snprintf(when, sizeof when, "random %d%s", r, sampled ? ", sampled" : "");
            play(c, 1 + r % 3, 90, when);
        }
        // The apparition's shape alone, all over: pose, tremble, fray and scroll,
        // and with a sample, its fray from any trim of it (Shift's start and end).
        for (int r = 0; r < 200000; ++r) {
            const u32 voice = next() & 0xffff, beat = next() & 0xffff, bed = next() & 0xffff;
            const u32 tremble = next() & 7, step = next() & 63;
            if (sampled) {
                const u32 a = next() % overview.frames, b = next() % overview.frames;
                const bool whole = (r & 3) == 0;
                overview.begin = whole ? 0 : a < b ? a : b;
                overview.end = whole ? overview.frames : (a < b ? b : a) + 1;
            }
            frame.paths[ghost::apparition].key = 0;
            ghost::shape_figure(frame, voice, beat, tremble, bed, overview, step, (next() & 3) != 0);
            ++frames_checked;
            check_path(ghost::apparition, sampled ? "shape, sampled" : "shape");
        }
    }
    {   // The robe frays into the part of the sample the bed plays: a trim to one
        // stretch changes it, and Shift's row names start, window and end with their times.
        load(true);
        Point whole[64], trimmed[64];
        ghost::shape_figure(frame, 0x8000, 0, 0, 0xffff, overview, 0, false);
        for (u32 i = 0; i < 64; ++i) whole[i] = frame.paths[ghost::apparition].points[i];
        overview.begin = 0; overview.end = overview.frames / 8;
        ghost::shape_figure(frame, 0x8000, 0, 0, 0xffff, overview, 0, false);
        bool differs = false;
        for (u32 i = 0; i < 64; ++i) {
            trimmed[i] = frame.paths[ghost::apparition].points[i];
            differs = differs || trimmed[i].x != whole[i].x || trimmed[i].y != whole[i].y;
        }
        if (!differs) fail("a trim leaves the robe's fray as it was", "trim", 0, 0);
        overview.begin = 26460; overview.end = 158760;
        std::memset(&frame, 0, sizeof frame);
        Input input;
        std::memset(&input, 0, sizeof input);
        ghost::display_reset(state);
        input.track = 1;
        input.elapsed = 17;
        for (int k = 0; k < 4; ++k) input.shown[k] = opxy::u8(k);
        input.shift = 1;
        for (int t = 0; t < 20; ++t) ghost::display(state, input, frame);
        // Encoder 3 with Shift is the sample's gain (stockShift "gain", #400), in stock's steps.
        const char* expect[8] = {"start", "window", "gain", "end", "0.60s", "3.00s", "+0", "3.60s"};
        for (u32 t = 0; t < 8; ++t)
            if (std::strcmp(frame.texts[t].chars, expect[t]))
                fail("Shift's row", frame.texts[t].chars, 0, 0);
        if (std::strcmp(frame.header[2], "+0")) fail("Shift's header cell 3", frame.header[2], 0, 0);
        // The trim's markers stand on the start and end, lit while Shift is held.
        const double x0 = frame.nodes[ghost::mark_first].x / 1024.0, x1 = frame.nodes[ghost::mark_first + 1].x / 1024.0;
        const double width = ghost::strip_x1 - ghost::strip_x0;
        if (std::fabs(x0 - (ghost::strip_x0 + width * 26460 / overview.frames)) > 0.5 ||
            std::fabs(x1 - (ghost::strip_x0 + width * 158760 / overview.frames)) > 0.5 || frame.nodes[ghost::mark_first].shade != 15)
            fail("the trim's markers", "Shift", 0, 0);
        // Bars within the trim lit solid, those outside dim.
        u32 lit = 0, dark = 0;
        for (u32 i = 0; i < ghost::bar_count; ++i) (frame.nodes[ghost::bar_first + i].paint == opxy::draw::fill_and_stroke ? lit : dark) += 1;
        if (!lit || !dark) fail("the strip lights the trim", "bars", 0, 0);
        for (int k = 0; k < 4; ++k) input.shown[k] = opxy::u8(4 + k);
        for (int t = 0; t < 20; ++t) ghost::display(state, input, frame);
        if (std::strcmp(frame.texts[0].chars, "start") || std::strcmp(frame.texts[2].chars, "gain") || std::strcmp(frame.texts[3].chars, "end") ||
            std::strcmp(frame.texts[6].chars, "+0"))
            fail("page 2's Shift row", frame.texts[2].chars, 0, 0);
        // Every gain from -30 to +20 reads signed; the strip's bars grow with it.
        double last = 0;
        for (opxy::i32 g = -30; g <= 20; g += 10) {
            overview.gain = g;
            for (int t = 0; t < 20; ++t) ghost::display(state, input, frame);
            char want[8];
            std::snprintf(want, sizeof want, "%c%d", g < 0 ? '-' : '+', g < 0 ? -g : g);
            if (std::strcmp(frame.texts[6].chars, want)) fail("the gain's caption", frame.texts[6].chars, 0, 0);
            double total = 0;
            for (u32 i = 0; i < ghost::bar_count; ++i) total += frame.nodes[ghost::bar_first + i].scale;
            if (total <= last) fail("the strip grows with the gain", want, 0, 0);
            last = total;
        }
        // With no gain known (gain_unavailable), its cell shows nothing.
        overview.gain = opxy::draw::gain_unavailable;
        for (int t = 0; t < 20; ++t) ghost::display(state, input, frame);
        if (frame.texts[2].chars[0] || frame.texts[6].chars[0] || frame.header[2][0]) fail("an unknown gain shows", frame.texts[6].chars, 0, 0);
        overview.gain = 0;
        // With no sample, the strip goes and the hint shows.
        overview.assigned = 0;
        for (int t = 0; t < 2; ++t) ghost::display(state, input, frame);
        for (u32 n = ghost::bar_first; n < ghost::mark_first + 2; ++n)
            if (frame.nodes[n].x != opxy::draw::hidden_x * 1024) fail("the strip without a sample", "shown", 0, 0);
        overview.assigned = 1;
    }
    {   // The amp envelope (input.amp, SDK 1.10): the figure lasts as long as the
        // sound. Unknown, it fades over ring's time, as before. Stock reads a
        // larger release word as a shorter release: at its top (0x7f000000) the
        // figure goes at once, a smaller word keeps it longer, and a larger
        // attack word swells it in more slowly.
        load(true);
        static ghost::DisplayState st;
        static ghost::Frame fr;
        Input in;
        auto start = [&](const opxy::draw::Envelope& amp, u32 held_frames) {
            std::memset(&fr, 0, sizeof fr);
            std::memset(&in, 0, sizeof in);
            ghost::display_reset(st);
            in.track = 1; in.elapsed = 17; in.amp = amp;
            for (u32 i = 0; i < 4; ++i) in.shown[i] = opxy::u8(i);
            ghost::display(st, in, fr);
            in.note_serial = 1; in.held_notes = 1;
            for (u32 f = 0; f < held_frames; ++f) ghost::display(st, in, fr);
            in.held_notes = 0; in.note_off_serial = 1;
        };
        auto presence_after = [&](const opxy::draw::Envelope& amp, u32 held_frames, u32 after_frames) {
            start(amp, held_frames);
            for (u32 f = 0; f < after_frames; ++f) ghost::display(st, in, fr);
            return st.presence;
        };
        auto gone_after = [&](opxy::i32 release) {   // frames from the release until the figure has gone
            start(opxy::draw::Envelope{0, 0, 0x7fffffff, release}, 60);
            u32 f = 0;
            for (; f < 60 * 60 && (ghost::display(st, in, fr), st.presence != 0); ++f) {}
            return f;
        };
        const opxy::i32 unknown = opxy::draw::envelope_unavailable;
        const opxy::draw::Envelope none = {unknown, unknown, unknown, unknown}, quick = {0, 0, 0x7fffffff, 0x7f000000},
                                   long_release = {0, 0, 0x7fffffff, 0x20000000}, long_attack = {0x66666666, 0, 0x7fffffff, 0x7f000000};
        const u32 a = presence_after(none, 60, 180), b = presence_after(quick, 60, 30), c = presence_after(long_release, 60, 60);
        const u32 d = presence_after(quick, 20, 0), e = presence_after(long_attack, 20, 0);
        const u32 longer = gone_after(q31(0.3)), shorter = gone_after(q31(0.7));
        std::printf("envelope: presence 3 s after release %u (unknown, ring's fade); 1 s into release word 0.25 %u; 0.5 s after the shortest %u; "
                    "1/3 s into a fast and a slow attack %u and %u (of 65536); gone %u frames after release word 0.3, %u after 0.7\n",
                    a, c, b, d, e, longer, shorter);
        if (a != 0) fail("an unknown envelope fades over ring's time", "envelope", 0, 0);
        if (b != 0) fail("the shortest release lets the figure go", "envelope", 0, 0);
        if (c < 16384 || c > 49152) fail("a long release keeps the figure", "envelope", 0, 0);
        if (e * 2 > d) fail("a larger attack word swells the figure in more slowly", "envelope", 0, 0);
        if (shorter >= longer) fail("a larger release word ends the figure sooner", "envelope", 0, 0);
    }
    std::printf("screen: %ld frames; sharpest turn %.2f degrees from straight back; %ld faults\n", frames_checked, sharpest, failures);
    return failures ? 1 : 0;
}
