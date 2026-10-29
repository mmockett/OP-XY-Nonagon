// Butterfly's screen checks, run by sdk.py test: every frame of three
// scripted sessions is valid, the idle screen is still, and churn (nodes
// whose transform or paint changed, outlines re-keyed) stays within budget.
// With arguments it plays one scenario and prints frames as JSON lines for
// tools/frame2svg.py:
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tests/screen.cpp -o screen
//   ./screen <session|idle|still|gif> [fps] [every]   (every: print one frame in that many)
#include "../display.cpp"
#include "../tables_data.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace butterfly;
opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }

// A control eased from one value to another over a span, as a hand turns it.
struct Turn { double from_t, to_t; int control; double from, to; };
struct Key { double at; int down; };

void dump(const Frame& f, double t) {
    std::printf("{\"t\":%.3f,\"nodes\":[", t);
    for (unsigned i = 0; i < Frame::node_count; ++i) {
        const opxy::draw::Node& n = f.nodes[i];
        std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint));
    }
    std::printf("],\"paths\":[");
    for (unsigned p = 0; p < Frame::path_count; ++p) {
        std::printf("%s[", p ? "," : "");
        for (unsigned i = 0; i < Frame::point_count; ++i) std::printf("%s%d,%d", i ? "," : "", f.paths[p].points[i].x, f.paths[p].points[i].y);
        std::printf("]");
    }
    std::printf("],\"texts\":[");
    for (unsigned i = 0; i < Frame::text_count; ++i) {
        const opxy::draw::Text& x = f.texts[i];
        std::printf("%s[%d,%d,%d,\"%s\"]", i ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::printf("]}\n");
}

struct Result { double mean, peak, idle_churn; bool valid; };

Result run(const char* scenario, double fps, int every, bool quiet) {
    double controls[8] = {0.764, 0.5, 0.75, 0.9, 0.844, 0.25, 0.4, 0.15};   // Specimen
    std::vector<Turn> turns;
    std::vector<Key> keys;
    double length = 3;
    bool still = false;
    if (!std::strcmp(scenario, "session") || !std::strcmp(scenario, "still")) {
        still = !std::strcmp(scenario, "still");
        length = 24;
        keys = {{0.5, 1}, {2.5, 0}, {6.0, 1}, {9.0, 0}, {10.0, 1}, {10.05, 1}, {10.1, 1}, {12.5, 0}, {12.5, 0}, {12.5, 0},
                {15.0, 1}, {17.0, 0}, {19.5, 1}, {22.5, 0}};
        turns = {{3.0, 5.0, FLUX, 0.764, 0.9},            // flux across a few sub-bands
                 {6.5, 8.5, COUPLING, 0.4, 0.8},          // lambda 0.85 to 1.6, while a note rings
                 {9.2, 9.8, SPREAD, 0.75, 1.0},
                 {13.0, 14.5, SITE, 0.25, 0.9},           // the strike out towards the chain's end
                 {14.6, 14.9, STRIKE, 0.9, 0.0},          // a pad
                 {15.0, 17.0, WINGBEAT, 0.15, 0.9},       // a deep wingbeat while a pad is held
                 {17.5, 19.0, FLUX, 0.9, 0.62},          // flux through fractions
                 {19.1, 19.3, GLOW, 0.844, 0.5},
                 {20.0, 21.0, TILT, 0.4, 0.9}};
    }
    if (!std::strcmp(scenario, "gif")) {   // docs/butterfly.gif: every control in turn, notes held under them
        length = 17;
        keys = {{0.3, 1}, {4.0, 0}, {4.3, 1}, {8.2, 0}, {8.5, 1}, {12.4, 0}, {12.7, 1}, {16.8, 0}};
        turns = {{1.0, 2.6, FLUX, 0.764, 0.92},          // flux reshapes the margin
                 {2.8, 3.8, SPREAD, 0.75, 0.3},          // spread: the wingspan
                 {4.6, 6.2, COUPLING, 0.45, 0.85},       // coupling past 1: even bands into eyespots
                 {6.4, 7.2, SITE, 0.25, 0.85},           // site: the lit band moves out
                 {7.3, 8.0, GLOW, 0.844, 0.2},           // glow: the tails draw in
                 {8.6, 9.8, TILT, 0.4, 1.0},             // tilt: the forewing draws in
                 {10.0, 10.8, STRIKE, 0.9, 0.0},         // strike: the antennae fold; a pad
                 {11.0, 12.2, SPREAD, 0.3, 0.75},
                 {11.0, 12.2, SITE, 0.85, 0.25},
                 {11.0, 12.2, GLOW, 0.2, 0.844},
                 {11.0, 12.2, TILT, 1.0, 0.4},
                 {11.0, 12.2, COUPLING, 0.85, 0.45},
                 {11.0, 12.2, FLUX, 0.92, 0.764},
                 {12.4, 12.6, WINGBEAT, 0.15, 1.0}};     // wingbeat: the wings breathe under a pad
    }
    static DisplayState state;
    static Frame frame, last;
    display_reset(state);
    opxy::draw::Input input = {};
    input.track = 1;
    input.reduced_motion = still ? 1 : 0;
    for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    const int frames = static_cast<int>(length * fps);
    const opxy::i32 tick = static_cast<opxy::i32>(std::lround(1024 / fps));
    size_t next = 0;
    int held = 0;
    double total = 0, worst = 0, worst_at = 0, idle = 0;
    for (int n = 0; n < frames; ++n) {
        const double t = n / fps;
        while (next < keys.size() && keys[next].at <= t) {
            if (keys[next].down) {
                ++held;
                ++input.note_serial;
            } else {
                --held;
                ++input.note_off_serial;
            }
            ++next;
        }
        for (const Turn& u : turns)
            if (t >= u.from_t && t <= u.to_t + 0.5 / fps) {
                const double x = std::min(1.0, (t - u.from_t) / (u.to_t - u.from_t));
                controls[u.control] = u.from + (u.to - u.from) * (0.5 - 0.5 * std::cos(3.14159265 * x));
            }
        input.held_notes = static_cast<opxy::u32>(held);
        input.elapsed = n ? tick : 0;
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
        for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
        display(state, input, frame);
        if (!opxy::draw::valid(frame)) {
            std::printf("FAIL invalid frame in %s at %.3f s\n", scenario, t);
            return Result{0, 0, 0, false};
        }
        int churn = 0;
        if (n) {
            for (unsigned i = 0; i < Frame::node_count; ++i) {
                const opxy::draw::Node &a = frame.nodes[i], &b = last.nodes[i];
                if (a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade ||
                    a.paint != b.paint)
                    ++churn;
                else if (i < Frame::path_count && frame.paths[i].key != last.paths[i].key)
                    ++churn;
            }
            total += churn;
            if (churn > worst) {
                worst = churn;
                worst_at = t;
            }
            if (!std::strcmp(scenario, "idle")) idle += churn;
        }
        last = frame;
        if (every > 0 && n % every == 0) dump(frame, t);
    }
    const Result r = {total / (frames - 1), worst, idle, true};
    if (!quiet)
        std::fprintf(stderr, "%s at %.0f fps: %d frames, churn mean %.2f, peak %.0f at %.2f s\n", scenario, fps, frames, r.mean,
                     worst, worst_at);
    return r;
}

// docs/controls.png: each control at 0, 25, 50, 75 and 100 with the others at
// Specimen's, 0.6 s into a note struck at 0.1 s; one frame each, in order.
void controls_sheet() {
    static DisplayState state;
    static Frame frame;
    const double fps = 60, length = 0.7, note = 0.1;
    const opxy::i32 tick = static_cast<opxy::i32>(std::lround(1024 / fps));
    for (int c = 0; c < 8; ++c)
        for (int v = 0; v <= 4; ++v) {
            double controls[8] = {0.764, 0.5, 0.75, 0.9, 0.844, 0.25, 0.4, 0.15};
            controls[c] = v / 4.0;
            display_reset(state);
            opxy::draw::Input input = {};
            input.track = 1;
            for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
            const int frames = static_cast<int>(length * fps);
            for (int n = 0; n <= frames; ++n) {
                const double t = n / fps;
                if (t >= note && !input.held_notes) {
                    input.held_notes = 1;
                    ++input.note_serial;
                }
                input.elapsed = n ? tick : 0;
                for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
                for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
                display(state, input, frame);
            }
            dump(frame, c + v / 4.0);
        }
}
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "controls")) {
        controls_sheet();
        return 0;
    }
    if (argc > 1) {
        const Result r = run(argv[1], argc > 2 ? std::atof(argv[2]) : 60, argc > 3 ? std::atoi(argv[3]) : 0, false);
        return r.valid ? 0 : 1;
    }
    bool ok = true;
    const Result idle = run("idle", 60, 0, true);
    std::printf("%s  the idle screen is still: %.0f nodes change after the first frame\n", idle.valid && idle.idle_churn == 0 ? "ok  " : "FAIL",
                idle.idle_churn);
    ok = ok && idle.valid && idle.idle_churn == 0;
    for (const char* scenario : {"session", "still"})
        for (double fps : {60.0, 30.0}) {
            const Result r = run(scenario, fps, 0, true);
            const bool pass = r.valid && r.mean <= 20 && r.peak <= 30;
            std::printf("%s  %s at %.0f fps: churn %.2f a frame on average, %.0f at most\n", pass ? "ok  " : "FAIL", scenario, fps,
                        r.mean, r.peak);
            ok = ok && pass;
        }
    std::printf(ok ? "all screen checks passed\n" : "screen checks failed\n");
    return ok ? 0 : 1;
}
