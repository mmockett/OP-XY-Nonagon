// What the OP-XY's own drawing does beyond the SDK's rules (after MNMD's
// tools/check_frame.cpp): it draws a path as long streaks or slivers when the
// path has no area (a line traced out and back over itself, or a turn
// straight back at a point), when neighbouring points lie within an eighth of
// a pixel (they merge), or when its closing edge is under a quarter of a
// pixel. Checks every path as the screen draws it: the static art at its own
// size, each shown node at its scale, and the drops' reshaping outlines, over
// each control's sweep (0, 25, 50, 75 and 100, the others at their defaults)
// while a note and then a chord of drops live and die. Run by sdk.py test.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Pt { double x, y; };
struct Shape { int node; std::vector<Pt> points; };   // node -1: static art

const double merge = 0.125;     // px: neighbours closer merge
const double sliver = 0.25;     // px: a shorter closing edge draws a sliver
const double thinnest = 0.25;   // px: a path's mean width, twice its area over its perimeter

// The paths of panel.svg.fragment, each with its node (or -1) and its points.
std::vector<Shape> art() {
    std::string file = __FILE__;
    file = file.substr(0, file.find_last_of("/\\") + 1) + "../panel.svg.fragment";
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) f = std::fopen("panel.svg.fragment", "rb");
    std::vector<Shape> shapes;
    if (!f) { std::printf("FAIL cannot read %s\n", file.c_str()); return shapes; }
    std::string text;
    char buffer[4096];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
    std::fclose(f);
    for (size_t at = text.find("<path"); at != std::string::npos; at = text.find("<path", at + 5)) {
        const size_t end = text.find('>', at);
        const std::string tag = text.substr(at, end - at);
        Shape s;
        const size_t id = tag.find("id=\"drop_node_");
        s.node = id == std::string::npos ? -1 : std::atoi(tag.c_str() + id + 14);
        const size_t d = tag.find(" d=\"");
        if (d == std::string::npos) continue;
        const char* p = tag.c_str() + d + 4;
        while (*p && *p != '"') {
            if (*p == 'M' || *p == 'L' || *p == ' ' || *p == ',' ) { ++p; continue; }
            if (*p == 'Z') break;
            char* next;
            const double x = std::strtod(p, &next);
            p = next;
            while (*p == ',' || *p == ' ') ++p;
            const double y = std::strtod(p, &next);
            p = next;
            s.points.push_back({x, y});
        }
        shapes.push_back(s);
    }
    return shapes;
}

int failures = 0;
std::vector<std::string> seen;

void fail(const std::string& what) {
    // Each path's each rule once: the first time it breaks.
    const std::string key = what.substr(0, what.find(" ("));
    std::string rule = key.substr(key.find(": ") + 2);
    rule = key.substr(0, key.find(": ")) + ": " + rule.substr(0, rule.find_first_of("0123456789"));
    for (const std::string& s : seen) if (s == rule) return;
    seen.push_back(rule);
    std::printf("FAIL %s\n", what.c_str());
    ++failures;
}

// The three rules for one closed path, its points in pixels.
void check(const std::vector<Pt>& q, const std::string& name, const char* when) {
    const size_t m = q.size();
    if (m < 3) return;
    double area = 0, perimeter = 0;
    for (size_t i = 0; i < m; ++i) {
        const Pt &a = q[i], &b = q[(i + 1) % m], &c = q[(i + 2) % m];
        area += a.x * b.y - b.x * a.y;
        const double ab = std::hypot(b.x - a.x, b.y - a.y);
        perimeter += ab;
        char line[256];
        if (i + 1 < m && ab < merge) {
            std::snprintf(line, sizeof line, "%s: points %zu and %zu merge (%.3f px apart, %s)", name.c_str(), i, i + 1, ab, when);
            fail(line);
        }
        // A turn straight back at b: its edges antiparallel.
        const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
        const double dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
        if (dot < 0 && std::fabs(cross) < 0.02 * std::hypot(ux, uy) * std::hypot(vx, vy)) {
            std::snprintf(line, sizeof line, "%s: turns straight back at point %zu (%s)", name.c_str(), (i + 1) % m, when);
            fail(line);
        }
    }
    const double closing = std::hypot(q[0].x - q[m - 1].x, q[0].y - q[m - 1].y);
    char line[256];
    if (closing < sliver) {
        std::snprintf(line, sizeof line, "%s: closes over %.3f px (%s)", name.c_str(), closing, when);
        fail(line);
    }
    if (perimeter > 0 && std::fabs(area) / perimeter < thinnest / 2) {
        std::snprintf(line, sizeof line, "%s: has no area (mean width %.3f px, %s)", name.c_str(), std::fabs(area) / perimeter, when);
        fail(line);
    }
}

opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }
}

int main() {
    using namespace opxy::draw;
    const std::vector<Shape> shapes = art();
    if (shapes.empty()) return 1;
    // The static art, at its own size.
    for (size_t k = 0; k < shapes.size(); ++k)
        if (shapes[k].node < 0) check(shapes[k].points, "static path " + std::to_string(k), "as authored");

    static drop::DisplayState state;
    static drop::Frame frame;
    const char* const names[8] = {"heat", "size", "liquid", "beat", "groove", "vapour", "life", "crackle"};
    for (int control = 0; control < 8; ++control)
        for (int step = 0; step <= 4; ++step) {
            double controls[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25};   // definition.json's defaults
            controls[control] = step / 4.0;
            Input input = {};
            input.track = 1;
            for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
            for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
            drop::display_reset(state);
            // A note at 0.1 s, a chord of three at 1 s; 10 s at 60 frames a second.
            for (int n = 0; n < 600; ++n) {
                if (n == 6 || n == 60) ++input.note_serial;
                if (n == 61 || n == 62) ++input.note_serial;
                input.elapsed = n ? 17 : 0;
                drop::display(state, input, frame);
                char when[96];
                std::snprintf(when, sizeof when, "%s %d at %.2f s", names[control], step * 25, n / 60.0);
                for (unsigned p = 0; p < drop::Frame::path_count; ++p) {
                    const Node& node = frame.nodes[p];
                    if (node.x >= hidden_x * unit) continue;
                    std::vector<Pt> q;
                    const double k = node.scale / 1024.0 / 1024.0;
                    for (unsigned i = 0; i < drop::Frame::point_count; ++i)
                        q.push_back({frame.paths[p].points[i].x * k, frame.paths[p].points[i].y * k});
                    check(q, "outline " + std::to_string(p), when);
                }
                for (const Shape& s : shapes) {
                    if (s.node < static_cast<int>(drop::Frame::path_count)) continue;
                    const Node& node = frame.nodes[s.node];
                    if (node.x >= hidden_x * unit) continue;
                    std::vector<Pt> q;
                    const double k = node.scale / 1024.0;
                    for (const Pt& a : s.points) q.push_back({a.x * k, a.y * k});
                    check(q, "node " + std::to_string(s.node), when);
                }
            }
        }
    if (!failures) std::puts("every path drawn has area, distinct points and a closing edge");
    return failures ? 1 : 0;
}
