#pragma once
#include <opxy/sample_engine.hpp>
#include "recur.h"

namespace recur {
// The hiss path's reader of the window: chrd stacks up to three, each at its
// own pitch. It plays the square's samples in order, each gathered from where
// the map's iteration put it, and crossfades between two iterations (ka and kb).
struct Tap {
    u32 j, fraction;         // read position in the window, samples and a 16-bit fraction
    u32 step;                // window samples per output sample, Q16
    u32 offset, next;        // the window's start in the region this pass and the next, frames
    i32 shift;               // an iteration offset this pass, Q20
    u32 ka, kb, w;           // the iterations heard, and kb's share, Q16
    Mat ga, gb;              // their gathers: power() of the map's inverse
    u32 random;              // xorshift state
};

// The slice path's reader: where it reads in the sample, and the slice
// before it, fading out while this one fades in.
struct Reader {
    u32 frame, fraction;
    u32 tail_frame, tail_fraction;
    bool crossing;
};

// A voice: its sample, its readers and where the scramble has got to. Stock
// creates 256, so keep it small.
struct Voice {
    opxy::SampleView sample;
    u32 pitch, chord;        // the note's phase increment and chrd the steps were worked out for
    i32 cents;               // and the sample's tuning
    u32 root;                // the sample's step at its root key, Q16
    u32 steps[most_taps];    // each reader's samples per output sample, Q16
    u32 taps_playing;        // readers sounding
    bool fresh;              // no block rendered since the note started
    bool hiss;               // the hiss path plays (slice at hiss)
    u32 presence;            // 0..128: its level through a change of path, or the hiss path's square or map
    // The slice path.
    Reader readers[most_taps];
    u32 slot, into, length;  // the window's slot playing, samples into it, and its length
    u32 carry;               // the passes' lengths' fractions, Q8
    u32 iteration;           // iterations rate has moved on since the note began
    u32 counted;             // 64th notes since the last iteration
    Orbits orbits;           // which cells move at this depth, and which start a step ahead
    u32 moving_key, ahead_key;   // the map, square and depth, and step's fraction, they were worked out for
    Mat gather;              // the inverse map's power for one iteration
    u32 gather_key;
    // The hiss path.
    Tap taps[most_taps];
    u32 progress;            // rate's progress through the period, Q20
    u32 bits, map;           // the square and map playing; a change fades through silence
};

void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);
}
