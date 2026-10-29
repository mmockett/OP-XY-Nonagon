// Cocoa's screen checks, compiled for the host and run by sdk.py test: plays a
// scripted scene through the real display code at 60 frames a second and
//   - checks every frame is valid, and says which element is not;
//   - counts the nodes and outlines that change each frame (churn), on
//     average and at most, and holds them to the budget;
//   - checks the idle screen is still, and reduced motion and a reset snap;
//   - checks the spoon never crosses a line of the cup: every cup, spoon from
//     wood to silver, taps' every rest, a tap's whole swing.
// Given a file, it also writes every fifth frame there as JSON lines, for
// tools/make_gif.py.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace cocoa;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

// Why a frame is invalid, or null.
const char* invalid(const Frame& f, unsigned& which) {
    using namespace opxy::draw;
    using cocoa::Frame;
    for (unsigned p = 0; p < Frame::path_count; ++p) {
        which = p;
        if (!f.paths[p].key) return "path key";
        for (unsigned i = 0; i < Frame::point_count; ++i) {
            const Point& q = f.paths[p].points[i];
            if (q.x < -path_extent * unit || q.x > path_extent * unit || q.y < -path_extent * unit || q.y > path_extent * unit)
                return "path point out of extent";
            for (unsigned j = 0; j < i; ++j)
                if (f.paths[p].points[j].x == q.x && f.paths[p].points[j].y == q.y) return "path points repeat";
        }
    }
    for (unsigned n = 0; n < Frame::node_count; ++n) {
        which = n;
        const Node& d = f.nodes[n];
        if (!placement(d.x, d.y, d.scale)) return "node placement";
        if (!rotation_valid(d.cos, d.sin)) return "node rotation";
        if (d.shade >= Frame::palette_count) return "node shade";
    }
    for (unsigned t = 0; t < Frame::text_count; ++t) {
        which = t;
        if (!placement(f.texts[t].x, f.texts[t].y, f.texts[t].scale)) return "text placement";
        if (!terminated(f.texts[t].chars, text_capacity)) return "text unterminated";
    }
    which = 0;
    return opxy::draw::valid(f) ? nullptr : "valid() says no";
}

// Path p of a frame as drawn, in screen pixels.
unsigned drawn(const Frame& f, unsigned p, double (*xy)[2]) {
    const opxy::draw::Node& n = f.nodes[p];
    const double k = n.scale / 1024.0, a = n.cos / 1024.0 * k, b = n.sin / 1024.0 * k;
    for (unsigned i = 0; i < Frame::point_count; ++i) {
        const double px = f.paths[p].points[i].x / 1024.0, py = f.paths[p].points[i].y / 1024.0;
        xy[i][0] = a * px - b * py + n.x / 1024.0;
        xy[i][1] = b * px + a * py + n.y / 1024.0;
    }
    return Frame::point_count;
}
// The least distance, px, from the spoon's lines (bowl and handle, every half
// pixel along them) to the cup's.
double spoon_to_cup(const Frame& f) {
    double cup[64][2], spoon[64][2], least = 1e9;
    drawn(f, node::vessel, cup);
    for (unsigned part = node::bowl; part <= node::handle; ++part) {
        drawn(f, part, spoon);
        for (unsigned i = 0; i < 64; ++i) {
            const double* s0 = spoon[i];
            const double* s1 = spoon[(i + 1) % 64];
            const unsigned steps = 1 + static_cast<unsigned>(std::hypot(s1[0] - s0[0], s1[1] - s0[1]) * 2);
            for (unsigned j = 0; j < steps; ++j) {
                const double x = s0[0] + (s1[0] - s0[0]) * j / steps, y = s0[1] + (s1[1] - s0[1]) * j / steps;
                for (unsigned c = 0; c < 64; ++c) {
                    const double* c0 = cup[c];
                    const double* c1 = cup[(c + 1) % 64];
                    const double dx = c1[0] - c0[0], dy = c1[1] - c0[1];
                    double t = ((x - c0[0]) * dx + (y - c0[1]) * dy) / (dx * dx + dy * dy);
                    t = t < 0 ? 0 : t > 1 ? 1 : t;
                    const double d = std::hypot(c0[0] + t * dx - x, c0[1] + t * dy - y);
                    least = d < least ? d : least;
                }
            }
        }
    }
    return least;
}

