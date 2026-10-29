// Renders Recur's demo WAVs: each preset (tools/presets.h) held on its source
// (tools/make_sources.py), and one played as a triad. Mono, with a 3 ms fade
// in and 30 ms fade out standing in for the OP-XY's envelope. Keep the results
// out of git.
//   clang++ -std=c++11 -O2 -I<open-xy>/sdk/include tools/render.cpp -o <folder>/render
//   <folder>/render <folder of sources> <out folder> [sweep [steps]]
#include "../sample.cpp"
#include "presets.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;

std::vector<opxy::i16> data;
u32 channels = 1, rate = 44100, frames = 0;

bool load(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(f);
    auto word = [&](size_t at) { return u32(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | bytes[at + 3] << 24); };
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const u32 length = word(at + 4);
        if (!std::memcmp(&bytes[at], "fmt ", 4)) { channels = bytes[at + 10]; rate = word(at + 12); }
        if (!std::memcmp(&bytes[at], "data", 4)) {
            data.resize(length / 2);
            std::memcpy(data.data(), &bytes[at + 8], data.size() * 2);
            frames = u32(data.size()) / channels;
            return true;
        }
        at += 8 + length + (length & 1);
    }
    return false;
}

void save(const std::string& path, const std::vector<opxy::i16>& out) {
    FILE* f = std::fopen(path.c_str(), "wb");
    const u32 bytes = u32(out.size()) * 2;
    auto put = [&](u32 v, int n) { for (int i = 0; i < n; ++i) std::fputc((v >> (8 * i)) & 255, f); };
    std::fwrite("RIFF", 1, 4, f); put(36 + bytes, 4); std::fwrite("WAVEfmt ", 1, 8, f);
    put(16, 4); put(1, 2); put(1, 2); put(44100, 4); put(88200, 4); put(2, 2); put(16, 2);
    std::fwrite("data", 1, 4, f); put(bytes, 4);
    std::fwrite(out.data(), 2, out.size(), f);
    std::fclose(f);
}

i32 at(double v) { return i32(v * 2147483647.0); }

// A held chord of notes (semitones from the sample's root) with a preset's controls.
struct Take { const char* file; const Preset* preset; double seconds; int notes[3]; u32 count; };

// The render of a take: its voices summed, then a 3 ms fade in and 30 ms out.
std::vector<opxy::i16> render(const Take& take) {
    const u32 blocks = u32(take.seconds * 44100 / opxy::block_frames), total = blocks * opxy::block_frames;
    std::vector<double> sum(total, 0.0);
    for (u32 v = 0; v < take.count; ++v) {
        i32 out[opxy::block_frames], parameters[8];
        for (u32 i = 0; i < 8; ++i) parameters[i] = at(take.preset->values[i]);
        u32 pitch[opxy::block_frames];
        double ratio = 1;
        const int n = take.notes[v];
        for (int s = 0; s < (n < 0 ? -n : n); ++s) ratio *= n < 0 ? 0.943874 : 1.059463;
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = u32(25480119.0 * ratio + 0.5);
        opxy::SampleBlock block = opxy::SampleBlock();
        block.output = out; block.parameters = parameters; block.pitch = pitch;
        block.region.begin = 0; block.region.end = frames; block.region.level = opxy::sample_unity_level;
        block.transport.bpm_x10 = 1200;
        block.output_channels = 1;
        static recur::Voice voice;
        const opxy::SampleView view = {data.data(), frames, rate, channels};
        recur::initialize(voice, view);
        recur::start(voice);
        for (u32 b = 0; b < blocks; ++b) {
            recur::render_sample(voice, block);
            for (u32 i = 0; i < opxy::block_frames; ++i) sum[b * opxy::block_frames + i] += out[i] / 65536.0;   // half scale peaks at -6 dBFS
        }
    }
    std::vector<opxy::i16> wav(total);
    for (u32 n = 0; n < total; ++n) {
        double g = 1;
        if (n < 132) g = n / 132.0;
        if (total - n < 1323) g = (total - n) / 1323.0;
        const double v = sum[n] * g;
        wav[n] = opxy::i16(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return wav;
}
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: render <folder of sources> <out folder> [sweep]\n"); return 1; }
    if (argc > 3 && std::string(argv[3]) == "sweep") {
        // Each control in steps of 5% (or 25%, with "sweep 4") over Recur's preset, on the
        // drums, as sweep-<control>-<percent>.wav: tools/sweep.py measures them.
        if (!load((std::string(argv[1]) + "/source-drums.wav").c_str())) return 1;
        const u32 steps = argc > 4 ? u32(std::atoi(argv[4])) : 20;
        for (u32 c = 0; c < 8; ++c)
            for (u32 v = 0; v <= steps; ++v) {
                static Preset p;
                p = presets[0];
                p.values[c] = double(v) / steps;
                const Take take = {"", &p, 8.2, {0, 0, 0}, 1};
                char name[64];
                std::snprintf(name, sizeof name, "/sweep-%u-%u.wav", c, v * 100 / steps);
                save(std::string(argv[2]) + name, render(take));
            }
        return 0;
    }
    // Each preset on its source, long enough to come round, and a played triad.
    const double seconds[] = {8.2, 8.2, 14.2, 10.2, 6.2, 8.2, 6.2, 7.2};
    std::vector<Take> takes;
    static char files[16][48];
    u32 n = 0;
    for (const Preset& p : presets) {
        std::snprintf(files[n], sizeof files[n], "recur-%s", p.name);
        for (char* c = files[n]; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
        takes.push_back(Take{files[n], &p, seconds[n], {0, 0, 0}, 1});
        ++n;
    }
    takes.push_back(Take{"recur-triad", &presets[2], 8.2, {0, 3, 7}, 3});   // Phrase played as a minor triad
    for (const Take& take : takes) {
        data.clear();
        const std::string source = std::string(argv[1]) + "/source-" + take.preset->source + ".wav";
        if (!load(source.c_str())) { std::fprintf(stderr, "no %s\n", source.c_str()); return 1; }
        save(std::string(argv[2]) + "/" + take.file + ".wav", render(take));
        std::printf("%s.wav\n", take.file);
    }
    return 0;
}
