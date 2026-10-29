// Butterfly's header captions against the OP-XY's columns, run by sdk.py
// test. The device lays the header out in Univers Next Pro Light by advance
// widths, without kerning: titles at 10 px from x 5 + 120 i, values at 20 px
// from x 65 + 120 i, the next title at 125 + 120 i and the last column ending
// at the screen's edge. So a value may be 53 px wide and a title 56, each
// keeping 2 px clear of its neighbour. Every caption each control can show,
// swept over 2^16 steps of its travel, and every title must fit.
#include "../display.cpp"
#include "../tables_data.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace butterfly;

// Univers Next Pro Light's advance widths for ' ' to '~', in its 1000 units
// an em (from the font's hmtx table).
const int advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};

// The width in pixels at `px`; -1 for a character the table does not hold.
double width(const char* text, double px) {
    double units = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < ' ' || *c > '~') return -1;
        units += advance[*c - ' '];
    }
    return units * px / 1000;
}
}

int main() {
    bool ok = true;
    double widest = 0, widest_title = 0;
    std::string at, at_title;
    for (int k = 0; k < 8; ++k) {
        const double t = width(screen::titles[k], 10);
        if (t < 0 || t > 56) {
            std::printf("FAIL  title '%s' is %.1f px wide, over 56\n", screen::titles[k], t);
            ok = false;
        }
        if (t > widest_title) {
            widest_title = t;
            at_title = screen::titles[k];
        }
        for (long v = 0; v <= 65536; ++v) {
            opxy::i32 p[8] = {};
            p[k] = static_cast<opxy::i32>(v >= 65536 ? 0x7fffffff : v << 15);
            char values[8][opxy::draw::header_capacity] = {};
            screen::captions(p, values);
            const char* c = values[k];
            const double w = width(c, 20);
            if (!opxy::draw::terminated(c, opxy::draw::header_capacity) || w < 0 || w > 53) {
                std::printf("FAIL  %s at %.4f: '%s' is %.1f px wide, over 53\n", screen::titles[k], v / 65536.0, c, w);
                ok = false;
                break;
            }
            if (w > widest) {
                widest = w;
                at = std::string(screen::titles[k]) + "'s '" + c + "'";
            }
        }
    }
    std::printf("%s  every caption fits its column in the OP-XY's font: the widest value %s, %.1f of 53 px; the widest title "
                "'%s', %.1f of 56\n",
                ok ? "ok  " : "FAIL", at.c_str(), widest, at_title.c_str(), widest_title);
    return ok ? 0 : 1;
}
