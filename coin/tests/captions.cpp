// Coin's header captions against the OP-XY's font, compiled for the host and
// run by python sdk.py test --machine <folder>. The header lays text out by
// Univers Next Pro Light's advance widths (no kerning): a value at 20 px may
// be at most 53 px wide before it runs into the next column, a title at 10 px
// at most 56. Every caption the screen can show is collected: each control
// swept finely with the rest at Euler's, air (which depends on spin and size)
// over all three, and ring (which depends on the disk) over every material,
// with either page edited.
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
// Univers Next Pro Light's advance widths for ' ' to '~', in thousandths of
// the size.
const int advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560,
    560, 560, 560, 560, 560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715,
    742, 275, 494, 621, 557, 944, 751, 745, 602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291,
    293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589, 574, 235, 235, 513, 235, 894, 574, 571,
    581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
const char* const titles[8] = {"spin", "size", "disk", "base", "wobble", "air", "ring", "many"};

double width(const std::string& text, double px) {
    double total = 0;
    for (char c : text) total += c >= ' ' && c <= '~' ? advance[c - ' '] : 1000;
    return total * px / 1000;
}

coin::DisplayState state;
coin::Frame frame;
std::set<std::string> seen;

void show(const double (&c)[8]) {
    for (int page = 0; page < 2; ++page) {
        opxy::draw::Input input;
        std::memset(&input, 0, sizeof input);
        for (int k = 0; k < 8; ++k) input.slots[k] = static_cast<opxy::i32>(c[k] * 2147483647.0 + (c[k] < 1 ? 0.5 : 0));
        for (int k = 0; k < 4; ++k) {
            input.parameters[k] = input.slots[k + 4 * page];
            input.shown[k] = static_cast<opxy::u8>(k + 4 * page);
        }
        coin::display_reset(state);
        coin::display(state, input, frame);
        for (unsigned t = 0; t < coin::Frame::text_count; ++t) seen.insert(frame.texts[t].chars);
    }
}
}

int main() {
    const double euler[8] = {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125};
    for (int k = 0; k < 8; ++k)
        for (int i = 0; i <= 1024; ++i) {
            double c[8];
            std::memcpy(c, euler, sizeof c);
            c[k] = i / 1024.0;
            show(c);
        }
    for (int s = 0; s <= 256; ++s)
        for (int z = 0; z < 5; ++z)
            for (int a = 0; a <= 16; ++a) {
                double c[8];
                std::memcpy(c, euler, sizeof c);
                c[0] = s / 256.0;
                c[1] = (z + 0.5) / 5;
                c[5] = a / 16.0;
                show(c);
            }
    for (int r = 0; r <= 512; ++r)
        for (int m = 0; m < 4; ++m) {
            double c[8];
            std::memcpy(c, euler, sizeof c);
            c[6] = r / 512.0;
            c[2] = (m + 0.5) / 4;
            show(c);
        }
    int failures = 0;
    double widest = 0;
    std::string which;
    for (const std::string& s : seen) {
        bool title = false;
        for (const char* t : titles) title = title || s == t;
        const double w = width(s, title ? 10 : 20), room = title ? 56 : 53;
        if (w > room) {
            std::printf("FAIL caption '%s' is %.1f px wide at %d px, over %.0f\n", s.c_str(), w, title ? 10 : 20, room);
            ++failures;
        }
        if (!title && w > widest) { widest = w; which = s; }
    }
    if (failures) return 1;
    std::printf("captions: %zu fit the header; the widest value, '%s', is %.1f of 53 px\n", seen.size(), which.c_str(), widest);
    return 0;
}
