#pragma once
#include <opxy/page_header.hpp>
#include "spin.h"

namespace coin {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 82, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const u32 disk = 0, edge = 1, reflection = 2, face = 3,   // the note's disk: the reshaping outlines
          header = 4,                                     // the two-page header's eight bar steps
          trail = 12, trails = 24,                        // the contact path's motion lines
          weight = 36,                                    // the eccentric weight on the rim
          glint = 37,                                     // the contact point
          grain = 38, grains = 5,                         // wood's grain
          speck = 43, specks = 6,                         // stone's four specks and two veins
          companion = 49,                                 // three disks for many: face, edge, reflection, glint
          air = 61, airs = 5,                             // the air squeezed from under the low edge
          ripple = 66, ripples = 8,                       // the ring's ripples across the mirror
          ghost = 74, ghosts = 8;                         // wobble's ghost of the rim, in dashes
}

// The projection (make_panel.py has the same): X right, Y away, Z up, pixels
// about the mirror's centre; the camera 16 degrees above, 1400 px away.
namespace scene {
const i32 cx = 240, cy = 136, distance = 1400;
const i32 sin_e = 4516, cos_e = 15749;   // Q14
const i32 reference = 77;                // the motion lines' radius as authored, px
const i32 ripple = 86;                   // the first ripple's radius as authored, px: 16 beyond size 1:8's rim
const i32 ripple_step = 14629;           // and the step out to each next, Q10 px (100 / 7)
const i32 reach = 186;                   // the last ripple's radius, px: inside the mirror's edge
const i32 dash = 24;                     // a ghost dash's length as authored, px
const i32 companion = 50;                // a companion disk's radius as authored, px
const i32 mirrored = 410;                // the concave mirror flattens its image to 0.4, Q10
}

// A disk as the screen keeps it: the same life the voice keeps (spin.h), and
// how it leans and turns on screen.
struct ScreenDisk {
    i32 life;
    u32 rolling;
    u32 precession;   // the contact point's way round on screen, Q32 turns
    i32 tilt;         // tenths of a degree, as drawn
};
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    ScreenDisk disk[spin::most];
    u32 count;        // the latest note's disks
    u32 poised;       // 1 until a note, and when spin turns between notes: the disks stand at their throw
    i32 spin;         // spin as last seen, Q16, to see it turn
    i32 owed;         // blocks of time not yet stepped, Q10
    u32 turn;         // the note's disk's own turn on screen, Q32 turns
    i32 radius;       // its radius as drawn, Q10 px
    i32 since;        // Q10 seconds since it stopped, for its ring
    u32 key;          // the outlines' geometry key
    u32 seed;         // the rattle's random numbers
    u32 frames;       // frames drawn, to step slow parts on alternate frames
    u32 breath;       // the air streaks' drift, Q32
};
}
