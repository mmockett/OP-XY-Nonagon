// The header's captions fit their columns on the OP-XY: each control's value,
// at 20 px in the device's font (Univers Next Pro Light), no wider than 53 px
// (55 px to the next column, 2 kept clear), and each title, at 10 px, no
// wider than 56. Every control swept finely over several settings of the
// others, and 200,000 settings at random. Widths are the font's advance
// widths, which the device lays out with no kerning. Run by sdk.py test.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
// Univers Next Pro Light's advance widths, thousandths of an em, for ASCII 32 to 126.
const int advance[95] = {280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560,
                         560, 560, 560, 560, 560, 560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602,
                         555, 715, 742, 275, 494, 621, 557, 944, 751, 745, 602, 745, 659, 617, 546, 744, 650, 1020, 638,
                         613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589, 574, 235, 235, 513,
                         235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
// Width in px at a size in px; -1 for a character outside ASCII.
double width(const char* text, int px) {
    int total = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) return -1;
        total += advance[*c - 32];
    }
    return total * px / 1000.0;
}

int failures = 0;
bool reported[8] = {};
const char* const names[8] = {"heat", "size", "liquid", "beat", "groove", "vapour", "life", "crackle"};

void check(const double* controls) {
    opxy::i32 slots[8];
    for (int k = 0; k < 8; ++k) slots[k] = static_cast<opxy::i32>(controls[k] * 2147483647.0);
    char values[8][opxy::draw::header_capacity];
    drop::captions(drop::physics::plate(slots), slots, values);
    for (int k = 0; k < 8; ++k) {
        const double w = width(values[k], 20);
        if ((w < 0 || w > 53 || !values[k][0]) && !reported[k]) {
            reported[k] = true;
            ++failures;
            std::printf("FAIL %s's caption '%s' is %.1f px wide (53 at most), at", names[k], values[k], w);
            for (int j = 0; j < 8; ++j) std::printf(" %.4f", controls[j]);
            std::printf("\n");
        }
    }
}
}

int main() {
    const char* const titles[8] = {"Heat", "Size", "Liquid", "Beat", "Groove", "Vapour", "Life", "Crackle"};
    for (const char* t : titles) {
        const double w = width(t, 10);
        if (w < 0 || w > 56) { std::printf("FAIL title '%s' is %.1f px wide (56 at most)\n", t, w); ++failures; }
    }
    // Each control swept finely, the others at their ends, their defaults, and each liquid.
    const double defaults[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25};
    double bases[9][8];
    for (int b = 0; b < 9; ++b)
        for (int k = 0; k < 8; ++k) bases[b][k] = b == 0 ? 0 : b == 1 ? 1 : defaults[k];
    const double liquids[7] = {0.25, 0.5, 0.6, 0.7, 0.75, 0.9, 1};
    for (int b = 2; b < 9; ++b) bases[b][drop::LIQUID] = liquids[b - 2];
    for (int k = 0; k < 8; ++k)
        for (int b = 0; b < 9; ++b)
            for (int i = 0; i <= 1024; ++i) {
                double c[8];
                std::memcpy(c, bases[b], sizeof c);
                c[k] = i / 1024.0;
                check(c);
            }
    // And at random.
    unsigned s = 12345;
    for (int n = 0; n < 200000; ++n) {
        double c[8];
        for (int k = 0; k < 8; ++k) { s = s * 1664525u + 1013904223u; c[k] = (s >> 8) / 16777216.0; }
        check(c);
    }
    if (!failures) std::puts("every caption and title fits its column");
    return failures ? 1 : 0;
}
