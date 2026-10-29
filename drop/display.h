#pragma once
#include <opxy/page_header.hpp>
#include "leidenfrost.h"

namespace drop {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 58, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const u32 rim = 0,                      // each drop's rim: three reshaping outlines
          reflection = 3,               // the newest drop's reflection, the fourth
          header = 4,                   // the two-page header's eight bar steps
          underside = 12,               // each drop's vapour layer, its bright underside
          mirror = 15,                  // an engraver's reflection under each other drop
          spray = 18, sprays = 6,       // a boiling drop's spray
          spark = 24, sparks = 4,       // a floating drop's brief contacts
          stain = 28,                   // the water mark where the next drop lands
          teeth = 29,                   // the ratchet's teeth
          heat = 30, heats = 4,         // the heat's tempered rings on the iron (or frost)
          steam = 34,                   // each drop's steam, a plume as tall as it evaporates fast
          spatter = 37, spatters = 6,   // droplets round the newest drop, from its contacts
          shadow = 43,                  // each drop's shadow on the iron, which the vapour lifts it off
          mark = 46, marks = 12;        // spatter dried on the iron, as many as the crackle
}

// The scene (make_panel.py has the same): X right, Y away, Z up, mm about the
// floor's centre; the camera 30 degrees above, parallel projection.
namespace scene {
const i32 cx = 175, cy = 150;           // px: the floor's centre
const i32 scale = 1024;                 // the skillet, px a mm, Q10 (1.0)
const i32 drop_scale = 2662;            // the drops, px a mm, Q10 (2.6): drawn 2.6 times larger
const i32 sin_e = 512, cos_e = 887;     // Q10: 30 degrees
const i32 track_y = 35, track_r = 50;   // the ratchet's track, mm: its centre's Y and its radius
const i32 ring = 20;                    // px: the radius the rings are authored at
const i32 most = 3;                     // drops on screen
}

// A drop as the screen keeps it: the same physics the voice keeps.
struct ScreenDrop {
    physics::Drop drop;
    u32 used;          // 1 from its landing until it is gone
    u32 order;         // the note count it landed at: the newest has the reflection
    u32 breath;        // the star's breathing as drawn, slowed, Q32 turns
    u32 key;           // its outline's geometry key
    u32 pulse;         // its pulses' phase, Q32: the vapour flashes at each wrap
    i32 flash;         // Q10 s left of a flash
};
// A spark at a drop's edge, for a moment.
struct Spark {
    i32 left;          // Q10 s left to show
    u32 drop, angle;   // which drop, and where round it (tenths of a degree)
};
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    u32 count;                          // notes landed since reset
    i32 owed;                           // blocks not yet stepped, Q10
    ScreenDrop drops[scene::most];
    u32 reflection_key;
    u32 dice;                           // the sparks' random numbers
    Spark sparks[node::sparks];
};
}
