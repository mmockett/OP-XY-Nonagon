// Butterfly's demonstration scenes: each preset plays a short phrase in its
// role, and one sweeps the flux under a re-struck bell. Run by sdk.py test,
// it checks each one's peak and DC; given a folder, it also writes them there
// as 16-bit mono WAVs:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include tests/scenes.cpp -o scenes
//   ./scenes <folder>
// The engine never hears a note's release, so this applies an amp envelope
// as the OP-XY would: full sustain while the note is held, then a release of
// about 0.6 s (a time constant of 0.2 s). Voices are taken round-robin.
#include "../engine.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace butterfly;
const unsigned rate = 44100;

struct Note { double at, length; int midi; };
struct Sweep { int control; double from, to, start, end; };
struct Scene {
    const char* name;
    double seconds;
    double controls[8];   // flux, coupling, spread, strike, glow, site, tilt, wingbeat
    std::vector<Note> notes;
    std::vector<Sweep> sweeps;
};

opxy::u32 increment(int midi) {
    const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5);
}
opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }

void write_wav(const std::string& path, const std::vector<short>& samples) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::perror(path.c_str());
        return;
    }
    const unsigned bytes = static_cast<unsigned>(samples.size() * 2);
    unsigned char h[44];
    std::memcpy(h, "RIFF", 4);
    unsigned v = 36 + bytes;
    std::memcpy(h + 4, &v, 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;
    std::memcpy(h + 16, &v, 4);
    unsigned short s = 1;
    std::memcpy(h + 20, &s, 2);
    std::memcpy(h + 22, &s, 2);
    v = rate;
    std::memcpy(h + 24, &v, 4);
    v = rate * 2;
    std::memcpy(h + 28, &v, 4);
    s = 2;
    std::memcpy(h + 32, &s, 2);
    s = 16;
    std::memcpy(h + 34, &s, 2);
    std::memcpy(h + 36, "data", 4);
    std::memcpy(h + 40, &bytes, 4);
    std::fwrite(h, 1, 44, f);
    std::fwrite(samples.data(), 2, samples.size(), f);
    std::fclose(f);
}

int failures = 0;

void play(const std::string& folder, const Scene& scene) {
    static Voice voices[8];
    static Shared shared;
    opxy::u32 pitch[8][opxy::block_frames] = {};
    double off[8] = {}, amp[8] = {};
    bool on[8] = {};
    for (unsigned v = 0; v < 8; ++v) {
        initialize(voices[v], shared, v + 1);
        off[v] = -1;
    }
    const unsigned blocks = static_cast<unsigned>(scene.seconds * rate / opxy::block_frames);
    const double release = std::exp(-1.0 / (0.2 * rate));
    std::vector<double> mix;
    size_t next = 0;
    unsigned turn = 0;
    double sum2 = 0, sum = 0, peak = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        const double t = b * static_cast<double>(opxy::block_frames) / rate;
        while (next < scene.notes.size() && scene.notes[next].at <= t) {
            const unsigned v = turn++ % 8;
            for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[v][i] = increment(scene.notes[next].midi);
            start(voices[v], shared, v + 1);
            on[v] = true;
            off[v] = scene.notes[next].at + scene.notes[next].length;
            amp[v] = 1;
            ++next;
        }
        double c[8];
        for (unsigned i = 0; i < 8; ++i) c[i] = scene.controls[i];
        for (const Sweep& s : scene.sweeps) {
            const double u = t <= s.start ? 0 : t >= s.end ? 1 : (t - s.start) / (s.end - s.start);
            c[s.control] = s.from + (s.to - s.from) * u;
        }
        opxy::i32 p[8];
        for (unsigned i = 0; i < 8; ++i) p[i] = q31(c[i]);
        double block[opxy::block_frames] = {};
        for (unsigned v = 0; v < 8; ++v) {
            if (!on[v]) continue;
            opxy::i32 out[opxy::block_frames];
            render(voices[v], shared, v + 1, out, p, pitch[v]);
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                if (t + i / static_cast<double>(rate) >= off[v]) amp[v] *= release;
                block[i] += amp[v] * out[i] / 2147483648.0;
            }
            if (amp[v] < 1e-5) on[v] = false;
        }
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const double x = block[i];
            mix.push_back(x);
            sum += x;
            sum2 += x * x;
            peak = std::max(peak, std::fabs(x));
        }
    }
    const double n = static_cast<double>(mix.size());
    const double rms = std::sqrt(sum2 / n), dc = sum / n;
    const bool ok = peak <= 0.9 && std::fabs(dc) < 1e-3;
    std::printf("%s  %-14s %5.1f s: RMS %6.1f dBFS, peak %.2f, DC %+.5f\n", ok ? "ok  " : "FAIL", scene.name, scene.seconds,
                20 * std::log10(rms + 1e-12), peak, dc);
    if (!ok) ++failures;
    if (!folder.empty()) {
        std::vector<short> pcm(mix.size());
        for (size_t i = 0; i < mix.size(); ++i) pcm[i] = static_cast<short>(std::lround(std::max(-1.0, std::min(1.0, mix[i])) * 32767));
        write_wav(folder + "/butterfly-" + scene.name + ".wav", pcm);
    }
}

