// Drop's demonstration phrases: each preset plays a short musical phrase (a
// melody, chords, a bass line, a rhythm), through a stand-in for the OP-XY's
// amp envelope (no attack, full sustain, a 0.15 s exponential release after
// the key is let go) and its voice allocation (eight voices, the oldest
// taken). Run by sdk.py test, it checks each one's level, DC, that it stays
// in bounds and that it ends in silence; given a folder, it also writes them
// there as 16-bit mono WAVs:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include tests/scenes.cpp -o scenes
//   ./scenes <folder>
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const unsigned rate = 44100;

struct Note { double at, length; int midi; };
struct Scene {
    const char* name;
    double controls[8];   // heat, size, liquid, beat, groove, vapour, life, crackle (definition.json's preset)
    std::vector<Note> notes;
    double seconds;
};

// A phrase from a list of {beat, beats, midi} at a tempo.
std::vector<Note> phrase(double bpm, std::initializer_list<std::initializer_list<double>> list) {
    std::vector<Note> notes;
    const double beat = 60.0 / bpm;
    for (const auto& n : list) {
        const double* v = n.begin();
        notes.push_back({0.2 + v[0] * beat, v[1] * beat, static_cast<int>(v[2])});
    }
    return notes;
}
std::vector<Note> chords(double bpm, double beats, std::initializer_list<std::initializer_list<int>> list) {
    std::vector<Note> notes;
    const double beat = 60.0 / bpm;
    double at = 0.2;
    for (const auto& c : list) {
        for (int m : c) notes.push_back({at, beats * beat * 0.97, m});
        at += beats * beat;
    }
    return notes;
}

std::vector<Scene> scenes() {
    std::vector<Scene> s;
    // Drop: a melody in A minor, quarter and half notes at 96 BPM.
    s.push_back({"drop-1-drop-melody", {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25},
                 phrase(96, {{0, 1, 57}, {1, 1, 60}, {2, 2, 64}, {4, 1, 62}, {5, 1, 60}, {6, 2, 59}, {8, 1, 57},
                             {9, 1, 59}, {10, 1, 60}, {11, 1, 64}, {12, 4, 69}}), 12});
    // Kettle: a pad, Am F C G, a bar each at 80 BPM.
    s.push_back({"drop-2-kettle-pad", {0.55, 1.0, 0, 0.06, 0, 0.05, 0.95, 0.05},
                 chords(80, 4, {{57, 60, 64, 69}, {53, 57, 60, 65}, {48, 55, 60, 64}, {55, 59, 62, 67}}), 14});
    // Star Lead: a lead line, long notes and a run.
    s.push_back({"drop-3-star-lead", {0.45, 0.75, 0, 0.55, 0, 0.3, 0.85, 0.1},
                 phrase(100, {{0, 2, 69}, {2, 1, 72}, {3, 1, 74}, {4, 3, 76}, {7, 0.5, 74}, {7.5, 0.5, 72}, {8, 2, 71},
                              {10, 2, 67}, {12, 4, 69}}), 11});
    // Spirit Bass: an eighth-note bass line, A1 to E2, at 100 BPM.
    s.push_back({"drop-4-spirit-bass", {0.45, 0.85, 0.5, 0.181, 0, 0.15, 0.8, 0.05},
                 phrase(100, {{0, 0.9, 33}, {1, 0.4, 33}, {1.5, 0.4, 45}, {2, 0.9, 36}, {3, 0.9, 38}, {4, 0.9, 40},
                              {5, 0.4, 40}, {5.5, 0.4, 52}, {6, 0.9, 38}, {7, 0.9, 36}, {8, 0.9, 33}, {9, 0.4, 33},
                              {9.5, 0.4, 45}, {10, 0.9, 41}, {11, 0.9, 40}, {12, 3.5, 33}}), 11.5});
    // Ratchet: a riff of held notes, each long enough to run round the track.
    s.push_back({"drop-5-ratchet-rhythm", {0.5, 0.9, 0, 0.181, 0.8, 0.45, 0.6, 0.3},
                 phrase(120, {{0, 3.5, 52}, {4, 3.5, 55}, {8, 1.5, 57}, {10, 1.5, 55}, {12, 3.5, 52}}), 10});
    // Sizzle: percussion, sixteenths and accents at 110 BPM.
    s.push_back({"drop-6-sizzle-percussion", {0.18, 0.6, 0, 0.5, 0, 0.6, 0.5, 0.7},
                 phrase(110, {{0, 0.2, 60}, {0.75, 0.2, 60}, {1.5, 0.2, 67}, {2, 0.2, 60}, {3, 0.2, 72}, {3.5, 0.2, 60},
                              {4, 0.2, 60}, {4.75, 0.2, 60}, {5.5, 0.2, 67}, {6, 0.2, 60}, {6.5, 0.2, 63}, {7, 0.2, 72},
                              {7.25, 0.2, 72}, {7.5, 0.2, 72}}), 5.5});
    // Whisper: liquid nitrogen, high slow chords.
    s.push_back({"drop-7-nitrogen-whisper", {0.6, 0.8, 1, 0.3, 0, 0.85, 0.75, 0.15},
                 chords(70, 4, {{69, 72, 76}, {67, 71, 74}, {65, 69, 72}}), 12});
    // Last Drop: single drops left to live out their lives, one each three seconds.
    s.push_back({"drop-8-last-drop", {0.5, 0.3, 0, 0.4, 0, 0.5, 0.35, 0.4},
                 phrase(60, {{0, 2.8, 64}, {3, 2.8, 59}, {6, 2.8, 67}}), 10});
    return s;
}

