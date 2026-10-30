// Writes Sand's screen as JSON lines, one frame a line, for tools/make_gif.py:
// the real display code at 60 frames a second. Not part of the machine.
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tools/frames.cpp -o frames
//   ./frames gif <file>     every third frame of the GIF's scene
//   ./frames sheet <file>   each control at 0, 50 and 100, a note held 3 s
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace opxy::draw;
sand::DisplayState state;
sand::Frame frame;
Input input;
FILE* out;

void dump() {
    std::fprintf(out, "{\"nodes\":[");
    for (unsigned i = 0; i < sand::Frame::node_count; ++i) {
        const Node& n = frame.nodes[i];
        std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, unsigned(n.paint));
    }
    std::fprintf(out, "],\"paths\":[");
    for (unsigned p = 0; p < sand::Frame::path_count; ++p) {
        std::fprintf(out, "%s[", p ? "," : "");
        for (unsigned k = 0; k < sand::Frame::point_count; ++k)
            std::fprintf(out, "%s[%d,%d]", k ? "," : "", frame.paths[p].points[k].x, frame.paths[p].points[k].y);
        std::fprintf(out, "]");
    }
    std::fprintf(out, "],\"texts\":[");
    for (unsigned t = 0; t < sand::Frame::text_count; ++t) {
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
    sand::display_reset(state);
    input.track = 1;
    input.elapsed = 17;   // 60 frames a second, Q10 s
}
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    out = std::fopen(argv[2], "w");
    const double chladni[8] = {0.2083, 0.6, 0.6, 0.3, 0.1667, 0.5, 0, 0};
    if (std::string(argv[1]) == "sheet") {
        const double values[3] = {0.0, 0.5, 1.0};
        for (int k = 0; k < 8; ++k)
            for (double v : values) {
                double c[8];
                std::memcpy(c, chladni, sizeof c);
                c[k] = v;
                start();
                set(c, k / 4);
                for (int t = 0; t < 12; ++t) sand::display(state, input, frame);
                ++input.note_serial;
                input.held_notes = 1;
                for (int t = 0; t < 180; ++t) sand::display(state, input, frame);
                dump();
            }
    } else {
        // Chladni, a note gathering its sand; a second note as Sand rises to
        // full; a note with Shake up; then a note on a tipped plate.
        double c[8];
        std::memcpy(c, chladni, sizeof c);
        const int seconds = 16, on[4] = {6, 240, 510, 738}, off[4] = {222, 474, 690, 918};
        start();
        set(c, 0);
        sand::display(state, input, frame);
        int page = 0;
        for (int t = 0; t < seconds * 60; ++t) {
            for (int n = 0; n < 4; ++n) {
                if (t == on[n]) { ++input.note_serial; input.held_notes = 1; }
                if (t == off[n]) { ++input.note_off_serial; input.held_notes = 0; }
            }
            if (t >= 240 && t < 300) c[2] = 0.6 + 0.4 * (t - 240) / 60.0;   // Sand to full
            if (t == 492) page = 1;
            if (t >= 492 && t < 552) c[7] = (t - 492) / 60.0;                // Shake up
            if (t >= 696) c[7] = t < 708 ? 1 - (t - 696) / 12.0 : 0;         // and down
            if (t >= 708 && t < 780) c[6] = 0.8 * (t - 708) / 72.0;          // Tilt to 8 degrees
            if (t >= 912) c[6] = t < 950 ? 0.8 * (950 - t) / 38.0 : 0;       // and back
            set(c, page);
            sand::display(state, input, frame);
            if (t % 3 == 0) dump();
        }
    }
    std::fclose(out);
    return 0;
}
