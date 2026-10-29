// Coin's frames against what the OP-XY's renderer does beyond the SDK's own
// rules (opxy::draw::valid), compiled for the host and run by
// python sdk.py test --machine <folder>. On the device an outline draws long
// streaks or slivers when:
//   - it has no area: a line traced out and back over itself, or a turn
//     straight back at a point;
//   - neighbouring points are within an eighth of a pixel (they merge);
//   - its closing edge is under a quarter of a pixel.
// Checked for the disk's four reshaping outlines and for every shown node of
// the artwork as each frame places it, over each control at 0, 25, 50, 75 and
// 100 (the rest at Euler's) through a note's whole animation, at every size
// and material, and with reduced motion.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace opxy::draw;
int failures = 0;
const double eighth = 1.0 / 8, quarter = 1.0 / 4;

struct Shape {
    std::vector<double> x, y;
};
std::vector<Shape> art(coin::Frame::node_count);   // each node's outline as authored, about its origin

// Reads panel.svg.fragment's node paths (circles are round at any size).
void load() {
    std::string path = __FILE__;
    path = path.substr(0, path.rfind('/')) + "/../panel.svg.fragment";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::printf("FAIL cannot read %s\n", path.c_str()); std::exit(1); }
    std::string text;
    char buffer[4096];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
    std::fclose(f);
    for (size_t at = 0; (at = text.find("<path id=\"coin_node_", at)) != std::string::npos; ++at) {
        const unsigned node = static_cast<unsigned>(std::atoi(text.c_str() + at + 19));
        const size_t d = text.find(" d=\"", at), end = text.find('"', d + 4);
        Shape s;
        for (const char* c = text.c_str() + d + 4; c < text.c_str() + end;) {
            if (*c == 'M' || *c == 'L' || *c == ' ') { ++c; continue; }
            if (*c == 'Z') break;
            char* next;
            const double x = std::strtod(c, &next);
            const double y = std::strtod(next + 1, &next);
            s.x.push_back(x);
            s.y.push_back(y);
            c = next;
        }
        if (node < art.size()) art[node] = s;
    }
}

// Checks one closed outline (pixels): returns what is wrong, or null.
const char* fault(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t m = x.size();
    if (m < 3) return nullptr;
    double area = 0;
    for (size_t i = 0; i < m; ++i) {
        const size_t j = (i + 1) % m, k = (i + 2) % m;
        area += x[i] * y[j] - x[j] * y[i];
        const double ax = x[j] - x[i], ay = y[j] - y[i], bx = x[k] - x[j], by = y[k] - y[j];
        const double a = std::sqrt(ax * ax + ay * ay), b = std::sqrt(bx * bx + by * by);
        if (std::fmax(std::fabs(ax), std::fabs(ay)) < (j == 0 ? quarter : eighth))
            return j == 0 ? "its closing edge is under a quarter of a pixel" : "neighbouring points merge";
        if (a > 0 && b > 0 && (ax * bx + ay * by) / (a * b) < -0.999) return "it turns straight back at a point";
    }
    if (std::fabs(area) / 2 < 0.05) return "it has no area";
    return nullptr;
}

void check(const coin::Frame& f, const char* when) {
    // The disk's reshaping outlines, in their own coordinates as the device's
    // loader reads them (drawn at twice that size).
    for (unsigned p = 0; p < coin::Frame::path_count; ++p) {
        const Node& n = f.nodes[p];
        if (n.x >= hidden_x * unit) continue;
        std::vector<double> x, y;
        for (unsigned i = 0; i < coin::Frame::point_count; ++i) {
            x.push_back(f.paths[p].points[i].x / 1024.0);
            y.push_back(f.paths[p].points[i].y / 1024.0);
        }
        if (const char* why = fault(x, y)) { std::printf("FAIL %s: outline %u: %s\n", when, p, why); ++failures; }
    }
    // The artwork's nodes, as authored and as placed, scaled and turned.
    for (unsigned i = coin::Frame::path_count; i < coin::Frame::node_count; ++i) {
        const Node& n = f.nodes[i];
        if (n.x >= hidden_x * unit || art[i].x.empty()) continue;
        if (const char* why = fault(art[i].x, art[i].y)) { std::printf("FAIL %s: node %u as authored: %s\n", when, i, why); ++failures; }
        const double s = n.scale / 1024.0, c = n.cos / 1024.0, sn = n.sin / 1024.0;
        std::vector<double> x, y;
        for (size_t k = 0; k < art[i].x.size(); ++k) {
            x.push_back(s * (c * art[i].x[k] - sn * art[i].y[k]));
            y.push_back(s * (sn * art[i].x[k] + c * art[i].y[k]));
        }
        if (const char* why = fault(x, y)) { std::printf("FAIL %s: node %u: %s\n", when, i, why); ++failures; }
    }
}

opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }

// Plays a note's animation, 30 fps, with the controls `c`, checking each frame.
void play(const double (&c)[8], bool reduced, const char* name) {
    static coin::DisplayState state;
    static coin::Frame frame;
    Input input;
    std::memset(&input, 0, sizeof input);
    std::memset(&frame, 0, sizeof frame);
    input.reduced_motion = reduced;
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    coin::display_reset(state);
    const int before = failures;
    for (int t = 0; t < 150 && failures - before < 3; ++t) {
        input.elapsed = t ? 34 : 0;
        if (t == 3) { input.note_serial++; input.held_notes = 1; }
        if (t == 60) input.held_notes = 0;
        coin::display(state, input, frame);
        char when[96];
        std::snprintf(when, sizeof when, "%s, frame %d", name, t);
        check(frame, when);
    }
}
}

int main() {
    load();
    const double euler[8] = {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125};
    const char* names[8] = {"spin", "size", "disk", "base", "wobble", "air", "ring", "many"};
    int runs = 0;
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v <= 4; ++v) {
            double c[8];
            std::memcpy(c, euler, sizeof c);
            c[k] = v / 4.0;
            char name[48];
            std::snprintf(name, sizeof name, "%s at %d", names[k], v * 25);
            play(c, false, name);
            ++runs;
        }
    // Every size and material, a short spin and at hold, with full wobble and many.
    for (int size = 0; size < 5; ++size)
        for (int disk = 0; disk < 4; ++disk)
            for (int s = 0; s < 2; ++s) {
                double c[8] = {s ? 1.0 : 0.0, (size + 0.5) / 5, (disk + 0.5) / 4, 0.1667, 1.0, 1.0, 1.0, 1.0};
                char name[64];
                std::snprintf(name, sizeof name, "size %d, disk %d, spin %s", size, disk, s ? "hold" : "0");
                play(c, s == 0, name);
                ++runs;
            }
    if (failures) { std::printf("frames: %d faults\n", failures); return 1; }
    std::printf("frames: %d runs of 150 frames, every outline drawable\n", runs);
    return 0;
}
