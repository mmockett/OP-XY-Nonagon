// Coin's presets, each playing a short musical phrase. Run by sdk.py test,
// it checks each phrase's peak, DC and that it ends in silence, and a four-
// note chord's summed peak for every preset; given a folder, it also writes
// the phrases there as 16-bit mono WAVs:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include tests/scenes.cpp -o scenes
//   ./scenes <folder>
// Voices are summed at unit gain, eight of them, a new note taking the oldest,
// each through a stand-in for the OP-XY's amp envelope: full sustain, then on
// release a fall of 60 dB over the preset's release time (Coin's presets set
// the longest, so a spin plays out; Drone's is shorter, as a drone wants).
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const unsigned rate = 44100;

struct Note { double at, length; int midi; };
struct Phrase {
    const char* preset;
    const char* name;
    double seconds;
    double release;       // the amp envelope's release, s
    Note notes[40];
    unsigned count;
};

// The presets in definition.json, in control order.
struct Preset { const char* name; double c[8]; };
const Preset presets[] = {
    {"Euler",   {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125}},
    {"Tick",    {0.0428, 0.1, 0.375, 0.8333, 0.3, 0.0, 0.0, 0.125}},
    {"Knock",   {0.1554, 0.3, 0.875, 0.5, 0.2, 0.2, 0.561, 0.125}},
    {"Bell",    {0.24, 0.1, 0.625, 0.1667, 0.1, 0.4, 0.7826, 0.125}},
    {"Chorus",  {0.5484, 0.3, 0.375, 0.1667, 0.3, 0.3, 0.8023, 0.875}},
    {"Thrum",   {0.4761, 0.1, 0.125, 0.1667, 0.45, 0.2, 0.7387, 0.125}},
    {"Drone",   {1.0, 0.1, 0.625, 0.1667, 0.35, 0.0, 0.6046, 0.375}},
    {"Settle",  {0.7495, 0.5, 0.125, 0.1667, 0.25, 1.0, 0.8295, 0.125}},
};
const Preset& preset(const char* name) {
    for (const Preset& p : presets)
        if (!std::strcmp(p.name, name)) return p;
    return presets[0];
}

opxy::u32 increment(int midi) {
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5);
}
opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }

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

int failures = 0;

// Renders a phrase into out (full scale +-1), returning its frame count.
unsigned play(const Phrase& phrase, float* out) {
    static coin::Voice voices[8];
    static coin::Shared shared;
    const Preset& p = preset(phrase.preset);
    opxy::i32 parameters[8];
    for (unsigned k = 0; k < 8; ++k) parameters[k] = q31(p.c[k]);
    int note_of[8];
    double started[8], gate[8], amp[8];
    for (unsigned v = 0; v < 8; ++v) {
        coin::initialize(voices[v], shared, v + 1);
        note_of[v] = -1;
        started[v] = -1;
        amp[v] = 0;
    }
    const double fall = std::pow(0.001, 64.0 / (phrase.release * rate));   // a block's release
    const unsigned blocks = static_cast<unsigned>(phrase.seconds * rate / 64);
    unsigned next = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        const double t = b * 64.0 / rate;
        while (next < phrase.count && phrase.notes[next].at <= t) {
            unsigned v = 0;
            for (unsigned w = 1; w < 8; ++w) if (started[w] < started[v]) v = w;   // a free voice, or the oldest
            coin::start(voices[v], shared, v + 1);
            note_of[v] = static_cast<int>(next);
            started[v] = t;
            gate[v] = t + phrase.notes[next].length;
            amp[v] = 1;
            ++next;
        }
        double mix[64] = {};
        for (unsigned v = 0; v < 8; ++v) {
            if (note_of[v] < 0) continue;
            opxy::u32 pitch[64];
            for (unsigned i = 0; i < 64; ++i) pitch[i] = increment(phrase.notes[note_of[v]].midi);
            opxy::i32 o[64];
            coin::render(voices[v], shared, v + 1, o, parameters, pitch);
            if (t >= gate[v]) amp[v] *= fall;
            for (unsigned i = 0; i < 64; ++i) mix[i] += o[i] / 2147483648.0 * amp[v];
        }
        for (unsigned i = 0; i < 64; ++i) out[b * 64 + i] = static_cast<float>(mix[i]);
    }
    return blocks * 64;
}