struct Run {
    DisplayState state;
    Frame frame, last;
    opxy::draw::Input in;
    double c[8];
    long frames, changed, peak;
    bool counting;
    FILE* out;
    Run() : frames(0), changed(0), peak(0), counting(false), out(nullptr) {
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        std::memset(&in, 0, sizeof in);
        const double standard[8] = {0.87, 0.55, 0.2, 0.1667, 0.65, 0.6, 0.3, 1.0};   // the long climb, tapping
        for (int k = 0; k < 8; ++k) c[k] = standard[k];
        for (int k = 0; k < 4; ++k) in.shown[k] = static_cast<opxy::u8>(k);
        in.track = 1;
        in.elapsed = 17;   // 60 frames a second, Q10 s
        display_reset(state);
    }
    void apply() {
        for (int k = 0; k < 8; ++k) in.slots[k] = static_cast<opxy::i32>(c[k] * 2147483647.0);
        for (int k = 0; k < 4; ++k) in.parameters[k] = in.slots[in.shown[k]];
    }
    unsigned difference() const {
        unsigned n = 0;
        for (unsigned p = 0; p < Frame::path_count; ++p)
            if (frame.paths[p].key != last.paths[p].key) ++n;
        for (unsigned i = Frame::path_count; i < Frame::node_count; ++i)
            if (std::memcmp(&frame.nodes[i], &last.nodes[i], sizeof(opxy::draw::Node))) ++n;
        for (unsigned p = 0; p < Frame::path_count; ++p)
            if (frame.paths[p].key == last.paths[p].key && std::memcmp(&frame.nodes[p], &last.nodes[p], sizeof(opxy::draw::Node))) ++n;
        return n;
    }
    void dump() {
        if (!out) return;
        std::fprintf(out, "{\"nodes\":[");
        for (unsigned i = 0; i < Frame::node_count; i++) {
            const opxy::draw::Node& n = frame.nodes[i];
            std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, (unsigned)n.paint);
        }
        std::fprintf(out, "],\"paths\":[");
        for (unsigned p = 0; p < Frame::path_count; p++) {
            std::fprintf(out, "%s[", p ? "," : "");
            for (unsigned k = 0; k < Frame::point_count; k++) std::fprintf(out, "%s[%d,%d]", k ? "," : "", frame.paths[p].points[k].x, frame.paths[p].points[k].y);
            std::fprintf(out, "]");
        }
        std::fprintf(out, "],\"texts\":[");
        for (unsigned t = 0; t < Frame::text_count; t++) {
            const opxy::draw::Text& x = frame.texts[t];
            std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", t ? "," : "", x.x, x.y, x.scale, x.chars);
        }
        std::fprintf(out, "]}\n");
    }
    // One frame; returns the nodes it changed.
    unsigned tick() {
        apply();
        last = frame;
        display(state, in, frame);
        unsigned which = 0;
        const char* why = invalid(frame, which);
        if (why) {
            std::printf("FAIL frame %ld invalid: %s (%u)\n", frames, why, which);
            ++failures;
        }
        const unsigned n = frames ? difference() : 0;
        if (counting) {
            changed += n;
            peak = n > peak ? n : peak;
        }
        if (frames % 5 == 0) dump();
        ++frames;
        return n;
    }
};
}

// The contact sheet: each control at 0, 50 and 100 with the others at the
// default (Mug Keys), one JSON frame each, row by row: at rest, or for powder
// and fizz 0.12 s into a note, and for taps 0.6 s into one.
void sheet(FILE* out) {
    const double defaults[8] = {0.32, 0.0, 0.7, 0.1667, 0.65, 0.6, 0.15, 1.0};
    const double values[3] = {0.0, 0.5, 1.0};
    for (int k = 0; k < 8; ++k)
        for (double v : values) {
            Run cell;
            for (int q = 0; q < 8; ++q) cell.c[q] = defaults[q];
            cell.c[k] = k == 3 ? (v == 0 ? 0.1667 : v == 0.5 ? 0.5 : 0.8333) : v;
            for (int q = 0; q < 4; ++q) cell.in.shown[q] = static_cast<opxy::u8>(k < 4 ? q : q + 4);
            for (int f = 0; f < 20; ++f) cell.tick();
            // Powder and fizz show in the bubbles, 0.12 s into a note; taps in
            // the spoon 0.6 s into one; the rest at rest.
            if (k == 0 || k == 1 || k == 2) {
                cell.in.note_serial = 1;
                cell.in.held_notes = 1;
                for (int f = 0; f < (k == 1 ? 36 : 7); ++f) cell.tick();
            }
            cell.out = out;
            cell.frames = 0;   // dump this one
            cell.tick();
            cell.out = nullptr;
        }
}

