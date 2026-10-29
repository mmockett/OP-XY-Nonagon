// One held note of Drop with every control at its default but one, as raw
// 32-bit floats on standard output, for tools/sweep.py:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include tools/sweep.cpp -o sweep
//   ./sweep <control 0-7 or -1> <value> <seconds> <midi> > note.f32
//   ./sweep all <heat> <size> <liquid> <beat> <groove> <vapour> <life> <crackle> <seconds> <midi> > note.f32
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 5) return 1;
    double controls[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25};   // definition.json's defaults
    double seconds;
    int midi;
    if (!std::strcmp(argv[1], "all")) {
        if (argc < 12) return 1;
        for (int k = 0; k < 8; ++k) controls[k] = std::atof(argv[2 + k]);
        seconds = std::atof(argv[10]);
        midi = std::atoi(argv[11]);
    } else {
        const int control = std::atoi(argv[1]);
        if (control >= 0 && control < 8) controls[control] = std::atof(argv[2]);
        seconds = std::atof(argv[3]);
        midi = std::atoi(argv[4]);
    }
    opxy::i32 parameters[8];
    for (int k = 0; k < 8; ++k) parameters[k] = static_cast<opxy::i32>(controls[k] * 2147483647.0 + 0.5);
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = static_cast<opxy::u32>(hz / 44100 * 4294967296.0 + 0.5);
    static drop::Voice voice;
    drop::Shared shared = {0};
    drop::initialize(voice, shared, 1);
    drop::start(voice, shared, 1);
    float out[64];
    for (long b = 0, blocks = static_cast<long>(seconds * 44100 / 64); b < blocks; ++b) {
        opxy::i32 o[64];
        drop::render(voice, shared, 1, o, parameters, pitch);
        for (int i = 0; i < 64; ++i) out[i] = static_cast<float>(o[i] / 2147483648.0);
        std::fwrite(out, sizeof(float), 64, stdout);
    }
    return 0;
}
