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
    for (u32 i = 0; i < 64; ++i) overview.peaks[i] = opxy::u8(60 + (i * 53 % 29) * 4 + ((i / 8) % 2) * 50);
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
        // The apparition's shape alone, all over: pose, tremble, fray and scroll.
        for (int r = 0; r < 200000; ++r) {
            const u32 voice = next() & 0xffff, beat = next() & 0xffff, bed = next() & 0xffff;
            const u32 tremble = next() & 7, step = next() & 63;
            frame.paths[ghost::apparition].key = 0;
            ghost::shape_figure(frame, voice, beat, tremble, bed, overview, step, (next() & 3) != 0);
            ++frames_checked;
            check_path(ghost::apparition, sampled ? "shape, sampled" : "shape");
        }
    }
    std::printf("screen: %ld frames; sharpest turn %.2f degrees from straight back; %ld faults\n", frames_checked, sharpest, failures);
    return failures ? 1 : 0;
}
