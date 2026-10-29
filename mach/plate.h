// Written by tools/make_panel.py: do not edit.
#pragma once
#include <opxy/types.hpp>

namespace mach {
namespace plate {
// The photograph's round field (pixels) and the card's window it shows through.
const opxy::i32 centre_x = 240, centre_y = 129, radius = 200;
const opxy::i32 top = 44, bottom = 216;
// The line of flight; the rings' authored radii (wave k's is (k + 1) times it) and the glow's.
const opxy::i32 line_y = 108, ring_radius = 10, glow_radius = 10;
// Each projectile's nose and tail (Q10 pixels ahead of its middle) and its bow shock's stand-off.
const opxy::i32 nose[5] = {24576, 21709, 20480, 9216, 28672};
const opxy::i32 tail[5] = {-24576, -22528, -20480, -9216, -28672};
const opxy::i32 standoff[5] = {38, 96, 70, 144, 16};   // Q4
}
}
