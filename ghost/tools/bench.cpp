// A host estimate of Ghost's cost in Blackfin instructions per voice block, as
// sdk.py's tooling/cpu.py estimates synths (it does not time sample engines):
// render_sample is timed against the SDK's reference loop, a table oscillator
// through a one-pole low-pass of about 590 Blackfin instructions a block.
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/bench.cpp -o bench && ./bench
#include "../sample.cpp"
#include <chrono>
#include <cstdio>

namespace {
double reference() {
    static opxy::i32 table[256], output[64];
    for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
    opxy::u32 phase = 0;
    opxy::i32 low = 0;
    volatile opxy::i32 sink = 0;
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
}

int main() {
    static opxy::i16 bed[2 * 44100];
    opxy::u32 seed = 3;
    for (int i = 0; i < 2 * 44100; ++i) bed[i] = opxy::i16(opxy::i32(seed = seed * 1664525u + 1013904223u) >> 17);
    const double ref = reference();
    // The default (Seance), and the heaviest setting: the stereo bed mixed in,
    // split past 3/4 (the fundamental's notch), beat, trade and orbit moving.
    const double settings[2][8] = {{0.6, 0.6, 0.6, 0.3, 0.35, 0.2, 0.4, 0}, {0.7, 0.5, 1, 0.5, 0.6, 0.9, 0.6, 0.5}};
    const char* const labels[2] = {"default", "heaviest"};
    for (int k = 0; k < 2; ++k) {
        static opxy::i32 out[128], parameters[8];
        static opxy::u32 pitch[64];
        for (int i = 0; i < 8; ++i) parameters[i] = opxy::i32(settings[k][i] * 2147483647.0);
        for (int i = 0; i < 64; ++i) pitch[i] = 25480119u;
        opxy::SampleBlock block = opxy::SampleBlock();
        block.output = out; block.parameters = parameters; block.pitch = pitch;
        block.region.begin = 0; block.region.end = 44100; block.region.level = opxy::sample_unity_level;
        block.output_channels = 2;
        static ghost::Voice voice;
        const opxy::SampleView view = {bed, 44100, 48000, 2};
        volatile opxy::i32 sink = 0;
        double best = 1e30;
        for (int run = 0; run < 7; ++run) {
            ghost::initialize(voice, view);
            const auto begin = std::chrono::steady_clock::now();
            for (int b = 0; b < 2000; ++b) {
                ghost::render_sample(voice, block);
                sink = sink + out[b & 127];
            }
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
            if (ns < best) best = ns;
        }
        std::printf("%-8s about %5.0f Blackfin instructions per voice block\n", labels[k], best / 2000 / ref * 590);
    }
    return 0;
}
