#pragma once
#include <opxy/page_header.hpp>
#include "drink.h"

namespace cocoa {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 168, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const u32 vessel = 0, surface = 1, bowl = 2, handle = 3,  // the reshaping outlines
          header = 4,                                     // the two-page header's eight bar steps
          bubble = 12, bubbles = 40,                      // five to a bubble class
          foam = 52, foams = 20,                          // two rows on the surface
          pop = 72, pops = 4,
          handle_china = 76, handle_tin = 77,
          glint = 78, glints = 2,                         // glass
          chip = 80, chips = 5,                           // tin's chipped enamel
          hatch = 85, hatches = 3,                        // china's cut wall and base, hatched
          water = 88, froth = 108, waters = 20,           // the drink's lines, clear and frothy, bottom up
          fleck = 128, flecks = 10,                       // powder floating on the drink
          sediment = 138, sediments = 8,                  // powder left on the floor: a heap, then lumps
          shine = 146,                                    // the silver bowl's shine
          steam = 147, steams = 3,
          grain = 150, grains = 3,                        // the wooden spoon's grain: handle, handle, bowl
          shake = 153, shakes = 3,                        // strokes beside the bowl tapping fast
          ring = 156, rings = 3,                          // ripples on the floor where the bowl taps
          clump = 159, clumps = 5,                        // powder hanging in the drink
          swirl = 164, swirls = 4;                        // the stirred drink turning
}

// The mug in section, at 1.5 px a millimetre (make_panel.py has the same).
namespace scene {
const i32 cx = 250, bottom = 200, inner = 60;     // px: the axis, the inner floor, the inner radius
const i32 vessel_y = 140;                         // the vessel's node, drawn at twice
const i32 handle_mid = 115;                       // px from the spoon's tip to its handle's node
const i32 heap_x = -35;                           // px from the axis to the middle of the heap's foot
const i32 water_step = 6;                         // px between the drink's lines
}

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    i32 d[drink::classes], s[drink::classes];   // the drink, as the voices keep it (drink.h)
    i32 s0[drink::classes];                    // where each class's bubbles began at the last stir
    i32 owed;             // time not yet stepped, Q10 blocks
    i32 tail;             // Q10 s the voices sound on after the last key is let go
    u32 tap_phase;        // the spoon, as the voices' (Q32 cycles)
    i32 since_tap;        // Q10 s since the last tap
    i32 since_stir;       // Q10 s since the last stir
    i32 stirred;          // that stir's strength, Q15
    i32 surface;          // the surface's height as drawn, Q10 px
    i32 spoon;            // the spoon's shape as drawn (wood 0 to silver 65536)
    u32 cup;              // the vessel drawn
    u32 key;              // the outlines' geometry key
    u32 seed;             // the pops' random numbers
    u32 turn;             // where the bubbles' budget starts this frame
    i32 pop_age[4];       // each pop ring's age, Q10 s (negative when idle)
    i32 pop_x[4];         // and its place, Q10 px
    i32 ring_age[3];      // each floor ripple's age, Q10 s (negative when idle)
};
}
