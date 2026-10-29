// Sand's screen checks, run by sdk.py test: the screen runs on the host over
// every figure, each control at five settings and a note's animation, and
// fails on any frame the OP-XY would draw wrongly, beyond what
// opxy::draw::valid checks; and that every header caption fits its column
// in the OP-XY's font. A shown outline (a mutable path) draws long
// streaks or slivers on the device when it has no area (traced out and back,
// or turning straight back at a point), when neighbouring points lie within
// an eighth of a pixel (they merge), or when its closing edge is under a
// quarter of a pixel.
//
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I. tests/screen.cpp -o screen && ./screen
#include "display.cpp"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace opxy::draw;
typedef sand::Frame F;
const opxy::i32 apart = unit / 8;  // path units (Q10): the outlines are drawn at twice this on screen
int failures = 0;

bool fail(const char* when, const char* what, unsigned p, long a, long b) {
    std::printf("%s: path %u %s (%ld, %ld)\n", when, p, what, a, b);
    ++failures;
    return false;
}

void check_paths(const F& f, const char* when) {
    for (unsigned p = 0; p < F::path_count; ++p) {
        if (f.nodes[p].x >= hidden_x * unit) continue;  // not shown
        const Point* q = f.paths[p].points;
        const unsigned n = F::point_count;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            const long dx = b.x - a.x, dy = b.y - a.y;
            const long size = dx * dx + dy * dy;
            // Neighbours, and the closing edge from the last point to the first.
            if (i + 1 < n && (dx < apart && dx > -apart && dy < apart && dy > -apart)) fail(when, "points merge at", p, i, i + 1);
            if (i + 1 == n && size < 4L * apart * apart) fail(when, "closes over", p, dx, dy);
            // A turn straight back: the next edge runs back along this one.
            const Point& c = q[(i + 2) % n];
            const long ex = c.x - b.x, ey = c.y - b.y;
            const long cross = dx * ey - dy * ex, dot = dx * ex + dy * ey;
            const long next = ex * ex + ey * ey;
            if (dot < 0 && size && next) {
                // sin of the turn's remainder under about 3 degrees: (cross^2 < 0.003 |d|^2 |e|^2)
                const double c2 = static_cast<double>(cross) * cross, s = static_cast<double>(size) * next;
                if (c2 < 0.003 * s) fail(when, "turns straight back at point", p, (i + 1) % n, 0);
            }
        }
        // Traced out and back: two edges running opposite ways along the same line.
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            const double ux = b.x - a.x, uy = b.y - a.y, length = ux * ux + uy * uy;
            if (!length) continue;
            for (unsigned j = i + 1; j < n; ++j) {
                const Point& c = q[j];
                const Point& d = q[(j + 1) % n];
                const double wx = d.x - c.x, wy = d.y - c.y;
                if (ux * wx + uy * wy >= 0) continue;
                const double cross = ux * wy - uy * wx;
                if (cross * cross > 1e-4 * length * (wx * wx + wy * wy)) continue;  // not parallel
                const double off = ux * (c.y - a.y) - uy * (c.x - a.x);
                if (off * off >= 0.01 * unit * unit * length) continue;            // not on the same line (0.1 px)
                const double t1 = (ux * (c.x - a.x) + uy * (c.y - a.y)) / length, t2 = (ux * (d.x - a.x) + uy * (d.y - a.y)) / length;
                if ((t1 > t2 ? t1 : t2) > 0.01 && (t1 < t2 ? t1 : t2) < 0.99) { fail(when, "runs back over itself on edges", p, i, j); break; }
            }
        }
        // Area, by the shoelace formula, in square path units.
        double area = 0;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            area += (static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y) / 2;
        }
        area /= static_cast<double>(unit) * unit;
        if (area < 4 && area > -4) fail(when, "has almost no area (square path px)", p, static_cast<long>(area * 1000), 0);
    }
    for (unsigned i = 0; i < F::node_count; ++i) {
        const Node& m = f.nodes[i];
        if (!placement(m.x, m.y, m.scale) || !rotation_valid(m.cos, m.sin) || m.shade >= F::palette_count || m.paint > fill_and_stroke) {
            std::printf("%s: node %u is invalid\n", when, i);
            ++failures;
        }
    }
}
}

