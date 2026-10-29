#pragma once
#include <opxy/page_header.hpp>
#include "layout.h"

namespace recur {
// Matches resources.display: paths, points, nodes, texts, palette. The puzzle's
// tiles are fixed outlines that move, so it reshapes none.
using Frame = opxy::draw::Frame<0, 0, node_total, 16, 16>;

// A tile sliding from one slot to another; ms since it began, at_rest when it's still.
struct Slide { opxy::u8 from, to; opxy::u16 ms; };
const opxy::u16 at_rest = 0xffff;

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 track;
    opxy::u32 fresh;      // nothing drawn since reset: place everything at once
    opxy::u32 held_ms;    // ms the held note has played
    opxy::u32 bits;       // the grid whose tiles are shown
    opxy::u32 iteration;  // the iteration last drawn
    opxy::u32 turns;      // the key's ratchet: an iteration a click
    opxy::u32 hiss_key;   // what the tiles' inks were worked out for
    opxy::i32 swing;      // the pendulum's angle, tenths of a degree
    opxy::u32 sweep;      // where in the tray this frame's changes begin
    Slide slides[64];     // the shown grid's tiles, by their home slot
    opxy::u8 inks[64];    // each tile's shade: in hiss, how much of it is back in place
};
}
