// Written by tools/make_panel.py: do not edit.
#pragma once
#include <opxy/types.hpp>

namespace mach {
namespace plate {
// The photograph's field (pixels), right of the data strip, and the closest pass.
const opxy::i32 left = 60, top = 44, right = 468, bottom = 216;
// The mask's left side, Q10 pixels: anything wholly right of it is hidden behind it.
const opxy::i32 mask = 478003;
const opxy::i32 centre_x = 288;
// The data strip's Mach scale: its line, and where M 0.3 and M 3 fall on it.
const opxy::i32 scale_x = 40, scale_bottom = 176, scale_top = 66;
// The line of flight; the rings' authored radii (wave k's is (k + 1) times it) and the glow's.
const opxy::i32 line_y = 130, ring_radius = 10, glow_radius = 10;
// Each projectile's nose and tail (Q10 pixels ahead of its middle) and its bow shock's stand-off.
const opxy::i32 nose[5] = {24576, 21709, 20480, 9216, 28672};
const opxy::i32 tail[5] = {-24576, -22528, -20480, -9216, -28672};
const opxy::i32 standoff[5] = {38, 96, 70, 144, 16};   // Q4
}
}