opxy::u32 increment(int midi) {
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5);
}

void write_wav(const char* path, const std::vector<short>& samples) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    const unsigned bytes = static_cast<unsigned>(samples.size() * 2);
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
    std::fwrite(samples.data(), 2, samples.size(), f);
    std::fclose(f);
}

opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + 0.5); }

int failures = 0;

void render(const char* folder, const Scene& scene) {
    static drop::Voice voices[8];
    static drop::Shared shared;
    shared.sounding = 0;
    int midi[8];
    double gain[8], off[8], born[8];
    bool used[8];
    for (unsigned v = 0; v < 8; ++v) {
        drop::initialize(voices[v], shared, v + 1);
        midi[v] = -1; gain[v] = 0; off[v] = 1e9; born[v] = 0; used[v] = false;
    }
    opxy::i32 parameters[8];
    for (unsigned k = 0; k < 8; ++k) parameters[k] = q31(scene.controls[k]);
    const double release = std::exp(-64.0 / (0.15 * rate));   // a block's share of the amp's release
    const unsigned blocks = static_cast<unsigned>(scene.seconds * rate / 64);
    std::vector<short> out;
    double peak = 0, sum = 0, total = 0, tail = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        const double t = b * 64.0 / rate;
        for (const Note& n : scene.notes) {
            if (n.at < t || n.at >= t + 64.0 / rate) continue;
            unsigned v = 8;
            for (unsigned k = 0; k < 8 && v == 8; ++k) if (!used[k]) v = k;
            if (v == 8) {   // the oldest note's voice
                v = 0;
                for (unsigned k = 1; k < 8; ++k) if (born[k] < born[v]) v = k;
                drop::destroy(voices[v], shared, v + 1);
                drop::initialize(voices[v], shared, v + 1);
            }
            used[v] = true; midi[v] = n.midi; gain[v] = 1; off[v] = n.at + n.length; born[v] = t;
            drop::start(voices[v], shared, v + 1);
        }
        double mix[64] = {};
        for (unsigned v = 0; v < 8; ++v) {
            if (!used[v]) continue;
            opxy::u32 pitch[64];
            for (unsigned i = 0; i < 64; ++i) pitch[i] = increment(midi[v]);
            opxy::i32 o[64];
            drop::render(voices[v], shared, v + 1, o, parameters, pitch);
            const double g0 = gain[v];
            if (t >= off[v]) gain[v] *= release;
            for (unsigned i = 0; i < 64; ++i) {
                if (o[i] > 1073741824 || o[i] < -1073741824) { std::printf("FAIL %s: over the declared peak\n", scene.name); ++failures; }
                mix[i] += o[i] / 2147483648.0 * (g0 + (gain[v] - g0) * i / 64.0);
            }
            if (gain[v] < 1e-4 || voices[v].quiet) {   // the amp has closed: stock frees the voice
                drop::destroy(voices[v], shared, v + 1);
                drop::initialize(voices[v], shared, v + 1);
                used[v] = false;
            }
        }
        for (unsigned i = 0; i < 64; ++i) {
            const double m = mix[i];
            peak = std::fabs(m) > peak ? std::fabs(m) : peak;
            sum += m;
            total += m * m;
            if (b + 345 > blocks) tail += m * m;
            const double s = m * 32767.0;
            out.push_back(static_cast<short>(s > 32767 ? 32767 : s < -32768 ? -32768 : s));
        }
    }
    const double frames = blocks * 64.0, dc = sum / frames, rms = std::sqrt(total / frames);
    std::printf("%-26s peak %.3f  rms %6.1f dBFS  crest %4.1f  dc %+.5f  last half second %6.1f dBFS\n", scene.name, peak,
                20 * std::log10(rms + 1e-12), peak / (rms + 1e-12), dc, 10 * std::log10(tail / 22050 + 1e-24));
    if (peak > 0.85) { std::printf("FAIL %s: near clipping\n", scene.name); ++failures; }
    if (std::fabs(dc) > 0.002) { std::printf("FAIL %s: DC\n", scene.name); ++failures; }
    if (tail / 22050 > 1e-7) { std::printf("FAIL %s: does not end in silence\n", scene.name); ++failures; }
    if (folder) {
        char path[512];
        std::snprintf(path, sizeof path, "%s/%s.wav", folder, scene.name);
        write_wav(path, out);
    }
}
}

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : 0;
    for (const Scene& s : scenes()) render(folder, s);
    return failures ? 1 : 0;
}
