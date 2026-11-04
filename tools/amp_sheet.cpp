// Writes a machine's screen under a short and a long amp envelope as JSON
// lines, for tools/amp_sheet.py: the real display code at 60 frames a second,
// a note held a second and released, and the frames at the sheet's times.
// Not part of any machine. Built by amp_sheet.py as
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I<machine> -DMACHINE=<id> tools/amp_sheet.cpp
//   ./amp_sheet <file> <eight controls, 0..1> [<page>]
#include "display.cpp"
// A machine whose tables are defined once, beside its engine (Butterfly),
// links them here as its tests do.
#if defined(__has_include)
#if __has_include("tables_data.h")
#include "tables_data.h"
#endif
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace opxy::draw;
typedef MACHINE::DisplayState State;
typedef MACHINE::Frame Screen;
State state;
Screen frame;
Input input;
FILE* out;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : opxy::i32(v * 2147483647.0); }
void dump(const char* row, double t) {
    std::fprintf(out, "{\"row\":\"%s\",\"t\":%.2f,\"nodes\":[", row, t);
    for (unsigned i = 0; i < Screen::node_count; ++i) {
        const Node& n = frame.nodes[i];
        std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, unsigned(n.paint));
    }
    std::fprintf(out, "],\"paths\":[");
    for (unsigned p = 0; p < Screen::path_count; ++p) {
        std::fprintf(out, "%s[", p ? "," : "");
        for (unsigned k = 0; k < Screen::point_count; ++k)
            std::fprintf(out, "%s[%d,%d]", k ? "," : "", frame.paths[p].points[k].x, frame.paths[p].points[k].y);
        std::fprintf(out, "]");
    }
    std::fprintf(out, "],\"texts\":[");
    for (unsigned t = 0; t < Screen::text_count; ++t) {
        const Text& x = frame.texts[t];
        std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", t ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::fprintf(out, "]}\n");
}
}

int main(int argc, char** argv) {
    if (argc < 10) return 2;
    out = std::fopen(argv[1], "w");
    double c[8];
    for (int k = 0; k < 8; ++k) c[k] = std::atof(argv[2 + k]);
    const int page = argc > 10 ? std::atoi(argv[10]) : 0;
    // Stock's words (amp_curve.h), where a larger release is SHORTER: a short
    // envelope (instant attack, full sustain, release word 0x7f000000, about
    // 6 ms) and a long one (attack word 0x40000000, about 1.45 s; sustain 75%;
    // release word 0, stock's longest, about 55 s to -66 dB).
    const opxy::i32 envelopes[2][4] = {{0, 0x40000000, 0x7fffffff, 0x7f000000}, {0x40000000, 0x40000000, 0x60000000, 0}};
    const char* rows[2] = {"short", "long"};
    // Seconds from the note: held to 1.0 s, then released.
    const double times[7] = {0.1, 0.4, 0.9, 1.1, 1.5, 2.5, 4.0};
    for (int e = 0; e < 2; ++e) {
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        std::memset(&input, 0, sizeof input);
        display_reset(state);
        for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
        for (int k = 0; k < 4; ++k) {
            input.shown[k] = opxy::u8(page * 4 + k);
            input.parameters[k] = input.slots[page * 4 + k];
        }
        input.track = 1;
        input.elapsed = 17;  // 60 frames a second, Q10 s
        input.amp.attack = envelopes[e][0];
        input.amp.decay = envelopes[e][1];
        input.amp.sustain = envelopes[e][2];
        input.amp.release = envelopes[e][3];
        for (int t = 0; t < 12; ++t) display(state, input, frame);
        ++input.note_serial;
        input.held_notes = 1;
        int next = 0;
        for (int t = 1; next < 7; ++t) {
            if (t == 60) { ++input.note_off_serial; input.held_notes = 0; }
            display(state, input, frame);
            if (t == int(times[next] * 60 + 0.5)) dump(rows[e], times[next++]);
        }
    }
    std::fclose(out);
    return 0;
}
