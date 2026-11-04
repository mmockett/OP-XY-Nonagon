// Recur's screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>: every point of a print's pieces
// lies inside the square of the tile carrying it, at rest and at every frame
// of a scramble, on each grid and map; every outline keeps to what the OP-XY
// can draw, across a sweep of every control on each map, the scrambles and
// the cover with no sample; and every frame is valid.
#include "../display.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
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

// What the OP-XY does with an outline beyond the SDK's rules: it merges
// neighbouring points within an eighth of a pixel, draws a closing edge under
// a quarter of a pixel as a sliver, and draws an outline with no area (a line
// traced out and back, or a turn straight back at a point) as a streak. Each
// outline is checked as authored and, for every shown node, as drawn at its
// scale. Returns false on the first that breaks a rule.
bool outline_ok(const std::vector<Point>& pts, double scale, u32 node, const char* where) {
    const size_t n = pts.size();
    if (n < 3) return true;
    double area = 0;
    for (size_t i = 0; i < n; ++i) {
        const Point &a = pts[(i + n - 1) % n], &b = pts[i], &c = pts[(i + 1) % n];
        const double ix = (b.x - a.x) * scale, iy = (b.y - a.y) * scale, ox = (c.x - b.x) * scale, oy = (c.y - b.y) * scale;
        const double in = std::sqrt(ix * ix + iy * iy), out = std::sqrt(ox * ox + oy * oy);
        area += (a.x * b.y - b.x * a.y) * scale * scale;
        if (out < 0.125) {
            std::printf("FAIL %s: node %u points %zu and %zu are %.3f px apart and merge\n", where, node, i, (i + 1) % n, out);
            return false;
        }
        if (i + 1 == n && out < 0.25) {
            std::printf("FAIL %s: node %u closes over %.3f px\n", where, node, out);
            return false;
        }
        if ((ix * ox + iy * oy) / (in * out) < -0.999) {
            std::printf("FAIL %s: node %u turns straight back at point %zu\n", where, node, i);
            return false;
        }
    }
    if (std::fabs(area) / 2 < 0.1) {
        std::printf("FAIL %s: node %u has no area (%.3f px^2)\n", where, node, std::fabs(area) / 2);
        return false;
    }
    return true;
}

// The header's captions in the OP-XY's face, Univers Next Pro Light: each
// character's advance in thousandths of an em (the device lays text out by
// advance, without kerning), for the characters Recur writes. A value at
// 20 px must fit 53 px before the next column's title, a title at 10 px 56 px.
struct Advance { char c; int width; };
const Advance advances[] = {
    {' ', 280}, {'%', 915}, {'+', 676}, {'-', 329}, {'.', 280}, {'/', 293}, {'0', 560}, {'1', 560}, {'2', 560},
    {'3', 560}, {'4', 560}, {'5', 560}, {'6', 560}, {'7', 560}, {'8', 560}, {'9', 560}, {'a', 536}, {'b', 581},
    {'c', 522}, {'d', 581}, {'e', 532}, {'f', 318}, {'g', 589}, {'h', 574}, {'i', 235}, {'j', 235}, {'k', 513},
    {'l', 235}, {'m', 894}, {'n', 574}, {'o', 571}, {'p', 581}, {'q', 581}, {'r', 345}, {'s', 480}, {'t', 325},
    {'u', 574}, {'v', 512}, {'w', 820}, {'x', 506}, {'y', 507}, {'z', 445}};
// A caption's width in px at size px, or a negative width for a character not in the table.
double caption_width(const char* text, int px) {
    int total = 0;
    for (; *text; ++text) {
        int w = -100000;
        for (const Advance& a : advances) if (a.c == *text) w = a.width;
        total += w;
    }
    return total * px / 1000.0;
}
std::set<std::string> values_seen, titles_seen;
void collect_captions(const recur::Frame& frame) {
    for (u32 t = 0; t < 16; ++t) ((t & 7) < 4 ? titles_seen : values_seen).insert(frame.texts[t].chars);
}

// The reshaping outlines (the tape's waveform and the frame round the stretch
// read) as the frame shapes them, rather than as authored.
std::vector<Point> reshaped(const recur::Frame& frame, u32 node) {
    std::vector<Point> pts;
    for (const opxy::draw::Point& q : frame.paths[node].points) pts.push_back(Point{q.x / 1024.0, q.y / 1024.0});
    return pts;
}

