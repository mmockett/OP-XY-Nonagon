// Cocoa's demonstration scenes: each preset playing a short phrase (a melody,
// chords, a bass line, a riff or a long note), plus a single held A3 and an
// eight-note chord of the default for measuring. Run by sdk.py test, it checks
// each one's peak, level and DC; given a folder, it also writes them there as
// 16-bit mono WAVs:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include tests/scenes.cpp -o scenes
//   ./scenes <folder>
// Notes take one of eight voices, a free one or the oldest, as stock does.
// Each runs through a stand-in for the OP-XY's amp envelope: full sustain
// while the key is held, then a 0.4 s release, after which the voice is
// destroyed. Voices are summed at unity.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const unsigned rate = 44100;
const double beat = 0.6;   // 100 BPM

struct Note { double at, held; int midi; };
struct Scene {
    const char* name;
    double seconds;
    double controls[8];   // powder, taps, fizz, cup, fill, spoon, pops, stir
    Note notes[48];
    unsigned count;
};

opxy::u32 increment(int midi) {
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5);
}

void write_wav(const char* path, const short* samples, unsigned frames) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    const unsigned bytes = frames * 2;
    unsigned char h[44];
    std::memcpy(h, "RIFF", 4);
    unsigned v = 36 + bytes; std::memcpy(h + 4, &v, 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; std::memcpy(h + 16, &v, 4);
    unsigned short s = 1; std::memcpy(h + 20, &s, 2);
    s = 1; std::memcpy(h + 22, &s, 2);
    v = rate; std::memcpy(h + 24, &v, 4);
    v = rate * 2; std::memcpy(h + 28, &v, 4);
    s = 2; std::memcpy(h + 32, &s, 2);
    s = 16; std::memcpy(h + 34, &s, 2);
    std::memcpy(h + 36, "data", 4);
    std::memcpy(h + 40, &bytes, 4);
    std::fwrite(h, 1, 44, f);
    std::fwrite(samples, 2, frames, f);
    std::fclose(f);
}

opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + 0.5); }

int failures = 0;

void render(const char* folder, const Scene& scene) {
    static cocoa::Voice voices[8];
    static cocoa::Shared shared;
    static short out[rate * 40];
    std::memset(static_cast<void*>(&shared), 0, sizeof shared);
    const unsigned blocks = static_cast<unsigned>(scene.seconds * rate / 64);
    int playing[8];         // the note each voice plays, or -1
    double gate[8], born[8];
    for (unsigned v = 0; v < 8; ++v) {
        cocoa::initialize(voices[v], shared, v + 1);
        playing[v] = -1;
        gate[v] = 0;
        born[v] = 0;
    }
    bool started[48] = {};
    opxy::i32 parameters[8];
    for (unsigned k = 0; k < 8; ++k) parameters[k] = q31(scene.controls[k]);
    double peak = 0, sum = 0, total = 0;
    long loud = 0;
    const double release = std::exp(-6.9 / (0.4 * rate));
    for (unsigned b = 0; b < blocks; ++b) {
        const double t = b * 64.0 / rate;
        for (unsigned n = 0; n < scene.count; ++n) {
            if (started[n] || t < scene.notes[n].at) continue;
            started[n] = true;
            unsigned v = 8;
            for (unsigned u = 0; u < 8 && v == 8; ++u) if (playing[u] < 0) v = u;
            if (v == 8) {   // the oldest note's voice
                v = 0;
                for (unsigned u = 1; u < 8; ++u) if (born[u] < born[v]) v = u;
                cocoa::destroy(voices[v], shared, v + 1);
            }
            playing[v] = static_cast<int>(n);
            gate[v] = 1;
            born[v] = t;
            cocoa::start(voices[v], shared, v + 1);
        }
        double mix[64] = {};
        for (unsigned v = 0; v < 8; ++v) {
            if (playing[v] < 0) continue;
            const Note& n = scene.notes[playing[v]];
            opxy::u32 pitch[64];
            for (unsigned i = 0; i < 64; ++i) pitch[i] = increment(n.midi);
            opxy::i32 o[64];
            cocoa::render(voices[v], shared, v + 1, o, parameters, pitch);
            const bool held = t < n.at + n.held;
            for (unsigned i = 0; i < 64; ++i) {
                if (!held) gate[v] *= release;
                mix[i] += o[i] / 2147483648.0 * gate[v];
            }
            if (!held && gate[v] < 1e-4) {
                cocoa::destroy(voices[v], shared, v + 1);
                playing[v] = -1;
            }
        }
        for (unsigned i = 0; i < 64; ++i) {
            const double x = mix[i];
            peak = std::fabs(x) > peak ? std::fabs(x) : peak;
            total += x;
            if (std::fabs(x) > 1e-3) { sum += x * x; ++loud; }
            double y = x * 32767.0;
            y = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
            out[b * 64 + i] = static_cast<short>(std::lrint(y));
        }
    }
    const double level = 10 * std::log10(sum / (loud ? loud : 1) + 1e-30), dc = total / (blocks * 64.0);
    std::printf("scene %-15s %5.1f s, peak %.3f, %5.1f dBFS RMS while sounding, DC %+.6f\n", scene.name, scene.seconds,
                peak, level, dc);
    if (peak > 0.85 || level < -34 || level > -14 || std::fabs(dc) > 1e-3) {
        std::printf("FAIL scene %s\n", scene.name);
        ++failures;
    }
    if (!folder) return;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/cocoa-%s.wav", folder, scene.name);
    write_wav(path, out, blocks * 64);
}

