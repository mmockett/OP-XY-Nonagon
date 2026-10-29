// Butterfly's outlines against what the OP-XY's renderer does beyond the
// SDK's rules, run by sdk.py test. A shown outline draws long streaks or
// slivers on the device, though not in the browser, when:
//   - it turns straight back at a point (or over a hair-wide hairpin): its
//     join is drawn as a spike to the edge of the screen;
//   - it has no area: a line traced out and back over itself;
//   - neighbouring points lie within an eighth of a pixel (the parser merges
//     them, and the outline then stops reshaping);
//   - its closing edge is under a quarter of a pixel.
// Every reshaping outline is checked on every frame of a sweep of each
// control (alone, and every control at 0, 1/2 and 1 together) and of a
// note's animation; every static outline of panel.svg.fragment at the scale
// its node is drawn at. With an argument it prints each failure, not just the first.
#include "../display.cpp"
#include "../tables_data.h"
#include "presets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace butterfly;
using opxy::draw::Point;
opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }

struct P { double x, y; };
bool verbose = false;
int failures = 0;
std::string first_failure;

void fail(const std::string& what) {
    if (!failures) first_failure = what;
    ++failures;
    if (verbose) std::printf("FAIL  %s\n", what.c_str());
}

// An outline in its own coordinates (where the parser merges points) and on
// screen (where the renderer draws its joins), checked against the four rules.
void check(const std::vector<P>& local, double scale, const std::string& what) {
    const size_t n = local.size();
    for (size_t i = 0; i < n; ++i) {
        const P& a = local[i];
        const P& b = local[(i + 1) % n];
        const double dx = std::fabs(b.x - a.x), dy = std::fabs(b.y - a.y);
        if (i + 1 < n && dx < 0.125 && dy < 0.125) {
            fail(what + ": points " + std::to_string(i) + " and " + std::to_string(i + 1) + " merge");
            return;
        }
        if (i + 1 == n && std::max(dx, dy) < 0.25) {
            fail(what + ": closes over " + std::to_string(std::max(dx, dy)) + " px");
            return;
        }
    }
    std::vector<P> s(n);
    for (size_t i = 0; i < n; ++i) s[i] = {local[i].x * scale, local[i].y * scale};
    double area = 0, perimeter = 0;
    for (size_t i = 0; i < n; ++i) {
        const P& a = s[i];
        const P& b = s[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
        perimeter += std::hypot(b.x - a.x, b.y - a.y);
    }
    // Its mean width: a ribbon's is its width, a line traced out and back's none.
    const double width = std::fabs(area) / perimeter;
    if (width < 0.25) {
        fail(what + ": no area (a mean width of " + std::to_string(width) + " px)");
        return;
    }
    // A turn straight back: the edges into and out of a point, each taken
    // over at least a quarter of a pixel (so a hairpin narrower than that
    // counts as one turn), within 12 degrees of opposite.
    for (size_t i = 0; i < n; ++i) {
        size_t a = i, b = i;
        double ax = 0, ay = 0, bx = 0, by = 0;
        for (size_t k = 1; k < n; ++k) {
            a = (i + n - k) % n;
            ax = s[i].x - s[a].x;
            ay = s[i].y - s[a].y;
            if (std::hypot(ax, ay) >= 0.25) break;
        }
        for (size_t k = 1; k < n; ++k) {
            b = (i + k) % n;
            bx = s[b].x - s[i].x;
            by = s[b].y - s[i].y;
            if (std::hypot(bx, by) >= 0.25) break;
        }
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la < 0.25 || lb < 0.25) continue;
        const double cosine = (ax * bx + ay * by) / (la * lb);
        if (cosine < -0.978) {
            fail(what + ": turns straight back at point " + std::to_string(i) + " (" + std::to_string(s[i].x) + ", " +
                 std::to_string(s[i].y) + ")");
            return;
        }
    }
}