// Four voices struck together, summed at unit gain: their peak.
double chord_peak(const Preset& p) {
    static coin::Voice voices[4];
    static coin::Shared shared;
    opxy::i32 parameters[8];
    for (unsigned k = 0; k < 8; ++k) parameters[k] = q31(p.c[k]);
    const int chord[4] = {57, 60, 64, 69};
    for (unsigned v = 0; v < 4; ++v) { coin::initialize(voices[v], shared, v + 1); coin::start(voices[v], shared, v + 1); }
    double peak = 0;
    for (unsigned b = 0; b < 689 * 4; ++b) {
        double mix[64] = {};
        for (unsigned v = 0; v < 4; ++v) {
            opxy::u32 pitch[64];
            for (unsigned i = 0; i < 64; ++i) pitch[i] = increment(chord[v]);
            opxy::i32 o[64];
            coin::render(voices[v], shared, v + 1, o, parameters, pitch);
            for (unsigned i = 0; i < 64; ++i) mix[i] += o[i] / 2147483648.0;
        }
        for (unsigned i = 0; i < 64; ++i) peak = std::fabs(mix[i]) > peak ? std::fabs(mix[i]) : peak;
    }
    return peak;
}
}

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : nullptr;
    // Am: A C E, F: F A C, C: C E G, G: G B D.
    const Phrase phrases[] = {
        {"Euler", "euler-melody", 7.0, 30,
         {{0.0, 0.4, 69}, {0.5, 0.4, 72}, {1.0, 0.4, 76}, {1.5, 0.9, 74}, {2.5, 0.4, 72}, {3.0, 0.4, 71},
          {3.5, 1.4, 69}}, 7},
        {"Tick", "tick-rhythm", 4.5, 30,
         {{0.0, 0.1, 84}, {0.25, 0.1, 84}, {0.375, 0.1, 91}, {0.5, 0.1, 84}, {0.75, 0.1, 84}, {1.0, 0.1, 84},
          {1.125, 0.1, 91}, {1.25, 0.1, 84}, {1.5, 0.1, 84}, {1.75, 0.1, 91}, {2.0, 0.1, 84}, {2.25, 0.1, 84},
          {2.375, 0.1, 91}, {2.5, 0.1, 84}, {2.75, 0.1, 84}, {3.0, 0.1, 84}, {3.125, 0.1, 91}, {3.25, 0.1, 84},
          {3.5, 0.1, 96}}, 19},
        {"Knock", "knock-melody", 4.5, 30,
         {{0.0, 0.2, 64}, {0.25, 0.2, 67}, {0.5, 0.2, 69}, {0.75, 0.2, 72}, {1.0, 0.2, 69}, {1.25, 0.2, 67},
          {1.5, 0.4, 64}, {2.0, 0.2, 62}, {2.25, 0.2, 64}, {2.5, 0.2, 67}, {2.75, 0.2, 64}, {3.0, 0.6, 62}}, 12},
        {"Bell", "bell-chords", 9.0, 30,
         {{0.0, 1.4, 57}, {0.0, 1.4, 64}, {0.0, 1.4, 72}, {1.5, 1.4, 53}, {1.5, 1.4, 65}, {1.5, 1.4, 69},
          {3.0, 1.4, 55}, {3.0, 1.4, 64}, {3.0, 1.4, 72}, {4.5, 1.4, 55}, {4.5, 1.4, 62}, {4.5, 1.4, 71}}, 12},
        {"Chorus", "chorus-chords", 11.0, 30,
         {{0.0, 2.8, 57}, {0.0, 2.8, 60}, {0.0, 2.8, 64}, {3.0, 2.8, 53}, {3.0, 2.8, 57}, {3.0, 2.8, 60}}, 6},
        {"Thrum", "thrum-bassline", 9.0, 30,
         {{0.0, 0.9, 33}, {1.0, 0.4, 33}, {1.5, 0.4, 36}, {2.0, 0.9, 38}, {3.0, 0.4, 40}, {3.5, 0.4, 38},
          {4.0, 0.9, 36}, {5.0, 1.9, 33}}, 8},
        {"Drone", "drone-fifths", 11.0, 2.5,
         {{0.0, 4.0, 45}, {0.0, 4.0, 52}, {4.0, 4.0, 43}, {4.0, 4.0, 50}}, 4},
        {"Settle", "settle-long", 13.5, 30, {{0.0, 13.0, 57}}, 1},
    };
    static float out[rate * 14];
    static short pcm[rate * 14];
    for (const Phrase& phrase : phrases) {
        const unsigned frames = play(phrase, out);
        double peak = 0, sum = 0, total = 0, tail = 0;
        long loud = 0;
        for (unsigned i = 0; i < frames; ++i) {
            const double x = out[i];
            peak = std::fabs(x) > peak ? std::fabs(x) : peak;
            total += x;
            if (std::fabs(x) > 1e-3) { sum += x * x; ++loud; }
            if (i + 4410 >= frames) tail = std::fabs(x) > tail ? std::fabs(x) : tail;
            pcm[i] = static_cast<short>(std::lrint((x > 1 ? 1 : x < -1 ? -1 : x) * 32767));
        }
        const double level = 10 * std::log10(sum / (loud ? loud : 1) + 1e-30), dc = total / frames;
        const double chord = chord_peak(preset(phrase.preset));
        std::printf("%-6s %-15s peak %.3f, %.1f dBFS sounding, DC %+.5f, last 0.1 s under %.1e; "
                    "A3 C4 E4 A4 struck together peaks at %.2f\n",
                    phrase.preset, phrase.name, peak, level, dc, tail, chord);
        if (peak > 1.0 || std::fabs(dc) > 1e-3 || tail > 1e-3 || chord > 2.0) {
            std::printf("FAIL %s\n", phrase.name);
            ++failures;
        }
        if (!folder) continue;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/coin-%s.wav", folder, phrase.name);
        write_wav(path, pcm, frames);
    }
    return failures ? 1 : 0;
}
