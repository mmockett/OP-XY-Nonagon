#pragma once
#include <opxy/page_header.hpp>

namespace ghost {
// Matches resources.display: paths, points, nodes, texts, palette (README, "Screen").
using Frame = opxy::draw::Frame<4, 64, 24, 16, 16>;

// Nodes: the apparition, its twin in the glass, its mouth and the chalk path
// it walks (the paths), the header's bar, its halos, two more apparitions for
// more held notes, the twin's own halos and mouth, the cover over the hint
// under the apron, and a black silhouette under each of the two figures.
// tools/make_panel.py draws them.
const opxy::u32 apparition = 0, twin = 1, mouth = 2, floor_path = 3, header_first = 4, halo_first = 12,
                halo_count = 3, extra_first = 15, twin_halo_first = 17, twin_mouth = 20, hint_cover = 21,
                mask = 22, twin_mask = 23;
// Where make_panel.py puts the stage, in pixels.
const opxy::i32 stage_front = 198, stage_back = 150, half_front = 170, half_back = 108;
const opxy::i32 front_feet = 187;   // the nearest feet: clear of the footlights' hoods (194) on the apron
const opxy::i32 path_x = 240, path_y = 174;   // the chalk path's origin, drawn at twice its points

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 track;
    opxy::u32 start;          // the apparition's start angle, from the note that began the hold
    opxy::u32 circled;        // how far it has circled, and trade's phase, while notes are held
    opxy::u32 trade;
    opxy::u32 presence;       // Q16: how present the apparition is, eased in and faded out
    opxy::u32 linger;         // ms the fade after release takes (ring's time, at most 2.5 s)
    opxy::i32 x, y, scale;    // its place on the stage (Q10), eased
    opxy::u32 beat_ms;        // time held, for the beat's tremble and the halos' breath
    opxy::u32 step_ms, step;  // the hem's ripple and the bed's scroll, a step every 100 ms while held
};
}
