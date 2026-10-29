// Mach's screen checks, compiled for the host and run by sdk.py test: the
// display draws a valid frame at every step of four scripted sessions and of
// a flight at every control's setting (each swept in twentieths, also below
// Mach 1 and at Mach 3), every outline it reshapes is one the OP-XY draws
// cleanly, every header caption fits its column in the OP-XY's font, and its churn (nodes whose transform or paint changed, outlines
// re-keyed) stays within budget. With arguments it measures one scenario and
// prints chosen frames as JSON for tools/frame2svg.py:
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tests/screen.cpp -o screen
//   ./screen <flight|session|echo|subsonic|tour|gallery|still:<control>:<value>|sweep:<control>:<value>[:<mach>]> [fps] [snapshot seconds ...]
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }
static double mach_control(double m) { return m <= 1 ? (m - 0.3) / 1.4 : 0.5 + (m - 1) / 4; }

struct Step { double at; int key; int down; int control; double value; };

static void dump(const mach::Frame& f, double t) {
    std::printf("{\"t\":%.3f,\"nodes\":[", t);
    for (unsigned i = 0; i < mach::Frame::node_count; ++i) {
        const opxy::draw::Node& n = f.nodes[i];
        std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint));
    }
    std::printf("],\"paths\":[");
    for (unsigned p = 0; p < mach::Frame::path_count; ++p) {
        std::printf("%s[", p ? "," : "");
        for (unsigned i = 0; i < mach::Frame::point_count; ++i)
            std::printf("%s%d,%d", i ? "," : "", f.paths[p].points[i].x, f.paths[p].points[i].y);
        std::printf("]");
    }
    std::printf("],\"texts\":[");
    for (unsigned i = 0; i < mach::Frame::text_count; ++i) {
        const opxy::draw::Text& x = f.texts[i];
        std::printf("%s[%d,%d,%d,\"%s\"]", i ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::printf("]}\n");
}

struct Churn { double mean, flying, peak; bool valid; };

// What the OP-XY draws as streaks or slivers, beyond the SDK's own rules: a
// reshaping outline with no area, or turning straight back at a point (a line
// traced out and back); neighbouring points within an eighth of a pixel on
// both axes (the device merges them); a closing edge under a quarter of a
// pixel. Measured in the outline's own points (Q10), before its node's scale.
static int outline_faults = 0;
static void outlines(const mach::Frame& f, const char* scenario, double t) {
    const unsigned n = mach::Frame::point_count;
    for (unsigned p = 0; p < mach::Frame::path_count; ++p) {
        if (f.nodes[p].x >= opxy::draw::hidden_x * opxy::draw::unit) continue;
        const opxy::draw::Point* q = f.paths[p].points;
        const char* fault = 0;
        unsigned at = 0;
        double area = 0;
        for (unsigned i = 0; i < n && !fault; ++i) {
            const opxy::draw::Point &a = q[(i + n - 1) % n], &b = q[i], &c = q[(i + 1) % n];
            area += static_cast<double>(b.x) * c.y - static_cast<double>(c.x) * b.y;
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            const double span = std::fmax(std::fabs(vx), std::fabs(vy));
            const double dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
            if (i + 1 < n && std::fabs(vx) < 128 && std::fabs(vy) < 128) fault = "neighbouring points merge";
            else if (i + 1 == n && span < 256) fault = "its closing edge is under a quarter of a pixel";
            else if (dot < 0 && std::fabs(cross) < 0.035 * -dot) fault = "it turns straight back";
            at = i;
        }
        if (!fault && std::fabs(area) / 2 < 1024.0 * 1024.0 / 4) fault = "it has no area";
        if (fault) {
            if (outline_faults < 12) std::printf("FAIL outline %u in %s at %.3f s: %s (point %u)\n", p, scenario, t, fault, at);
            ++outline_faults;
        }
    }
}

static Churn run(const char* scenario, double fps, const std::vector<double>& snaps, bool quiet);

// The header's text in the OP-XY's face, Univers Next Pro Light: its advance
// widths (thousandths of the size, no kerning, as the device lays text out)
// for ' ' to '~'. A value (20 px on the page being edited) has 55 px before
// the next column's title, a title (10 px) 58; each keeps 2 px clear.
static const int advance[95] = {280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560, 560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745, 602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589, 574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
static double text_width(const char* text, double px) {
    double w = 0;
    for (const char* c = text; *c; ++c) w += (*c >= 32 && *c < 127 ? advance[*c - 32] : 1000) * px / 1000;
    return w;
}
static int caption_faults = 0;
static double widest_value = 0;
static void captions(const mach::Frame& f) {
    for (unsigned i = 0; i < 16; ++i) {
        const bool value = (i >= 4 && i < 8) || i >= 12;
        const double w = text_width(f.texts[i].chars, value ? 20 : 10);
        if (value && w > widest_value) widest_value = w;
        if (w > (value ? 53 : 56)) {
            if (caption_faults < 12) std::printf("FAIL caption \"%s\" is %.1f px wide, over %d\n", f.texts[i].chars, w, value ? 53 : 56);
            ++caption_faults;
        }
    }
}
// Every caption the header can show: each control swept in 400ths from the
// Init sound, and every control at its ends and middle together (the air's
// and tune's captions depend on others).
static bool captions_fit() {
    static mach::DisplayState state;
    static mach::Frame frame;
    mach::display_reset(state);
    opxy::draw::Input input = {};
    input.track = 1;
    for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    const double init[8] = {0.6667, 0.301, 0.7, 0.5805, 0.4, 0.15, 1.0, 0.0};
    auto draw = [&](const double* v) {
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(v[i]);
        for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
        mach::display(state, input, frame);
        captions(frame);
    };
    double v[8];
    for (int c = 0; c < 8; ++c)
        for (int k = 0; k <= 400; ++k) {
            for (int i = 0; i < 8; ++i) v[i] = init[i];
            v[c] = k / 400.0;
            draw(v);
        }
    for (int n = 0; n < 6561; ++n) {
        int m = n;
        for (int i = 0; i < 8; ++i) { v[i] = (m % 3) / 2.0; m /= 3; }
        draw(v);
    }
    return caption_faults == 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        std::vector<double> snaps;
        for (int i = 3; i < argc; ++i) snaps.push_back(std::atof(argv[i]));
        const Churn c = run(argv[1], argc > 2 ? std::atof(argv[2]) : 60, snaps, false);
        return c.valid ? 0 : 1;
    }
    bool ok = true;
    for (const char* scenario : {"flight", "session", "echo", "subsonic"})
        for (double fps : {60.0, 30.0}) {
            const Churn c = run(scenario, fps, std::vector<double>(), true);
            const bool pass = c.valid && c.mean <= 20 && c.flying <= 20 && c.peak <= 32;
            std::printf("%s %s at %.0f fps: churn %.2f a frame on average, %.2f while a flight shows, %.0f at most\n",
                        pass ? "ok  " : "FAIL", scenario, fps, c.mean, c.flying, c.peak);
            ok = ok && pass;
        }
    // A flight at every control's setting, in twentieths, from the Init sound,
    // below Mach 1 and at Mach 3: every outline it reshapes draws cleanly.
    const int before = outline_faults;
    char sweep[48];
    for (int c = 0; c < 8; ++c)
        for (int k = 0; k <= 20; ++k)
            for (double m : {-1.0, 0.6, 3.0}) {
                std::snprintf(sweep, sizeof sweep, "sweep:%d:%.2f:%.2f", c, k / 20.0, m);
                ok = run(sweep, 30, std::vector<double>(), true).valid && ok;
            }
    const bool clean = outline_faults == 0;
    std::printf("%s outlines: %d faults over the sessions and %d sweeps (no area, merged points, sliver closings)\n",
                clean ? "ok  " : "FAIL", outline_faults, 8 * 21 * 3);
    (void)before;
    ok = ok && clean;
    const bool fit = captions_fit();
    std::printf("%s captions: every one fits its column (%d too wide; the widest value %.1f px of 53)\n",
                fit ? "ok  " : "FAIL", caption_faults, widest_value);
    ok = ok && fit;
    return ok ? 0 : 1;
}

static Churn run(const char* scenario, double fps, const std::vector<double>& snaps, bool quiet) {
    double controls[8] = {mach_control(2.0), std::log2(20 / 3.0) / std::log2(100.0), 0.9, 0.5, 0.6, 0.35, 0.5, 0};
    std::vector<Step> steps;
    double length = 6;
    // A session: idle, a supersonic note held 3 s, control turns, a subsonic
    // chord, echo and a dive.
    if (!std::strcmp(scenario, "flight")) {
        steps = {{0.5, 60, 1, -1, 0}, {3.5, 60, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "session")) {
        length = 16;
        steps = {{0.5, 60, 1, -1, 0}, {3.0, 60, 0, -1, 0},
                 {3.5, 0, 0, 0, mach_control(1.4)}, {3.6, 0, 0, 0, mach_control(1.0)}, {3.7, 0, 0, 0, mach_control(0.7)},
                 {4.2, 64, 1, -1, 0}, {4.25, 67, 1, -1, 0}, {6.5, 64, 0, -1, 0}, {6.5, 67, 0, -1, 0},
                 {7.0, 0, 0, 0, mach_control(2.5)}, {7.2, 0, 0, 7, 0.6}, {7.4, 0, 0, 6, 0.75}, {7.6, 0, 0, 1, 0.7},
                 {8.0, 60, 1, -1, 0}, {11.0, 60, 0, -1, 0},
                 {11.5, 0, 0, 2, 0.1}, {11.7, 0, 0, 3, 0.8}, {11.9, 0, 0, 4, 1.0}, {12.1, 0, 0, 5, 0.9},
                 {12.5, 0, 0, 0, mach_control(0.4)}, {12.6, 0, 0, 1, 0.2},
                 {13.0, 60, 1, -1, 0}, {15.5, 60, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "echo")) {
        controls[7] = 0.6; controls[6] = 0.72; controls[0] = mach_control(1.8); controls[1] = 0.6;
        steps = {{0.5, 60, 1, -1, 0}, {4.5, 60, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "tour")) {
        // From the Init sound: a supersonic bell flight; then a narrower cone,
        // the organ's rifle bullet, a full crack and the plate, another flight;
        // then a subsonic pass, its bow wave ahead, the waves wider as tune
        // lowers the source.
        length = 13;
        const double init[8] = {0.6667, 0.301, 0.7, 0.5805, 0.4, 0.15, 1.0, 0.0};
        for (int i = 0; i < 8; ++i) controls[i] = init[i];
        steps = {{0.8, 60, 1, -1, 0}, {3.4, 60, 0, -1, 0},
                 {3.9, 0, 0, 0, 0.875}, {4.3, 0, 0, 2, 0.1}, {4.7, 0, 0, 4, 0.95}, {5.1, 0, 0, 7, 0.6},
                 {5.6, 55, 1, -1, 0}, {8.2, 55, 0, -1, 0},
                 {8.7, 0, 0, 0, 0.2143}, {9.1, 0, 0, 6, 0.5}, {9.5, 0, 0, 1, 0.65}, {9.9, 0, 0, 2, 0.3},
                 {10.3, 48, 1, -1, 0}, {12.6, 48, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "gallery")) {
        // Idle: each source's projectile in turn (a second each), then boom
        // from off through a thump to the crack, then echo and tune.
        length = 9;
        controls[0] = mach_control(1.6);
        steps = {{0.0, 0, 0, 2, 0.1}, {1.0, 0, 0, 2, 0.3}, {2.0, 0, 0, 2, 0.5}, {3.0, 0, 0, 2, 0.7}, {4.0, 0, 0, 2, 0.9},
                 {5.0, 0, 0, 4, 0.0}, {6.0, 0, 0, 4, 0.2}, {7.0, 0, 0, 4, 1.0}, {7.5, 0, 0, 7, 0.7}, {8.0, 0, 0, 6, 0.0}};
    } else if (!std::strncmp(scenario, "still:", 6)) {
        // still:<control>:<value>: the Init sound with one control set, idle
        // (the contact sheet, tools/make_contact.py).
        length = 1.5;
        const double init[8] = {0.6667, 0.301, 0.7, 0.5805, 0.4, 0.15, 1.0, 0.0};
        for (int i = 0; i < 8; ++i) controls[i] = init[i];
        int c = 0; double v = 0;
        std::sscanf(scenario + 6, "%d:%lf", &c, &v);
        if (c >= 0 && c < 8) controls[c] = v;
    } else if (!std::strncmp(scenario, "sweep:", 6)) {
        // sweep:<control>:<value>[:<mach>]: the Init sound with one control set
        // (and the speed, if given), idle, then a note held through its flight.
        length = 6;
        const double init[8] = {0.6667, 0.301, 0.7, 0.5805, 0.4, 0.15, 1.0, 0.0};
        for (int i = 0; i < 8; ++i) controls[i] = init[i];
        int c = 0; double v = 0, m = -1;
        std::sscanf(scenario + 6, "%d:%lf:%lf", &c, &v, &m);
        if (m > 0 && c != 0) controls[0] = mach_control(m);
        if (c >= 0 && c < 8) controls[c] = v;
        steps = {{0.3, 60, 1, -1, 0}, {5.5, 60, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "subsonic")) {
        controls[0] = mach_control(0.6); controls[1] = 0.8; controls[2] = 0.1;
        steps = {{0.5, 60, 1, -1, 0}, {5.5, 60, 0, -1, 0}};
    }
    static mach::DisplayState state;
    static mach::Frame frame;
    static mach::Frame last;
    mach::display_reset(state);
    opxy::draw::Input input = {};
    input.track = 1;
    input.held_notes = 0;
    for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    const int frames = static_cast<int>(length * fps);
    const opxy::i32 tick = static_cast<opxy::i32>(std::lround(1024 / fps));
    size_t next = 0, snap = 0;
    int held = 0;
    double total = 0, worst = 0, worst_at = 0, total_flying = 0;
    int flying_frames = 0;
    for (int n = 0; n < frames; ++n) {
        const double t = n / fps;
        while (next < steps.size() && steps[next].at <= t) {
            const Step& s = steps[next++];
            if (s.control >= 0) controls[s.control] = s.value;
            else if (s.down) { ++held; ++input.note_serial; }
            else { --held; ++input.note_off_serial; }
        }
        input.held_notes = static_cast<opxy::u32>(held);
        input.elapsed = n ? tick : 0;
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
        for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
        mach::display(state, input, frame);
        if (!opxy::draw::valid(frame)) { std::printf("FAIL invalid frame in %s at %.3f s\n", scenario, t); return Churn{0, 0, 0, false}; }
        outlines(frame, scenario, t);
        int churn = 0;
        if (n) {
            for (unsigned i = 0; i < mach::Frame::node_count; ++i) {
                const opxy::draw::Node &a = frame.nodes[i], &b = last.nodes[i];
                if (a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint)
                    ++churn;
                else if (i < mach::Frame::path_count && frame.paths[i].key != last.paths[i].key) ++churn;
            }
            total += churn;
            if (state.flying) { total_flying += churn; ++flying_frames; }
            if (churn > worst) { worst = churn; worst_at = t; }
        }
        last = frame;
        if (snap < snaps.size() && t + 0.5 / fps >= snaps[snap]) { dump(frame, t); ++snap; }
    }
    const Churn c = {total / (frames - 1), flying_frames ? total_flying / flying_frames : 0.0, worst, true};
    if (!quiet)
        std::fprintf(stderr, "%s at %.0f fps: %d frames, churn mean %.2f, while flying %.2f, peak %.0f at %.2f s\n", scenario, fps, frames,
                     c.mean, c.flying, worst, worst_at);
    return c;
}
