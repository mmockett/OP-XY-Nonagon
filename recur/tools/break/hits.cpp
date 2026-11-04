// Renders MD KIT hits on the host, through MD KIT's own engine (machines/mdkit/engine.cpp).
// Reads from stdin:  <frames> then lines "<start frame> <model> <c0..c7 0..127> <gain>".
// Each hit gets a fresh voice, rendered until the engine says it has ended (or 6 s);
// its tail wraps round the loop so the loop point is seamless. Writes float32 mono
// (full scale = 1.0, the engine's half-scale ceiling = 0.5) to stdout.
// Built and run by make_break.py.
#include "engine.cpp"
#include <cstdio>
#include <vector>

int main() {
    long frames = 0;
    if (std::scanf("%ld", &frames) != 1 || frames <= 0) return 1;
    std::vector<double> mix(frames, 0.0);
    mdkit::Shared shared = mdkit::Shared();
    long start;
    unsigned model;
    unsigned c[8];
    double gain;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = ::md::kit::middle_c;
    while (std::scanf("%ld %u %u %u %u %u %u %u %u %u %lf", &start, &model, &c[0], &c[1], &c[2], &c[3], &c[4],
                      &c[5], &c[6], &c[7], &gain) == 11) {
        opxy::i32 values[8];
        for (int k = 0; k < 8; ++k) values[k] = c[k] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(c[k] << 24);
        mdkit::Voice voice = mdkit::Voice();
        mdkit::initialize(voice, shared, 1);
        mdkit::start(voice, shared, 1);
        opxy::i32 out[opxy::block_frames];
        const long blocks = 6L * 44100 / opxy::block_frames;
        for (long b = 0; b < blocks && !voice.done; ++b) {
            mdkit::drum_select(voice, shared, 1, model);
            mdkit::render(voice, shared, 1, out, values, pitch);
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const long f = ((start + b * opxy::block_frames + i) % frames + frames) % frames;
                mix[f] += gain * (out[i] / 2147483648.0);
            }
        }
    }
    std::vector<float> f32(mix.begin(), mix.end());
    std::fwrite(f32.data(), sizeof(float), f32.size(), stdout);
    return 0;
}
