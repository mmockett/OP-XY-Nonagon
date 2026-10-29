// Renders a scene to a WAV (44.1 kHz, 16-bit mono) through a machine's own
// render(), eight voices mixed as the device mixes them before its amp
// envelope. A host tool, not part of the machine. tools/phrases.py writes
// the scenes and builds this for Fermi and for Hexagon's engines:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include -DENGINE='"path/engine.cpp"' -DNS=name tools/render.cc -o render
//   ./render scene.txt out.wav
// A scene file: "seconds attack release" (seconds), then the eight control
// values (0..1), then one note a line: "start length midi" (seconds). The
// amp envelope is the OP-XY's; a linear attack and an exponential release
// stand in for it.
#include ENGINE   // -DENGINE='"path/engine.cpp"' -DNS=its namespace
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
const double rate = 44100.0;
struct Note { double start, length, midi; };
opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : v <= 0 ? 0 : static_cast<opxy::i32>(v * 2147483647.0); }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: render scene.txt out.wav\n"); return 1; }
    std::FILE* in = std::fopen(argv[1], "r");
    if (!in) { std::perror(argv[1]); return 1; }
    double seconds, attack, release, c[8];
    if (std::fscanf(in, "%lf %lf %lf", &seconds, &attack, &release) != 3) return 1;
    for (int k = 0; k < 8; ++k) if (std::fscanf(in, "%lf", &c[k]) != 1) return 1;
    std::vector<Note> notes;
    Note n;
    while (std::fscanf(in, "%lf %lf %lf", &n.start, &n.length, &n.midi) == 3) notes.push_back(n);
    std::fclose(in);

    static NS::Voice voices[8];
    static NS::Shared shared;
    opxy::i32 parameters[8];
    for (int k = 0; k < 8; ++k) parameters[k] = q31(c[k]);
    for (unsigned v = 0; v < 8; ++v) NS::initialize(voices[v], shared, v + 1);
    // Each note takes the voice that has been free longest.
    std::vector<int> voice_of(notes.size(), -1);
    double free_at[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < notes.size(); ++i) {
        int best = 0;
        for (int v = 1; v < 8; ++v) if (free_at[v] < free_at[best]) best = v;
        voice_of[i] = best;
        free_at[best] = notes[i].start + notes[i].length + 6 * release;
    }
    const int frames = static_cast<int>(seconds * rate) / 64 * 64;
    std::vector<double> out(frames, 0.0);
    int current[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    for (int b = 0; b < frames; b += 64) {
        const double t0 = b / rate;
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].start >= t0 && notes[i].start < t0 + 64 / rate) {
                current[voice_of[i]] = static_cast<int>(i);
                NS::start(voices[voice_of[i]], shared, voice_of[i] + 1);
            }
        for (int v = 0; v < 8; ++v) {
            if (current[v] < 0) continue;
            const Note& note = notes[current[v]];
            opxy::u32 pitch[64];
            const double hz = 440.0 * std::pow(2.0, (note.midi - 69) / 12.0);
            for (int i = 0; i < 64; ++i) pitch[i] = static_cast<opxy::u32>(hz / rate * 4294967296.0);
            opxy::i32 block[64];
            NS::render(voices[v], shared, v + 1, block, parameters, pitch);
            for (int i = 0; i < 64; ++i) {
                const double t = (b + i) / rate - note.start;
                double gate = t <= 0 ? 0.0 : t < attack ? t / attack : 1.0;
                if (t > note.length) gate = std::exp(-(t - note.length) / (release / 6.9));
                out[b + i] += block[i] / 2147483648.0 * gate;
            }
        }
    }
    std::FILE* f = std::fopen(argv[2], "wb");
    if (!f) { std::perror(argv[2]); return 1; }
    const unsigned count = out.size(), bytes = count * 2;
    auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (double v : out) {
        const long s = std::lround(v * 32767);
        const short x = static_cast<short>(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        std::fwrite(&x, 2, 1, f);
    }
    std::fclose(f);
    return 0;
}
