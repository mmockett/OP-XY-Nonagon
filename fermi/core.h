#pragma once
#include <opxy/audio.hpp>
#include "chain.h"

namespace fermi {
// One note: its own chain, and a partial for each of the chain's lowest
// twelve modes.
struct Voice {
    chain::Chain chain;
    u32 phase[chain::voiced];   // Q32 cycles
    i32 amp[chain::voiced];     // the partial's level now, Q16 << 15
    u32 level[chain::voiced];   // the mode's amplitude, sqrt of its share of the energy, eased, Q16
    u32 plain[chain::voiced];   // 1 while the partial reads a plain sine rather than the body
    u32 inc[chain::voiced];     // the partial's increment, Q32 cycles a sample (0: silent)
    u32 fade[chain::voiced];    // its fade toward Nyquist, Q15
    u32 key;                    // the pitch and dispersion the increments were set for
    i32 dispersion;
    u32 turn;                   // which third of the modes this block measures
    u32 fresh;                  // 1 until the note's first block loads the chain
};
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
