#pragma once
#include <opxy/page_header.hpp>
#include "sand.h"
#include "view.h"

namespace sand {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, node_total, 16, 16>;

// Node numbers and the view are in view.h (tools/make_plate.py).
const u32 grains_shown = 48, bow_parts = 4, materials = 3;
// The bow's stroke over the plate while notes are held: its longest (px).
const i32 stroke_most = 40;
// Palette greys (0 black .. 15 white): the nodal lines; a grain at rest,
// moving and in the air; each bow part's grey idle and while bowing; the
// plate's edge for metal, glass and wood.
const u32 line_grey = 8, rest_grey = 11, moving_grey = 13, aloft_grey = 15;
const u32 bow_grey[bow_parts][2] = {{9, 11}, {9, 11}, {8, 10}, {10, 13}};
const u32 edge_grey[materials] = {9, 13, 7};
// The nodal lines scribed on a bare plate.
const u32 bare_grey = 4;

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    Grain grains[grains_shown];
    u32 started;    // the first frame has set every element
    u32 serial;     // the note-start serial last seen
    u32 seed;
    i32 swing;      // the bowed mode's amplitude on the drawn plate, as bowed (amplitude units)
    i32 ring;       // and as struck, ringing down (amplitude units)
    u32 jolted;     // looks left of a strike's jolt
    i32 clock;      // time owed to the sand, Q20 seconds
    i32 since;      // Q10 seconds since the last note, for hosts without note activity
    i32 stroke;     // how far the bow has drawn over the plate, Q10 px
    i32 tilt;       // the plate's tilt, Q10 tenths of a degree (negative: its far edge down)
    i32 struck;     // Q10 seconds since the mallet last struck
    u32 frames;     // frames drawn: moving grains are redrawn on alternate frames
};
}
