#pragma once
#include <opxy/page_header.hpp>
#include "amp.h"
#include "chain.h"

namespace fermi {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 118, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const u32 ghost = 0,                    // the swing's ghost: four reshaping outlines, a quarter of the chain each
          header = 4,                   // the two-page header's eight bar steps
          mass = 12,                    // 32 bobs
          cone = 44, straight = 77,     // 33 coils of each kind: conical (quadratic) and straight (cubic)
          rod = 110,                    // the regulator pendulum's rod, on its knife edge
          stand_left = 111,             // the left stand, with the damper's pivot boss
          damper = 112,                 // the felt damper's lever
          pin = 113,                    // the seed's pin, pressing the bob it nudges hardest
          regulator = 114,              // the pendulum's sliding bob
          stand_right = 115,            // the right stand, with the pendulum's bracket
          plank_left = 116, plank_right = 117;   // the plank, in two halves that slide apart with the stands
}
// Palette entries (make_panel.py: PALETTE): the grey code, each part in the
// grey of its encoder.
namespace shade {
const u32 black = 0, ghost = 1, hard = 2, mass = 3, coil = 4, rod = 5, stand = 6, plank = 7, damper = 9,
          pin = 10, regulator = 11;
}
// Geometry (make_panel.py has the same), px.
namespace plate {
const i32 centre = 230, row_y = 124;          // the chain's middle and rest line
const i32 span = 210, span_step = 25;         // hook to hook at 8 masses, and each step of masses more
const i32 stand_out = 8;                      // a stand's rod outside its hook
const i32 plank_y = 200, plank_out = 30;      // the plank's halves hang from their ends, past the hooks
const i32 pendulum_out = 34, pendulum_y = 72; // the regulator's knife edge, right of the right hook
const i32 fast = 24, slow = 90;               // its bob's travel down the rod
const i32 pivot_y = 74;                       // the damper's lever pivot, on the left stand's top
const i32 lever = 170, pad = 8;               // the lever's reach to its felt pad, and the pad's height
const i32 bob = 5734;               // a bob's radius at unit scale, Q10 px (5.6)
const i32 coil = 32;                // a coil as authored
const i32 groove = 5734;            // a bob's rim from its centre, where the coils hook on, Q10 px (5.6) at unit scale
}

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    nonagon_amp::Follower amp;     // the track's amp envelope, as the screen follows it (amp.h)
    u32 initialized, track, serial;
    chain::Chain chain;            // the latest note's chain, run as its voice runs it
    u32 running;                   // 1 while the strobe holds the chain
    i32 linger;                    // Q10 s it runs on after the last key is let go
    u32 owed;                      // blocks of time owed, Q16
    u32 strobe;                    // the struck mode's velocity at the last step (its sign)
    i32 shape[chain::most];        // each mass's strain-scaled displacement at the last strobe, Q12
    i32 drawn[chain::most];        // eased toward the strobe, px Q8
    i32 shown[chain::most];        // as the weights stand on screen now, px Q8 (the coils follow these)
    i32 high[chain::most], low[chain::most];   // the ghost: the swing's recent extremes, px Q8
    i32 reach;                     // energy's draw: px a unit of strain, Q8, eased
    i32 slide;                     // the regulator's bob down its rod, px Q8, eased
    i32 swing;                     // its swing's amplitude, tenths of a degree, eased
    i32 damper;                    // the lever's turn, tenths of a degree, eased
    i32 lump;                      // the dispersion the weights are drawn for, Q15, eased
    i32 lift;                      // the seed's pin off its bob, px Q8, eased (4 px while the chain runs)
    u32 phase;                     // the pendulum's phase in its swing, tenths of a degree
    u32 rotor;                     // where the budgeted redraw resumes
    u32 keys[4];                   // the outlines' geometry keys
    u32 ghost_shown;
};
}
