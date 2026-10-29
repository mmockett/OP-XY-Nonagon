#pragma once
#include <opxy/page_header.hpp>
#include "harper.h"
#include "plate.h"

namespace butterfly {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, plate::nodes, 16, 16>;

// The screen cannot hear the voices. It reads the same tables as the engine
// from the same controls (harper.h) and follows the latest note's strike
// with the engine's own gate and wingbeat.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    u32 eased_alpha;          // the flux as drawn, easing to the controls' (Q32 turns)
    i32 eased_lambda, eased_site;   // Q12, Q16
    i32 gate;                 // the latest strike's vactrol, Q30
    u32 blocks;               // since it, in blocks
    u32 beat, beat_site;      // its wingbeat's phases
    i32 since;                // Q10 seconds since the wings were last solved
    i32 flick;                // Q10 seconds since the antennae were flicked, while they move
    u32 keys[4];              // the outlines' geometry keys
    i32 reach[plate::margin]; // the margin's reaches as drawn, Q12
    i32 lit_share;            // the lit band's share of the margin's reach (the struck site), Q15
    i32 scale;                // the wings' scale about their roots, easing to the controls', Q15
    i32 drawn_scale[2];       // each wing's scale as its nodes were last placed (0: place them)
    i32 sweep;                // the forewing's sweep back about the root (tilt), tenths of a degree, easing
    i32 tail;                 // the tail's length (glow), Q10 px, easing
    i32 lit_strike;           // the strike's knob as the lit band shows it, Q15, easing
    i32 drawn_sweep[2], drawn_tail[2], drawn_strike[2];   // as each wing's nodes were last placed
    u32 dirty;                // wings whose nodes must be placed again (bit 0 left, bit 1 right)
    u32 turn;                 // which wing may move next
    u32 drawn_light;          // the light's step as drawn
    i32 spot_x[plate::eyes_a_wing], spot_y[plate::eyes_a_wing];   // eyespots on the left wing, Q10 px from the centre
    i32 spot_scale[plate::eyes_a_wing];
    u32 spot_state[plate::eyes_a_wing];                            // 0 hidden
    i32 spot_light[plate::eyes_a_wing];                            // how much the strike excites it, Q15
    i32 spot_fore[plate::eyes_a_wing];                             // how much it turns with the forewing, Q15
};
}