int main(int argc, char** argv) {
    if (argc > 2 && !std::strcmp(argv[2], "sheet")) {
        FILE* out = std::fopen(argv[1], "w");
        sheet(out);
        std::fclose(out);
        return failures ? 1 : 0;
    }
    // The spoon against the cup: every cup, 33 spoons, five tap rates, at rest
    // and through a held note's taps (a second at 60 fps).
    {
        double least = 1e9;
        const double cups[3] = {0.1667, 0.5, 0.8333};
        for (int cup = 0; cup < 3; ++cup)
            for (int sp = 0; sp <= 32; ++sp)
                for (int tp = 0; tp <= 4; ++tp) {
                    Run r;
                    r.c[3] = cups[cup];
                    r.c[5] = sp / 32.0;
                    r.c[1] = tp / 4.0;
                    for (int f = 0; f < 80; ++f) {
                        if (f == 10) { r.in.note_serial = 1; r.in.held_notes = 1; }
                        r.tick();
                        const double d = spoon_to_cup(r.frame);
                        if (d < least) least = d;
                        if (d < 0.9) {
                            std::printf("FAIL the spoon touches the cup: %.2f px (cup %d, spoon %d/32, taps %d/4, frame %d)\n", d, cup, sp, tp, f);
                            ++failures;
                            f = 80;
                        }
                    }
                }
        std::printf("spoon: at least %.2f px from the cup's lines, over every cup, spoon and tap\n", least);
    }
    Run run;
    if (argc > 1) run.out = std::fopen(argv[1], "w");
    // Idle: still after the first frames settle.
    for (int f = 0; f < 30; ++f) run.tick();
    unsigned idle = 0;
    for (int f = 0; f < 60; ++f) idle += run.tick();
    std::printf("idle: %u node changes in a second\n", idle);
    check(idle == 0, "the idle screen moves");

    // A note held 7 s, tapping, then let go; page 2 and its controls; a second
    // note with stir at a third; a chord; then the cup and fill change.
    run.counting = true;
    long notes = 0;
    for (int f = 0; f < 60 * 22; ++f) {
        const double t = f / 60.0;
        if (f == 0 || f == 60 * 9 || f == 60 * 14) { run.in.note_serial = static_cast<opxy::u32>(++notes); run.in.held_notes = 1; }
        if (f == 60 * 7 || f == 60 * 12 || f == 60 * 19) { run.in.held_notes = 0; ++run.in.note_off_serial; }
        if (t >= 8.0 && t < 8.4) {
            for (int k = 0; k < 4; ++k) run.in.shown[k] = static_cast<opxy::u8>(4 + k);
            run.c[7] = 1.0 - (t - 8.0) / 0.4 * 0.65;   // stir down to a third
            run.c[4] = 0.5 + (t - 8.0) / 0.4 * 0.3;    // fill up
            run.c[5] = 0.6 - (t - 8.0);                // a softer spoon
        }
        if (f == 60 * 13) { for (int k = 0; k < 4; ++k) run.in.shown[k] = static_cast<opxy::u8>(k); run.c[7] = 1.0; }
        if (f == 60 * 14) { run.c[1] = 0.85; run.in.held_notes = 3; run.in.note_serial = static_cast<opxy::u32>(++notes); }
        if (f == 60 * 20) { run.c[3] = 0.5; }
        if (f == 60 * 21) { run.c[3] = 0.8333; run.c[4] = 0.2; }
        run.tick();
    }
    const double average = static_cast<double>(run.changed) / (60 * 22);
    std::printf("churn: %.1f node changes a frame on average, %ld at most, over %d frames at 60 fps\n", average,
                run.peak, 60 * 22);
    check(average <= 20, "average churn above 20 a frame");
    check(run.peak <= 48, "peak churn above 48 a frame");

    // Reduced motion: a note changes the state but nothing swings or ripples.
    run.counting = false;
    run.in.reduced_motion = 1;
    run.in.note_serial = static_cast<opxy::u32>(++notes);
    run.in.held_notes = 1;
    run.tick();
    const opxy::draw::Node spoon = run.frame.nodes[node::handle];
    for (int f = 0; f < 120; ++f) run.tick();
    check(spoon.x == run.frame.nodes[node::handle].x || run.frame.nodes[node::handle].y != spoon.y, "the spoon swings with reduced motion");
    check(run.frame.nodes[node::pop].x == opxy::draw::hidden_x * opxy::draw::unit, "pops show with reduced motion");
    // A reset snaps: the first frame after it sets everything.
    run.in.reduced_motion = 0;
    display_reset(run.state);
    std::memset(&run.frame, 0, sizeof run.frame);
    run.tick();
    if (run.out) std::fclose(run.out);
    return failures ? 1 : 0;
}
