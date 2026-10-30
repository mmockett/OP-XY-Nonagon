// Writes Coin's screen as JSON lines, one frame a line, for tools/make_gif.py:
// the real display code at 60 frames a second. Not part of the machine.
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tools/frames.cpp -o frames
//   ./frames gif <file>     every fifth frame of the GIF's scene
//   ./frames sheet <file>   each control at 0, 50 and 100, the disk stood at its throw
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace opxy::draw;
coin::DisplayState state;
coin::Frame frame;
Input input;
FILE* out;

void dump() {
    std::fprintf(out, "{\"nodes\":[");
    for (unsigned i = 0; i < coin::Frame::node_count; ++i) {
        const Node& n = frame.nodes[i];
        std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, unsigned(n.paint));
    }
    std::fprintf(out, "],\"paths\":[");
    for (unsigned p = 0; p < coin::Frame::path_count; ++p) {
        std::fprintf(out, "%s[", p ? "," : "");
        for (unsigned k = 0; k < coin::Frame::point_count; ++k)
            std::fprintf(out, "%s[%d,%d]", k ? "," : "", frame.paths[p].points[k].x, frame.paths[p].points[k].y);
        std::fprintf(out, "]");
    }
    std::fprintf(out, "],\"texts\":[");
    for (unsigned t = 0; t < coin::Frame::text_count; ++t) {
        const Text& x = frame.texts[t];
        std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", t ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::fprintf(out, "]}\n");
}
opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : opxy::i32(v * 2147483647.0); }
void set(const double* c, int page) {
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) {
        input.shown[k] = opxy::u8(page * 4 + k);
        input.parameters[k] = input.slots[page * 4 + k];
    }
}
void start() {
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    std::memset(&input, 0, sizeof input);
    coin::display_reset(state);
    input.track = 1;
    input.elapsed = 17;   // 60 frames a second, Q10 s
}
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    out = std::fopen(argv[2], "w");
    if (std::string(argv[1]) == "sheet") {
        const double euler[8] = {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125};
        const double values[3] = {0.0, 0.5, 1.0};
        for (int k = 0; k < 8; ++k)
            for (double v : values) {
                double c[8];
                std::memcpy(c, euler, sizeof c);
                c[k] = v == 0.5 && (k == 2 || k == 7) ? 0.625 : v;   // disk's glass, many's three
                start();
                set(c, k / 4);
                for (int t = 0; t < 40; ++t) coin::display(state, input, frame);
                dump();
            }
    } else {
        // Three steel disks spun and landing, then one big brass disk.
        double c[8] = {0.37, 0.5, 0.125, 0.1667, 0.45, 0.6, 0.854, 0.625};
        start();
        set(c, 0);
        coin::display(state, input, frame);
        for (int t = 0; t < 435; ++t) {
            if (t == 2 || t == 240) { ++input.note_serial; input.held_notes = 1; }
            if (t == 180 || t == 400) { ++input.note_off_serial; input.held_notes = 0; }
            if (t == 236) { c[1] = 0.7; c[2] = 0.375; c[7] = 0.125; set(c, 0); }
            coin::display(state, input, frame);
            if (t % 5 == 0) dump();
        }
    }
    std::fclose(out);
    return 0;
}