bool outlines_ok(const recur::Frame& frame, const char* where) {
    for (u32 node = 0; node < recur::node_total; ++node) {
        if (!shown(frame.nodes[node])) continue;
        const std::vector<Point>& pts = node < recur::paths ? reshaped(frame, node) : outlines[node];
        if (!outline_ok(pts, frame.nodes[node].scale / 1024.0, node, where)) {
            ++failures;
            return false;
        }
    }
    return true;
}
}

int main() {
    if (!read_artwork()) { std::printf("FAIL: can't read panel.svg.fragment\n"); return 1; }
    overview.assigned = overview.loaded = 1;
    overview.frames = 176400; overview.rate = 44100; overview.begin = 0; overview.end = 176400;
    overview.generation = 1;
    for (u32 b = 0; b < 96; ++b) { const opxy::i16 peak = opxy::i16((40 + (b * 37) % 200) << 7); overview.high[b] = peak; overview.low[b] = opxy::i16(-peak); }
    static recur::DisplayState state;
    static recur::Frame frame;
    const double defaults[8] = {0, 0.5, 0.583333, 0.083333, 0, 0.4375, 0.7, 0.071429};
    double worst = -1e9;
    u32 frames = 0, invalid = 0;
    // Every outline as authored, then as drawn: each control from 0 to 1 by
    // quarters on each map at rest, and the cover with no sample.
    for (u32 node = 0; node < recur::node_total; ++node)
        if (!outline_ok(outlines[node], 1, node, "as authored")) ++failures;
    for (u32 map = 0; map < recur::map_count && !failures; ++map)
        for (u32 c = 0; c < 8 && !failures; ++c)
            for (u32 v = 0; v <= 4; ++v) {
                opxy::draw::Input input = {};
                for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
                input.slots[recur::kMap] = choice_of(map, recur::map_count);
                input.slots[c] = c == recur::kMap ? choice_of(v, recur::map_count) : at(v * 0.25);
                input.track = 1;
                input.elapsed = 17;
                frame = recur::Frame();
                recur::display_reset(state);
                for (u32 f = 0; f < 30; ++f, ++frames) recur::display(state, input, frame);
                char where[64];
                std::snprintf(where, sizeof where, "map %u, control %u at %u%%", map, c, v * 25);
                if (!outlines_ok(frame, where)) break;
            }
    {
        opxy::draw::Input input = {};
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        input.track = 1;
        input.elapsed = 17;
        overview.assigned = 0;
        frame = recur::Frame();
        recur::display_reset(state);
        for (u32 f = 0; f < 5; ++f, ++frames) recur::display(state, input, frame);
        outlines_ok(frame, "with no sample");
        overview.assigned = 1;
    }
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
                if (!failures) outlines_ok(frame, "in a scramble");
            }
        }
    // Every caption the header can show: each control swept in hundredths on
    // each map, grid and slice (with the sample from a tenth of a second to
    // twenty minutes long), and with no sample.
    {
        const u32 lengths[] = {4410, 176400, 1323000, 8820000, 52920000};
        for (u32 map = 0; map < recur::map_count; ++map)
            for (u32 g = 0; g < recur::grid_count; ++g)
                for (u32 sl = 0; sl < recur::slice_count; ++sl)
                    for (u32 length : lengths)
                        for (u32 c = 0; c < 8; ++c) {
                            if ((c == recur::kMap || c == recur::kGrid || c == recur::kSlice) && length != lengths[1]) continue;
                            opxy::draw::Input input = {};
                            for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
                            input.slots[recur::kMap] = choice_of(map, recur::map_count);
                            input.slots[recur::kGrid] = choice_of(g, recur::grid_count);
                            input.slots[recur::kSlice] = choice_of(sl, recur::slice_count);
                            input.track = 1;
                            input.elapsed = 17;
                            overview.frames = overview.end = length;
                            for (u32 v = 0; v <= 100; ++v) {
                                input.slots[c] = at(v / 100.0);
                                recur::display(state, input, frame);
                                ++frames;
                                collect_captions(frame);
                            }
                        }
        // Shift's row (stockShift): start, window and end in seconds, on
        // either page, for samples of every length trimmed anywhere.
        for (u32 length : lengths)
            for (u32 a = 0; a <= 20; ++a)
                for (u32 b = a; b <= 20; ++b)
                    for (u32 page = 0; page < 2; ++page) {
                        opxy::draw::Input input = {};
                        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
                        for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(4 * page + i);
                        input.track = 1;
                        input.elapsed = 17;
                        input.shift = 1;
                        overview.frames = length;
                        overview.begin = u32((unsigned long long)(length) * a / 20);
                        overview.end = u32((unsigned long long)(length) * b / 20);
                        recur::display(state, input, frame);
                        ++frames;
                        collect_captions(frame);
                    }
        overview.begin = 0;
        overview.frames = overview.end = 176400;
        overview.assigned = 0;
        opxy::draw::Input input = {};
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        input.track = 1;
        for (u32 v = 0; v <= 100; ++v) {
            input.slots[recur::kFit] = at(v / 100.0);
            recur::display(state, input, frame);
            collect_captions(frame);
        }
        overview.assigned = 1;
        double widest = 0, widest_title = 0;
        for (const std::string& s : values_seen) {
            const double w = caption_width(s.c_str(), 20);
            if (std::getenv("CAPTIONS")) std::printf("%s\n", s.c_str());
            if (w < 0 || w > 53) { std::printf("FAIL: the caption '%s' is %.1f px wide at 20 px, over 53\n", s.c_str(), w); ++failures; }
            widest = w > widest ? w : widest;
        }
        for (const std::string& s : titles_seen) {
            const double w = caption_width(s.c_str(), 10);
            if (std::getenv("CAPTIONS")) std::printf("title: %s\n", s.c_str());
            if (w < 0 || w > 56) { std::printf("FAIL: the title '%s' is %.1f px wide at 10 px, over 56\n", s.c_str(), w); ++failures; }
            widest_title = w > widest_title ? w : widest_title;
        }
        std::printf("%zu captions, the widest %.1f of 53 px; %zu titles, the widest %.1f of 56 px\n", values_seen.size(), widest,
                    titles_seen.size(), widest_title);
    }
    {   // Shift (stockShift): the header's row names the stock sampler's start,
        // window, gain and end, with their times and the gain in stock's steps,
        // and the trim's markers on the tape light up.
        overview.assigned = overview.loaded = 1;
        overview.frames = 176400; overview.begin = 22050; overview.end = 154350;
        opxy::draw::Input input = {};
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(i);
        input.track = 1;
        input.elapsed = 17;
        frame = recur::Frame();
        recur::display_reset(state);
        for (u32 f = 0; f < 30; ++f) recur::display(state, input, frame);
        const auto text = [&](u32 slot) { return std::string(frame.texts[slot].chars); };
        if (text(0) != "fit" || text(4) != "0") { std::printf("FAIL: without Shift the header shows fit\n"); ++failures; }
        const u32 unlit = frame.nodes[recur::mark_first].shade;
        input.shift = 1;
        for (u32 f = 0; f < 60; ++f) recur::display(state, input, frame);
        if (text(0) != "start" || text(1) != "window" || text(2) != "gain" || text(3) != "end" ||
            text(4) != "0.50s" || text(5) != "3.00s" || text(6) != "+0" || text(7) != "3.50s" || std::strcmp(frame.header[2], "+0")) {
            std::printf("FAIL: Shift's row reads %s %s %s %s / %s %s %s\n", text(0).c_str(), text(1).c_str(), text(2).c_str(),
                        text(3).c_str(), text(4).c_str(), text(5).c_str(), text(7).c_str());
            ++failures;
        }
        const i32 x0 = frame.nodes[recur::mark_first].x / 1024, x1 = frame.nodes[recur::mark_first + 1].x / 1024;
        const i32 eighth = (recur::rule_x1 - recur::rule_x0) / 8;
        if (std::abs(x0 - (recur::rule_x0 + eighth)) > 1 || std::abs(x1 - (recur::rule_x1 - eighth)) > 1) {
            std::printf("FAIL: the trim's markers stand at %d..%d, not the trim\n", x0, x1);
            ++failures;
        }
        if (frame.nodes[recur::mark_first].shade <= unlit) { std::printf("FAIL: Shift doesn't light the trim's markers\n"); ++failures; }
        for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(4 + i);
        for (u32 f = 0; f < 30; ++f) recur::display(state, input, frame);
        if (text(0) != "start" || text(2) != "gain" || text(6) != "+0" || text(3) != "end") {
            std::printf("FAIL: page 2's Shift row reads %s .. %s %s\n", text(0).c_str(), text(2).c_str(), text(3).c_str());
            ++failures;
        }
        // The gain as stock steps it, every value within the caption's width; and
        // with no gain known (gain_unavailable), its cell shows nothing.
        for (i32 g = -30; g <= 20; ++g) {
            overview.gain = g;
            recur::display(state, input, frame);
            char expect[8];
            std::snprintf(expect, sizeof expect, "%c%d", g < 0 ? '-' : '+', g < 0 ? -g : g);
            if (text(6) != expect) { std::printf("FAIL: gain %d reads '%s'\n", g, text(6).c_str()); ++failures; }
            const double w = caption_width(frame.texts[6].chars, 20);
            if (w < 0 || w > 53) { std::printf("FAIL: the gain caption '%s' is %.1f px wide\n", frame.texts[6].chars, w); ++failures; }
        }
        overview.gain = opxy::draw::gain_unavailable;
        recur::display(state, input, frame);
        if (text(2) != "" || text(6) != "" || frame.header[2][0]) { std::printf("FAIL: an unknown gain shows '%s' '%s'\n", text(2).c_str(), text(6).c_str()); ++failures; }
        overview.gain = 0;
        overview.begin = 0; overview.end = 176400;
    }
    {   // The gain scales the tape's waveform: taller as it rises (the loudest
        // bins meet the slot's edge from about +10), never past the slot.
        opxy::draw::Input input = {};
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        input.track = 1;
        input.elapsed = 17;
        double last = 0;
        bool rising = true, inside = true;
        for (i32 g = -30; g <= 20; g += 10) {
            overview.gain = g;
            frame = recur::Frame();
            recur::display_reset(state);
            recur::display(state, input, frame);
            double tallest = 0;
            for (u32 h = 0; h < recur::waves; ++h)
                for (const opxy::draw::Point& q : frame.paths[h].points) tallest = std::fmax(tallest, std::fabs(q.y / 1024.0));
            rising = rising && (g <= 0 ? tallest > last + 0.2 : tallest >= last);
            inside = inside && tallest < 7.5;
            last = tallest;
        }
        if (!rising || !inside) { std::printf("FAIL: the waveform doesn't grow with the gain within the slot\n"); ++failures; }
        overview.gain = opxy::draw::gain_unavailable;
        frame = recur::Frame();
        recur::display_reset(state);
        recur::display(state, input, frame);
        recur::Frame unknown = frame;
        overview.gain = 0;
        frame = recur::Frame();
        recur::display_reset(state);
        recur::display(state, input, frame);
        bool same = true;
        for (u32 h = 0; h < recur::waves; ++h)
            for (u32 i = 0; i < 64; ++i) same = same && unknown.paths[h].points[i].y == frame.paths[h].points[i].y;
        if (!same) { std::printf("FAIL: an unknown gain changes the waveform\n"); ++failures; }
    }
    {   // The tape: the grid's cuts stand where the engine cuts the trim, a long
        // tick at each row, between the trim's markers; and they don't move at rest.
        overview.frames = 352800; overview.begin = 0; overview.end = 88200;   // bar 1 of the break
        for (u32 g = 0; g < recur::grid_count; ++g) {
            opxy::draw::Input input = {};
            for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
            input.slots[recur::kGrid] = choice_of(g, recur::grid_count);
            input.track = 1;
            input.elapsed = 17;
            frame = recur::Frame();
            recur::display_reset(state);
            for (u32 f = 0; f < 60; ++f) recur::display(state, input, frame);
            const u32 bits = 1 + g, side = 1u << bits, cells = side * side;
            u32 ticks = 0;
            bool placed = true;
            for (u32 n = recur::row_first; n < recur::cut_first + recur::cut_count; ++n) {
                if (!shown(frame.nodes[n])) continue;
                ++ticks;
                // Its x is a cut: span * c / cells, as a px of the 312 the whole sample spans.
                const double x = frame.nodes[n].x / 1024.0 - recur::rule_x0, per = 88200.0 / cells * 312 / 352800;
                const double c = x / per;
                placed = placed && std::fabs(c - std::round(c)) < 0.05 && c > 0.5 && c < cells - 0.5;
            }
            // At 8 x 8 a bar's 64 slices are 1.2 px apart on the tape: its rows show, not its slices.
            const u32 expect = g == 2 ? side - 1 : cells - 1;
            if (ticks != expect || !placed) { std::printf("FAIL: %u x %u shows %u cuts on the tape (expected %u)%s\n", side, side, ticks, expect, placed ? "" : ", off the slices"); ++failures; }
            recur::Frame before = frame;
            recur::display(state, input, frame);
            if (std::memcmp(&before, &frame, sizeof frame)) { std::printf("FAIL: the screen changes at rest\n"); ++failures; }
        }
    }
    {   // fit shows on the tape: the frame round the stretch the slot reads
        // spans its slot at the note's pitch at fit 0 (two slices for 1/8s of
        // a bar on 4 x 4) and its own slice at fit 100.
        overview.frames = 352800; overview.begin = 0; overview.end = 88200;
        double widths[2];
        for (u32 fit = 0; fit < 2; ++fit) {
            opxy::draw::Input input = {};
            for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
            input.slots[recur::kSlice] = choice_of(4, recur::slice_count);   // 1/8
            input.slots[recur::kFit] = at(fit);
            input.track = 1;
            input.elapsed = 17;
            frame = recur::Frame();
            recur::display_reset(state);
            for (u32 f = 0; f < 30; ++f) recur::display(state, input, frame);
            widths[fit] = frame.paths[recur::reading].points[16].x / 1024.0;
        }
        const double slice_px = 88200.0 / 16 * 312 / 352800;
        if (std::fabs(widths[0] - 2 * slice_px) > 0.6 || std::fabs(widths[1] - slice_px) > 0.3) {
            std::printf("FAIL: the reading frame spans %.2f px at fit 0 and %.2f at fit 100, not %.2f and %.2f\n", widths[0], widths[1],
                        2 * slice_px, slice_px);
            ++failures;
        }
        std::printf("fit on the tape: the stretch read %.1f px at fit 0, %.1f at fit 100\n", widths[0], widths[1]);
        overview.frames = 176400; overview.begin = 0; overview.end = 176400;
    }
    {   // The amp envelope (input.amp, SDK 1.10): the scramble and the pendulum
        // play on while the note sounds. Unknown, they stop as the note is let
        // go, as before; with a long release they keep going, the pendulum
        // narrowing; a short one stops them at once; a long attack swells the
        // pendulum's swing in.
        overview.frames = 352800; overview.begin = 0; overview.end = 88200;
        struct Run { u32 held_ms; i32 swing; };
        auto play = [&](const opxy::draw::Envelope& amp, u32 held_frames, u32 after_frames) {
            opxy::draw::Input input = {};
            for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
            input.slots[recur::kSlice] = choice_of(5, recur::slice_count);   // 1/4: the pendulum swings its widest
            input.track = 1; input.elapsed = 17; input.amp = amp;
            frame = recur::Frame();
            recur::display_reset(state);
            recur::display(state, input, frame);
            input.note_serial = 1; input.held_notes = 1;
            i32 widest = 0;
            for (u32 f = 0; f < held_frames; ++f) { recur::display(state, input, frame); widest = std::max(widest, std::abs(state.swing)); }
            input.held_notes = 0; input.note_off_serial = 1;
            for (u32 f = 0; f < after_frames; ++f) recur::display(state, input, frame);
            return Run{state.held_ms, after_frames ? std::abs(state.swing) : widest};
        };
        const i32 unknown = opxy::draw::envelope_unavailable;
        const opxy::draw::Envelope none = {unknown, unknown, unknown, unknown}, quick = {0, 0, 0x7fffffff, 0},
                                   long_release = {0, 0, 0x7fffffff, 0x66666666}, long_attack = {0x66666666, 0, 0x7fffffff, 0};
        const Run a = play(none, 120, 30), b = play(quick, 120, 30), c = play(long_release, 120, 30), d = play(quick, 15, 0), e = play(long_attack, 15, 0);
        std::printf("envelope: half a second after release, %u ms of motion (unknown), %u (short release), %u (long); "
                    "the swing a quarter second in %d (fast attack) and %d (slow) tenths of a degree\n", a.held_ms, b.held_ms, c.held_ms, d.swing, e.swing);
        if (a.held_ms || b.held_ms) { std::printf("FAIL: the motion outlasts a note with no or a short release\n"); ++failures; }
        if (c.held_ms < 2400) { std::printf("FAIL: a long release doesn't keep the motion going\n"); ++failures; }
        if (e.swing * 2 > d.swing) { std::printf("FAIL: a long attack doesn't swell the swing in\n"); ++failures; }
        overview.frames = 176400; overview.begin = 0; overview.end = 176400;
    }
    if (invalid) { std::printf("FAIL: %u invalid frames\n", invalid); ++failures; }
    std::printf("%u frames; every print piece inside its tile's square, at worst %.2f px inside its edge\n", frames, -worst);
    return failures ? 1 : 0;
}
