// Plays Mach through a phrase on the host and writes a 24-bit WAV: eight
// voices taken as the OP-XY takes them (a free one, else the oldest note),
// each through an amp envelope like the OP-XY's (a 5 ms attack, full sustain,
// a linear release after note-off), summed with no limiting (full scale is Q31
// full scale). Not part of the machine: tools/render_phrases.py drives it.
//   c++ -x c++ -std=c++11 -O2 -I<sdk>/sdk/include tools/render.cc -o render
//   render < spec
// The spec, one item a line:
//   out <path>   seconds <s>   release <s>   params <eight normalized values>
//   note <on seconds> <off seconds> <MIDI note>
//   trace <path>   (the first note's voice, each block: its time, then each
//                  root's state, step and gain, and the source's pitch factor)
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Note { double on, off; int midi; };

static void wav(const std::string& path, const std::vector<double>& x) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    const unsigned rate = 44100, n = static_cast<unsigned>(x.size()), bytes = n * 3;
    auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned v) { const unsigned short s = static_cast<unsigned short>(v); std::fwrite(&s, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 3); u16(3); u16(24);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (double v : x) {
        const long s = std::lround(std::fmax(-1.0, std::fmin(1.0, v)) * 8388607.0);
        const unsigned char b[3] = {static_cast<unsigned char>(s & 255), static_cast<unsigned char>((s >> 8) & 255),
                                    static_cast<unsigned char>((s >> 16) & 255)};
        std::fwrite(b, 1, 3, f);
    }
    std::fclose(f);
}

int main() {
    std::string out = "out.wav", trace;
    double seconds = 4, release = 0.4;
    double params[8] = {0};
    std::vector<Note> notes;
    char word[64];
    while (std::scanf("%63s", word) == 1) {
        if (!std::strcmp(word, "out")) { char path[1024]; if (std::scanf("%1023s", path) == 1) out = path; }
        else if (!std::strcmp(word, "seconds")) { if (std::scanf("%lf", &seconds) != 1) return 1; }
        else if (!std::strcmp(word, "release")) { if (std::scanf("%lf", &release) != 1) return 1; }
        else if (!std::strcmp(word, "params")) { for (double& p : params) if (std::scanf("%lf", &p) != 1) return 1; }
        else if (!std::strcmp(word, "trace")) { char path[1024]; if (std::scanf("%1023s", path) == 1) trace = path; }
        else if (!std::strcmp(word, "note")) { Note n; if (std::scanf("%lf %lf %d", &n.on, &n.off, &n.midi) != 3) return 1; notes.push_back(n); }
    }
    opxy::i32 p[8];
    for (int i = 0; i < 8; ++i) p[i] = static_cast<opxy::i32>(std::lround(std::fmin(1.0, std::fmax(0.0, params[i])) * 2147483647.0));
    static mach::Voice voices[8];
    static mach::Shared shared;
    int owner[8];
    double started[8];
    for (int v = 0; v < 8; ++v) { mach::initialize(voices[v], shared, v + 1); owner[v] = -1; started[v] = -1; }
    const size_t blocks = static_cast<size_t>(seconds * 44100 / 64);
    std::vector<double> mix(blocks * 64, 0.0);
    opxy::i32 o[64];
    FILE* traced = trace.empty() ? nullptr : std::fopen(trace.c_str(), "w");
    for (size_t b = 0; b < blocks; ++b) {
        const double t = b * 64 / 44100.0;
        for (size_t n = 0; n < notes.size(); ++n)
            if (notes[n].on >= t && notes[n].on < t + 64 / 44100.0) {
                // A free voice (never used, or released and faded), else the oldest note's.
                int pick = -1;
                for (int v = 0; v < 8; ++v) if (owner[v] < 0) { pick = v; break; }
                if (pick < 0) { pick = 0; for (int v = 1; v < 8; ++v) if (started[v] < started[pick]) pick = v; }
                owner[pick] = static_cast<int>(n);
                started[pick] = t;
                mach::start(voices[pick], shared, pick + 1);
            }
        for (int v = 0; v < 8; ++v) {
            if (owner[v] < 0) continue;
            const Note& n = notes[owner[v]];
            opxy::u32 pitch[64];
            const double hz = 440 * std::pow(2.0, (n.midi - 69) / 12.0);
            for (int i = 0; i < 64; ++i) pitch[i] = static_cast<opxy::u32>(std::lround(hz / 44100 * 4294967296.0));
            mach::render(voices[v], shared, v + 1, o, p, pitch);
            if (traced && owner[v] == 0) {
                std::fprintf(traced, "%.5f", t);
                for (const mach::Root& r : voices[v].root) std::fprintf(traced, " %u %d %d", r.state, r.step, r.gain);
                std::fprintf(traced, " %d\n", voices[v].pitch);
            }
            for (int i = 0; i < 64; ++i) {
                const double ts = t + i / 44100.0;
                double gate = std::fmin(1.0, (ts - n.on) / 0.005);
                if (ts > n.off) gate *= std::fmax(0.0, 1 - (ts - n.off) / release);
                mix[b * 64 + i] += gate * o[i] / 2147483648.0;
            }
            if (t > n.off + release) { owner[v] = -1; mach::destroy(voices[v], shared, v + 1); }
        }
    }
    if (traced) std::fclose(traced);
    wav(out, mix);
    return 0;
}
