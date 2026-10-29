#pragma once
#include <opxy/page_header.hpp>
#include "flight.h"

namespace mach {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 34, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const u32 bow = 0, tail = 1, reflected = 2, wake = 3,    // the reshaping outlines
          header = 4,                                    // the two-page header's eight bar steps
          ring = 12, rings = 12,                         // the sound waves
          projectile = 24, projectiles = 5,              // one for each source
          wire = 29, glow = 30, plate = 31,
          heard = 32, heards = 2;                        // where the sound heard now left
}

// What the photograph is drawn from: the controls, eased, or a flight's,
// fixed at its note-on as the engine fixes them.
struct Scene {
    i32 mach;        // Q16
    i32 distance;    // the closest pass in sound-seconds, Q16
    i32 scale;       // the photograph's scale: the whole flight spans the field, pixels a sound-second, Q4
    i32 reach;       // the closest pass on that scale: the listening wire's tip under the line, Q10 pixels
    i32 height;      // the plate under the wire's tip, Q10 pixels (echo)
    i32 echo;        // 1 with a plate
    i32 length;      // dur, Q16 seconds
    i32 tune;        // Q16, -1 .. 1
    i32 wave;        // the sound waves' spacing, Q10 pixels (20 to 10): the source's pitch, tighter as tune raises it
    i32 boom;        // Q15
    u32 source;
};

// A sound-wave ring or the projectile as drawn: Q10 pixels, and the ring's radius (0: hidden).
struct Mark { i32 x, y, r; };

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    Scene eased;               // the controls as the idle photograph shows them, eased
    Scene flight;              // the latest flight's, fixed at its note-on
    flight::Timeline line;     // ... its timeline
    i32 before;                // ... its approach, Q16 seconds of its own time before the pass
    i32 boom_at;               // seconds after note-on its boom is heard, Q16 (-1: none)
    i32 echo_cone;             // seconds after note-on the plate's cone arrives, Q16 (-1: none)
    u32 flying;                // 1 while a flight is shown
    i32 elapsed;               // Q10 seconds since its note-on
    i32 boom_age, echo_age;    // Q10 seconds since each boom flash began (-1: none)
    Mark ring[node::rings];
    Mark bullet;
    i32 shock[3][4];           // each shock outline as last drawn: -, -, its arms' lengths (whole pixels; -1 hidden)
    i32 shock_mach, shock_look;   // the cone and the look (stand-off, width) they were drawn at
    i32 wake[3];               // the wake as drawn: length (px), its eddies' phase step, 1 shown
    u32 key;                   // outline geometry keys
    u32 frames;                // frames drawn while flying (paces the wake's eddies)
};
}