// A phrase from (beat, beats held, MIDI note) triples.
template <unsigned N>
unsigned phrase(Note* notes, const double (&n)[N][3]) {
    for (unsigned k = 0; k < N; ++k) notes[k] = {0.2 + n[k][0] * beat, n[k][1] * beat, static_cast<int>(n[k][2])};
    return N;
}
}

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : nullptr;
    // The presets (definition.json).
    const double keys[8] = {0.32, 0.0, 0.7, 0.1667, 0.65, 0.6, 0.15, 0.6};
    const double bells[8] = {0.22, 0.0, 0.8, 0.5, 0.25, 0.8, 0.05, 0.8};
    const double tin[8] = {0.45, 0.62, 0.55, 0.8333, 0.45, 0.85, 0.3, 0.3};
    const double bass[8] = {0.25, 0.0, 0.75, 0.1667, 0.8, 0.35, 0.0, 0.3};
    const double chocolate[8] = {0.87, 0.55, 0.2, 0.1667, 0.5, 0.6, 0.35, 1.0};
    const double fizz[8] = {0.7, 0.85, 0.35, 0.5, 0.4, 0.9, 1.0, 0.5};
    const double spoonful[8] = {0.7, 0.0, 0.45, 0.1667, 0.6, 0.6, 0.25, 0.0};

    // Mug Keys: a tune over half-note chords (C, Am, F, G, C).
    const double tune[][3] = {
        {0, 1, 64}, {1, 1, 62}, {2, 1, 60}, {3, 1, 62}, {4, 1, 64}, {5, 1, 64}, {6, 2, 64},
        {8, 1, 62}, {9, 1, 62}, {10, 2, 62}, {12, 1, 64}, {13, 1, 67}, {14, 2, 67}, {16, 4, 72},
        {0, 2, 48}, {0, 2, 55}, {4, 2, 45}, {4, 2, 52}, {8, 2, 41}, {8, 2, 48}, {12, 2, 43}, {12, 2, 50},
        {16, 4, 48}, {16, 4, 55}, {16, 4, 64}};
    // Glass Bells: broken chords, Am F C G, then Am together.
    const double broken[][3] = {
        {0, 0.5, 57}, {0.5, 0.5, 60}, {1, 0.5, 64}, {1.5, 0.5, 69}, {2, 0.5, 53}, {2.5, 0.5, 57}, {3, 0.5, 60}, {3.5, 0.5, 65},
        {4, 0.5, 48}, {4.5, 0.5, 55}, {5, 0.5, 60}, {5.5, 0.5, 64}, {6, 0.5, 55}, {6.5, 0.5, 59}, {7, 0.5, 62}, {7.5, 0.5, 67},
        {8, 4, 57}, {8, 4, 60}, {8, 4, 64}, {8, 4, 69}};
    // Tin Percussion: held notes, the spoon taps their rhythm.
    const double riff[][3] = {{0, 2, 57}, {2, 1, 60}, {3, 1, 55}, {4, 2, 57}, {6, 1, 64}, {7, 1, 62}, {8, 4, 57}, {8, 4, 45}};
    // Bass Mug: a bass line in eighths and quarters.
    const double line[][3] = {
        {0, 0.5, 33}, {0.5, 0.5, 33}, {1, 1, 45}, {2, 0.5, 36}, {2.5, 0.5, 38}, {3, 1, 40},
        {4, 0.5, 29}, {4.5, 0.5, 29}, {5, 1, 41}, {6, 0.5, 31}, {6.5, 0.5, 31}, {7, 1, 43}, {8, 3, 33}};
    // Hot Chocolate: one long note, then a held fifth.
    const double climb[][3] = {{0, 9, 57}, {10, 9, 52}, {10, 9, 59}};
    // Fizz: an A minor seventh held, fizzing.
    const double held[][3] = {{0, 9, 45}, {0, 9, 52}, {0, 9, 55}, {0, 9, 60}};
    // Spoonful: legato single taps on one drink, each higher; a pause; again.
    const double spoon[][3] = {
        {0, 1, 57}, {0.9, 1, 57}, {1.8, 1, 60}, {2.7, 1, 57}, {3.6, 1, 64}, {4.5, 1, 62}, {5.4, 1.5, 57},
        {8, 1, 57}, {8.9, 1, 60}, {9.8, 1, 64}, {10.7, 2, 69}};
    const double one[][3] = {{0, 6, 57}};
    const double eight[][3] = {{0, 6, 45}, {0, 6, 52}, {0, 6, 57}, {0, 6, 60}, {0, 6, 64}, {0, 6, 67}, {0, 6, 71}, {0, 6, 76}};

    static Scene scenes[9];
    const struct { const char* name; double seconds; const double* c; } list[9] = {
        {"mug-keys", 13.5, keys}, {"glass-bells", 9.0, bells}, {"tin-percussion", 8.5, tin}, {"bass-mug", 7.5, bass},
        {"hot-chocolate", 12.5, chocolate}, {"fizz", 7.0, fizz}, {"spoonful", 9.5, spoonful},
        {"single-a3", 4.5, keys}, {"chord-8", 4.5, keys}};
    for (unsigned s = 0; s < 9; ++s) {
        scenes[s].name = list[s].name;
        scenes[s].seconds = list[s].seconds;
        for (unsigned k = 0; k < 8; ++k) scenes[s].controls[k] = list[s].c[k];
    }
    scenes[0].count = phrase(scenes[0].notes, tune);
    scenes[1].count = phrase(scenes[1].notes, broken);
    scenes[2].count = phrase(scenes[2].notes, riff);
    scenes[3].count = phrase(scenes[3].notes, line);
    scenes[4].count = phrase(scenes[4].notes, climb);
    scenes[5].count = phrase(scenes[5].notes, held);
    scenes[6].count = phrase(scenes[6].notes, spoon);
    scenes[7].count = phrase(scenes[7].notes, one);
    scenes[8].count = phrase(scenes[8].notes, eight);
    for (const Scene& s : scenes) render(folder, s);
    if (folder) std::printf("wrote %s/cocoa-*.wav\n", folder);
    return failures ? 1 : 0;
}