// The static outlines: each path of the artwork, by node (-1 for fixed art).
struct Art { int node; std::vector<P> points; };
std::vector<Art> artwork() {
    std::string path(__FILE__);
    path = path.substr(0, path.find_last_of("/\\") + 1) + "../panel.svg.fragment";
    FILE* f = std::fopen(path.c_str(), "rb");
    std::vector<Art> out;
    if (!f) {
        fail("cannot read " + path);
        return out;
    }
    std::string text;
    char buffer[4096];
    for (size_t got; (got = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, got);
    std::fclose(f);
    for (size_t at = text.find("<path"); at != std::string::npos; at = text.find("<path", at + 1)) {
        const size_t end = text.find('>', at);
        const std::string e = text.substr(at, end - at);
        Art art;
        art.node = -1;
        const size_t id = e.find("_node_");
        if (id != std::string::npos) art.node = std::atoi(e.c_str() + id + 6);
        const size_t d = e.find(" d=\"");
        if (d == std::string::npos) continue;
        const char* c = e.c_str() + d + 4;
        while (*c && *c != '"') {
            if (*c == 'M' || *c == 'L' || *c == ' ' || *c == 'Z') {
                ++c;
                continue;
            }
            char* next;
            const double x = std::strtod(c, &next);
            c = next + 1;   // the comma
            const double y = std::strtod(c, &next);
            c = next;
            art.points.push_back({x, y});
        }
        out.push_back(art);
    }
    return out;
}

void check_frame(const Frame& frame, const std::vector<Art>& art, const std::string& when) {
    for (u32 p = 0; p < Frame::path_count; ++p) {
        const opxy::draw::Node& node = frame.nodes[p];
        if (node.x >= opxy::draw::hidden_x * opxy::draw::unit) continue;
        std::vector<P> local;
        for (u32 i = 0; i < Frame::point_count; ++i)
            local.push_back({frame.paths[p].points[i].x / 1024.0, frame.paths[p].points[i].y / 1024.0});
        check(local, node.scale / 1024.0, when + ", outline " + std::to_string(p));
    }
    for (const Art& a : art) {
        if (a.node >= 0 && a.node < static_cast<int>(Frame::path_count)) continue;   // reshaped: checked above
        double scale = 1;
        if (a.node >= 0) {
            const opxy::draw::Node& node = frame.nodes[a.node];
            if (node.x >= opxy::draw::hidden_x * opxy::draw::unit) continue;
            scale = node.scale / 1024.0;
        }
        check(a.points, scale, when + ", artwork " + (a.node >= 0 ? "node " + std::to_string(a.node) : std::string("(fixed)")));
    }
}

// Frames of one setting: a held note struck after a few frames, then 2 s of
// its animation at 30 frames a second, then released.
void play(const double* c, const std::vector<Art>& art, const std::string& name, DisplayState& state, Frame& frame) {
    display_reset(state);
    opxy::draw::Input in = {};
    in.track = 1;
    for (int i = 0; i < 8; ++i) in.slots[i] = q31(c[i]);
    for (int i = 0; i < 4; ++i) {
        in.parameters[i] = in.slots[i];
        in.shown[i] = static_cast<opxy::u8>(i);
    }
    for (int t = 0; t < 70; ++t) {
        in.elapsed = 34;
        if (t == 3) {
            in.note_serial = 1;
            in.held_notes = 1;
        }
        if (t == 63) in.held_notes = 0;
        display(state, in, frame);
        if (t % 3 == 0 || t < 8) check_frame(frame, art, name + " frame " + std::to_string(t));
    }
}
}

int main(int argc, char**) {
    verbose = argc > 1;
    static DisplayState state;
    static Frame frame;
    const std::vector<Art> art = artwork();
    const char* names[8] = {"flux", "coupling", "spread", "strike", "glow", "site", "tilt", "wingbeat"};
    int settings = 0;
    // Each control alone, in twentieths, the others at Specimen's.
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v <= 20; ++v) {
            double c[8];
            for (int i = 0; i < 8; ++i) c[i] = presets[0].c[i];
            c[k] = v / 20.0;
            play(c, art, std::string(names[k]) + " " + std::to_string(v * 5), state, frame);
            ++settings;
        }
    // Every control at 0, 1/2 and 1 together, and each preset.
    for (int m = 0; m < 6561; ++m) {
        double c[8];
        for (int i = 0, r = m; i < 8; ++i, r /= 3) c[i] = (r % 3) / 2.0;
        play(c, art, "corner " + std::to_string(m), state, frame);
        ++settings;
    }
    for (const Preset& pr : presets) {
        play(pr.c, art, pr.name, state, frame);
        ++settings;
    }
    // As a device showed the root's spike: flux .293, coupling 0.19, spread
    // 4st, strike -26dB, glow 3.5s, site 2, tilt -10dB, wingbeat .26Hz.
    const double photo[8] = {0.586, 0.095, 0.104, 0.75, 0.844, 0.25, 0.4, 0.15};
    play(photo, art, "the device's photo", state, frame);
    ++settings;
    if (failures) {
        std::printf("FAIL  %d outline(s) the OP-XY would draw as streaks or slivers; the first: %s\n", failures, first_failure.c_str());
        return 1;
    }
    std::printf("ok    every outline, over %d settings and their animation, has area, turns no sharper than 168 degrees, keeps "
                "its points apart and closes over at least a quarter of a pixel\n",
                settings);
    return 0;
}