// Notes from a list of MIDI numbers, one every `step` seconds, each held `length`.
std::vector<Note> phrase(std::initializer_list<int> midi, double step, double length, double from = 0.1) {
    std::vector<Note> out;
    double at = from;
    for (int m : midi) {
        if (m > 0) out.push_back({at, length, m});
        at += step;
    }
    return out;
}
std::vector<Note> chord(std::initializer_list<int> midi, double at, double length) {
    std::vector<Note> out;
    for (int m : midi) out.push_back({at, length, m});
    return out;
}
std::vector<Note> join(std::vector<Note> a, const std::vector<Note>& b) {
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end(), [](const Note& x, const Note& y) { return x.at < y.at; });
    return a;
}
}

int main(int argc, char** argv) {
    const std::string folder = argc > 1 ? argv[1] : "";
    std::vector<Scene> scenes = {
        {"specimen", 8, {0.764, 0.5, 0.75, 0.9, 0.844, 0.25, 0.4, 0.15},
         phrase({60, 64, 67, 72, 71, 67, 64, 62, 60}, 0.55, 0.45), {}},
        {"glass-keys", 7, {0.8284, 0.812, 0.666, 1.0, 0.608, 0.12, 0.6, 0.0},
         phrase({69, 72, 76, 81, 76, 72, 69, 72, 76, 79, 83, 79, 76, 72, 74, 0, 81}, 0.26, 0.2), {}},
        {"gamelan", 10, {0.8, 0.4, 0.83, 1.0, 0.891, 0.5, 0.3, 0.08},
         join(phrase({62, 64, 67, 69, 72, 69, 67, 64, 62, 67, 64, 62}, 0.7, 0.6), phrase({38, 0, 0, 0, 38, 0, 0, 0, 38}, 0.7, 2.5)), {}},
        {"shimmer", 10, {0.62, 0.329, 0.5, 0.0, 0.815, 0.4, 0.5, 0.55},
         join(chord({48, 55, 59, 64}, 0.1, 4.6), chord({45, 52, 59, 60, 67}, 5.0, 4.4)), {}},
        {"struck-bass", 7, {0.6667, 0.577, 0.5, 0.8, 0.756, 0.125, 0.5, 0.0},
         phrase({36, 36, 43, 46, 48, 0, 46, 43, 36, 39, 41, 43}, 0.4, 0.3), {}},
        {"fractal", 12, {0.54, 0.5, 0.934, 0.35, 0.946, 0.7, 0.2, 0.85},
         join(chord({48, 54, 59}, 0.1, 7.0), phrase({71, 0, 66, 0, 0, 73}, 0.9, 1.5, 3.0)), {}},
        {"chime", 7, {0.5, 0.235, 0.896, 1.0, 0.78, 0.6, 0.5, 0.0},
         phrase({84, 79, 88, 86, 0, 84, 91, 0, 79, 84}, 0.6, 0.3), {}},
        {"bowed-glass", 10, {0.8284, 0.671, 0.666, 0.15, 0.869, 0.3, 0.6, 0.4},
         join(chord({50, 57, 65}, 0.1, 4.5), chord({48, 55, 64, 67}, 5.0, 4.5)), {}},
        // Flux from 0.30 to 0.50 over 14 s under a bell struck every 1.4 s: the clusters split, merge and regroup.
        {"flux-sweep", 16, {0.6, 0.5, 0.75, 1.0, 0.844, 0.25, 0.4, 0.0},
         phrase({57, 50, 57, 50, 57, 50, 57, 50, 57, 50, 57}, 1.4, 1.2), {{FLUX, 0.6, 1.0, 0, 14}}},
    };
    for (const Scene& s : scenes) play(folder, s);
    std::printf(failures ? "%d scene(s) failed\n" : "all scenes passed\n", failures);
    return failures ? 1 : 0;
}
