// A host CPU estimate for Recur, which sdk.py test makes only for synths and
// effects. It times one voice's render_sample at its heaviest settings against
// the SDK's reference loop (tooling/cpu.py: about 590 Blackfin instructions a
// block), in the same run, and prints Blackfin instructions per voice block.
// Not a device measurement. Build and run:
//   clang++ -std=c++11 -O2 -I<open-xy>/sdk/include tools/cpu.cpp -o <folder>/cpu && <folder>/cpu
#include "../sample.cpp"
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const u32 frames = 4 * 44100;
opxy::i16 mono[frames], stereo[2 * frames];

double reference() {
    static i32 table[256], output[64];
    for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
    u32 phase = 0;
    i32 low = 0;
    volatile i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 7; ++run) {
        const auto begin = std::chrono::steady_clock::now();
        for (int block = 0; block < 20000; ++block) {
            for (int i = 0; i < 64; ++i) {
                phase += 42852281u;
                low += (table[phase >> 24] - low) >> 4;
                output[i] = low;
            }
            sink = sink + output[block & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        if (ns < best) best = ns;
    }
    return best / 20000;
}

i32 at(double v) { return i32(v * 2147483647.0); }
i32 choice_of(u32 n, u32 count) { return at((n + 0.5) / count); }

// Nanoseconds per block, the best of seven runs of 4000 blocks.
// grid 0 1 2 for 2 4 8 a side; slice 0 hiss, 1 1/64 .. 5 1/4; rate 7 is an iteration each 1/16.
// hiss at depth 0.5 reads the plain window as well as two iterations. turning
// moves step and depth a little every block, so the orbits are ranked anew.
double time_voice(bool two_channels, u32 region, u32 map, u32 grid, u32 slice, u32 chord, bool turning) {
    static i32 out[opxy::block_frames];
    i32 parameters[8] = {at(0.3), choice_of(grid, recur::grid_count), choice_of(slice, recur::slice_count),
                         choice_of(map, recur::map_count), at(0.2), choice_of(7, recur::rate_count), at(slice ? 1 : 0.5),
                         choice_of(chord, recur::chord_count)};
    u32 pitch[opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = 25480119u * 3 / 2;
    opxy::SampleBlock block = opxy::SampleBlock();
    block.output = out; block.parameters = parameters; block.pitch = pitch;
    block.region.begin = 0; block.region.end = region; block.region.level = opxy::sample_unity_level;
    block.transport.bpm_x10 = 1200;
    block.output_channels = 1;
    const opxy::SampleView view = {two_channels ? stereo : mono, frames, 44100, two_channels ? 2u : 1u};
    static recur::Voice voice;
    volatile i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 7; ++run) {
        recur::initialize(voice, view);
        recur::start(voice);
        const auto begin = std::chrono::steady_clock::now();
        for (int b = 0; b < 4000; ++b) {
            if (turning) {
                parameters[recur::kStep] = at((b % 1000) / 1000.0);
                parameters[recur::kDepth] = at(0.3 + (b % 700) / 1000.0);
            }
            recur::render_sample(voice, block);
            sink = sink + out[b & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        if (ns < best) best = ns;
    }
    return best / 4000;
}
}

int main() {
    u32 seed = 1;
    for (u32 i = 0; i < frames; ++i) mono[i] = opxy::i16((seed = seed * 1664525u + 1013904223u) >> 16);
    for (u32 i = 0; i < 2 * frames; ++i) stereo[i] = opxy::i16((seed = seed * 1664525u + 1013904223u) >> 16);
    struct Case { const char* name; bool two; u32 region, map, grid, slice, chord; bool turning; };
    const Case cases[] = {
        {"defaults: cat, 4 x 4 slices of 1/16, one reader", false, frames, recur::kCat, 1, 3, 0, false},
        {"slices: baker, 8 x 8 of 1/64, maj, stereo", true, frames, recur::kBaker, 2, 1, 4, false},
        {"slices: gold, 8 x 8 of 1/64, maj, stereo, step and depth turning", true, frames, recur::kGold, 2, 1, 4, true},
        {"hiss: cat 64, maj, stereo", true, frames, recur::kCat, 1, 0, 4, false},
        {"hiss: baker 128, maj, stereo", true, frames, recur::kBaker, 2, 0, 4, false},
        {"hiss: baker, maj, stereo, 0.45 s region", true, 20000, recur::kBaker, 2, 0, 4, false},
    };
    double worst = 0;
    for (const Case& c : cases) {
        // Reference and machine timed in turn, nine rounds, the best of each:
        // other work on the computer slows both alike.
        double ref = 1e30, own = 1e30;
        for (int round = 0; round < 9; ++round) {
            const double r = reference(), o = time_voice(c.two, c.region, c.map, c.grid, c.slice, c.chord, c.turning);
            if (r < ref) ref = r;
            if (o < own) own = o;
        }
        const double cost = own / ref * 590;
        std::printf("%-50s about %6.0f Blackfin instructions a voice block\n", c.name, cost);
        if (cost > worst) worst = cost;
    }
    std::printf("heaviest: about %.0f (aim for at most 15,000)\n", worst);
    return 0;
}