// Univers Next Pro Light's advance widths for printable ASCII (32 to 126), in
// thousandths of an em: the OP-XY lays out its header text by them.
const int kAdvance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
// A caption's width in pixels at a size, or -1 for a character outside printable ASCII.
double text_width(const char* text, int px) {
    int total = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) return -1;
        total += kAdvance[*c - 32];
    }
    return total * px / 1000.0;
}

// Every caption the header can show, each control swept in 1024 steps on its
// own page, fits its column in the OP-XY's font: a value at 20 px within
// 53 px, a title at 10 px within 56 px (each leaves 2 px clear of the next
// column, or of the screen's edge).
int check_captions() {
    static sand::DisplayState state;
    static F frame;
    const double defaults[8] = {0.2083, 0.6, 0.6, 0.3, 0.1667, 0.5, 0, 0};
    std::set<std::string> values, titles;
    for (int c = 0; c < 8; ++c) {
        for (int k = 0; k <= 1024; ++k) {
            Input input;
            std::memset(&input, 0, sizeof input);
            for (int i = 0; i < 8; ++i) input.slots[i] = static_cast<opxy::i32>((i == c ? k / 1024.0 : defaults[i]) * 2147483647.0);
            for (int i = 0; i < 4; ++i) {
                input.shown[i] = static_cast<opxy::u8>(c < 4 ? i : 4 + i);
                input.parameters[i] = input.slots[input.shown[i]];
            }
            input.track = 1;
            sand::display_reset(state);
            sand::display(state, input, frame);
            for (int i = 0; i < 4; ++i) {
                values.insert(frame.header[i]);
                titles.insert(frame.texts[i].chars);      // the bright row's titles
                titles.insert(frame.texts[8 + i].chars);  // and the other page's
            }
        }
    }
    int wide = 0;
    for (const std::string& v : values) {
        const double w = text_width(v.c_str(), 20);
        if (w < 0 || w > 53) { std::printf("caption '%s' is %.1f px wide at 20 px (at most 53)\n", v.c_str(), w); ++wide; }
    }
    for (const std::string& t : titles) {
        const double w = text_width(t.c_str(), 10);
        if (w < 0 || w > 56) { std::printf("title '%s' is %.1f px wide at 10 px (at most 56)\n", t.c_str(), w); ++wide; }
    }
    std::printf("%u captions and %u titles measured\n", static_cast<unsigned>(values.size()), static_cast<unsigned>(titles.size()));
    return wide;
}

int main() {
    if (check_captions()) return 1;
    static sand::DisplayState state;
    static F frame;
    const double steps[5] = {0, 0.25, 0.5, 0.75, 1};
    const double defaults[8] = {0.2083, 0.6, 0.6, 0.3, 0.1667, 0.5, 0, 0};
    unsigned frames = 0;
    // Every figure at the defaults, then each control at five settings.
    for (int control = -1; control < 8; ++control) {
        const int settings = control < 0 ? 12 : 5;
        for (int s = 0; s < settings; ++s) {
            Input input;
            std::memset(&input, 0, sizeof input);
            for (int c = 0; c < 8; ++c) {
                double v = defaults[c];
                if (control < 0 && c == 0) v = (s + 0.5) / 12;
                if (c == control) v = steps[s] > 0.999 && (c == 0 || c == 4) ? 0.999 : steps[s];
                input.slots[c] = static_cast<opxy::i32>(v * 2147483647.0);
            }
            for (int c = 0; c < 4; ++c) { input.parameters[c] = input.slots[c]; input.shown[c] = static_cast<opxy::u8>(c); }
            input.track = 1;
            sand::display_reset(state);
            // A note held for three seconds and released, at 30 frames a second.
            for (int t = 0; t < 150; ++t) {
                input.elapsed = 34;
                if (t == 3) { input.note_serial = 1; input.held_notes = 1; }
                if (t == 93) { input.held_notes = 0; input.note_off_serial = 1; }
                sand::display(state, input, frame);
                char when[80];
                std::snprintf(when, sizeof when, "control %d setting %d frame %d", control, s, t);
                if (!valid(frame)) { std::printf("%s: invalid frame\n", when); ++failures; }
                check_paths(frame, when);
                ++frames;
            }
        }
    }
    if (failures) { std::printf("%d failures in %u frames\n", failures, frames); return 1; }
    std::printf("%u frames valid\n", frames);
    return 0;
}
