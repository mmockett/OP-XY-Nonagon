#pragma once
#include <opxy/sample_engine.hpp>
#include "ghost.h"

namespace ghost {
// A second-order allpass section's state, and its coefficients: a2 = 1 - q
// and a1 = 2 * a1h, both Q31.
struct Section { i32 x1, x2, y1, y2; };
struct Coefficients { i32 q, a1h; };

// Each sounding note borrows a pair of resonator lines (one per ear) and
// the short lines that delay one ear, from a pool every Ghost voice shares:
// stock creates 256 voices, and 256 pairs would cost over half a megabyte.
const u32 line_length = 1024;   // the lowest note's period: every sample is written twice, a line apart, so reads never wrap
const u32 ear_tail = 16;        // each ear's last samples, for interaural delays up to 0.36 ms
const u32 pool_size = 16;       // notes that sound at once, on every track
struct Lines {
    opxy::i16 comb[2][2 * line_length];
    i32 tail[2][ear_tail];
};

// A voice: what it plays from, its resonators' state and where its
// apparition is. Stock creates 256, so keep it small.
struct Voice {
    opxy::SampleView sample;
    u32 step, frame, fraction;   // the bed: frames per output sample (Q16) and its read point
    u32 noise, random;           // xorshift: breath, and this note's own choices
    u32 slot;                    // 1 + the pool slot it holds, or 0
    u32 cycle;                   // the census cycle it last rendered in
    u32 write;                   // where its lines are next written
    i32 damp[2], slow[2];        // each resonator's loop low-pass, and its very slow average (DC, Q15 more)
    i32 air;                     // breath's gentle low-pass
    i32 formant[2][4];           // each ear's vowel: two filters, low and band each
    Section fundamental[2];      // split's top quarter: the odd ear's notch at the note
    u32 pulse[2];                // each ear's hummed voice's phase (the right's sharp by beat)
    u32 trade, circled;          // trade's phase, and how far the apparition has circled
    u32 start_angle;             // where round the head this note began
    i32 cents;                   // this note's own small detune, Q8 cents
    i32 shadow[2];               // each ear's head-shadow low-pass
    i32 delay[2], gain[2], cut[2];   // each ear's delay (Q16 samples), gain (Q12), shadow (Q15), last block
    u32 power, follow;           // the resonators' recent power, and the level follower's gain (Q12)
    i32 last_e[2];               // each ear's last half-rate excitation, to interpolate from
    u32 cached_increment, cached_period;   // the note's period (Q16 samples) and
    i32 cached_bright, cached_response;    // the loop's response there (Q15), for this note and vowel
    bool fresh;                  // a new note: clear its lines
    bool fresh_ears, fresh_power;    // and place its ears and level at once
};

void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);
}
