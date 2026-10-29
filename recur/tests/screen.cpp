// Recur's screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>: every point of a print's pieces
// lies inside the square of the tile carrying it, at rest and at every frame
// of a scramble, on each grid and map; and every frame is valid.
#include "../display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
opxy::draw::SampleOverview overview;
}
const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

namespace {
struct Point { double x, y; };
// Each node's authored outline, read from the artwork beside this file.
std::vector<std::vector<Point>> outlines(recur::node_total);
int failures = 0;

bool read_artwork() {
    std::string path = __FILE__;
    path = path.substr(0, path.rfind("tests/")) + "panel.svg.fragment";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
    std::fclose(f);
    for (size_t at = 0; (at = text.find("id=\"recur_node_", at)) != std::string::npos;) {
        const u32 node = u32(std::atoi(text.c_str() + at + 15));
        const size_t end = text.find("/>", at), d = text.find(" d=\"", at);
        at = end;
        if (node >= recur::node_total || d == std::string::npos || d > end) continue;
        const char* p = text.c_str() + d + 4;
        while (*p && *p != '"') {
            while (*p == 'M' || *p == 'L' || *p == ' ') ++p;
            if (*p == 'Z') break;
            char* next;
            Point q;
            q.x = std::strtod(p, &next);
            q.y = std::strtod(next + 1, &next);
            outlines[node].push_back(q);
            p = next;
        }
    }
    return true;
}

i32 at(double v) { return i32(v * 2147483647.0); }
i32 choice_of(u32 n, u32 count) { return at((n + 0.5) / count); }

bool shown(const opxy::draw::Node& n) { return n.x != opxy::draw::hidden_x * opxy::draw::unit || n.y != opxy::draw::hidden_y * opxy::draw::unit; }
Point on_screen(const opxy::draw::Node& n, const Point& p) {
    const double s = n.scale / 1024.0;
    return Point{n.x / 1024.0 + p.x * s, n.y / 1024.0 + p.y * s};
}

// Every shown piece of the map's print against the square of its tile: its
// points at least 0.25 px inside the square's outline. Returns the worst miss.
double check_frame(const recur::Frame& frame, u32 map, u32 bits, const char* where) {
    double worst = -1e9;
    const u32 side = 1u << bits, shift = 3 - bits;
    for (u32 j = 0; j < recur::print_count[map]; ++j) {
        const u32 node = recur::print_first[map] + j;
        const opxy::draw::Node& piece = frame.nodes[node];
        if (!shown(piece)) continue;
        const u32 cell = recur::piece_cell[node - recur::print_first[0]];
        const u32 tile = ((cell >> 3) >> shift) * side + ((cell & 7) >> shift);
        const u32 square = (bits == 1 ? recur::large_first : recur::small_first) + tile;
        const opxy::draw::Node& sq = frame.nodes[square];
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const Point& p : outlines[square]) {
            const Point q = on_screen(sq, p);
            x0 = q.x < x0 ? q.x : x0; x1 = q.x > x1 ? q.x : x1;
            y0 = q.y < y0 ? q.y : y0; y1 = q.y > y1 ? q.y : y1;
        }
        for (const Point& p : outlines[node]) {
            const Point q = on_screen(piece, p);
            double out = x0 + 0.25 - q.x;
            out = q.x - (x1 - 0.25) > out ? q.x - (x1 - 0.25) : out;
            out = y0 + 0.25 - q.y > out ? y0 + 0.25 - q.y : out;
            out = q.y - (y1 - 0.25) > out ? q.y - (y1 - 0.25) : out;
            if (out > worst) worst = out;
            if (out > 0 && failures < 10) {
                std::printf("FAIL %s: map %u, grid %u, piece %u (cell %u) point (%.2f, %.2f) is %.2f px outside tile %u's square\n",
                            where, map, side, node, cell, q.x, q.y, out, tile);
                ++failures;
            }
        }
    }
    return worst;
}
}

int main() {
    if (!read_artwork()) { std::printf("FAIL: can't read panel.svg.fragment\n"); return 1; }
    overview.assigned = overview.loaded = 1;
    overview.frames = 176400; overview.rate = 44100; overview.begin = 0; overview.end = 176400;
    for (u32 b = 0; b < 64; ++b) overview.peaks[b] = opxy::u8(40 + (b * 37) % 200);
    static recur::DisplayState state;
    static recur::Frame frame;
    const double defaults[8] = {0, 0.5, 0.583333, 0.083333, 0, 0.4375, 0.7, 0.071429};
    double worst = -1e9;
    u32 frames = 0, invalid = 0;
    for (u32 map = 0; map < recur::map_count; ++map)
        for (u32 g = 0; g < recur::grid_count; ++g) {
            opxy::draw::Input input = {};
            for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
            input.slots[recur::kMap] = choice_of(map, recur::map_count);
            input.slots[recur::kGrid] = choice_of(g, recur::grid_count);
            input.slots[recur::kDepth] = at(1);
            input.slots[recur::kRate] = choice_of(7, recur::rate_count);   // an iteration a 1/16: a scramble each slice
            input.track = 1;
            input.elapsed = 17;
            frame = recur::Frame();
            recur::display_reset(state);
            const u32 bits = 1 + g;
            for (u32 f = 0; f < 30; ++f, ++frames) {
                recur::display(state, input, frame);
                invalid += !opxy::draw::valid(frame);
            }
            double w = check_frame(frame, map, bits, "at rest");
            worst = w > worst ? w : worst;
            // A held note through a whole period and more, every frame checked.
            input.note_serial = 1;
            input.held_notes = 1;
            const u32 period = recur::period(map, bits);
            for (u32 f = 0; f < period * 8 + 60; ++f, ++frames) {
                recur::display(state, input, frame);
                invalid += !opxy::draw::valid(frame);
                w = check_frame(frame, map, bits, "in a scramble");
                worst = w > worst ? w : worst;
            }
        }
    if (invalid) { std::printf("FAIL: %u invalid frames\n", invalid); ++failures; }
    std::printf("%u frames; every print piece inside its tile's square, at worst %.2f px inside its edge\n", frames, -worst);
    return failures ? 1 : 0;
}
