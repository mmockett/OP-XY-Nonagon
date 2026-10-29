// Runs Recur's screen offline through a scripted session at 60 frames a
// second: counts the nodes each frame changes (a reshaped outline counts as
// one), checks every frame is valid, and writes chosen frames as JSON for
// tools/frame2svg.py to draw.
//   clang++ -std=c++11 -O2 -I<open-xy>/sdk/include tools/screen.cpp -o <folder>/screen
//   <folder>/screen source.wav <frames folder> [every | sheet | prints]
#include "../display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
opxy::draw::SampleOverview overview;
}
const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

namespace {
// The overview of a 16-bit mono WAV: its length and 64 peaks.
bool load(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(f);
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const u32 length = u32(bytes[at + 4] | bytes[at + 5] << 8 | bytes[at + 6] << 16 | bytes[at + 7] << 24);
        if (!std::memcmp(&bytes[at], "data", 4)) {
            const opxy::i16* data = reinterpret_cast<const opxy::i16*>(&bytes[at + 8]);
            const u32 frames = length / 2;
            overview.assigned = overview.loaded = 1;
            overview.frames = frames; overview.rate = 44100; overview.begin = 0; overview.end = frames;
            for (u32 b = 0; b < 64; ++b) {
                i32 peak = 0;
                for (u32 i = b * frames / 64; i < (b + 1) * frames / 64; ++i) peak = data[i] < 0 ? (-data[i] > peak ? -data[i] : peak) : (data[i] > peak ? data[i] : peak);
                overview.peaks[b] = opxy::u8(peak * 255 / 32768);
            }
            return true;
        }
        at += 8 + length + (length & 1);
    }
    return false;
}

i32 at(double v) { return i32(v * 2147483647.0); }
i32 choice_of(u32 n, u32 count) { return at((n + 0.5) / count); }

void dump(const recur::Frame& frame, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    std::fprintf(f, "{\"paths\": [], \"nodes\": [");
    for (u32 n = 0; n < recur::Frame::node_count; ++n) {
        const opxy::draw::Node& d = frame.nodes[n];
        std::fprintf(f, "%s[%d, %d, %d, %d, %d, %u, %u]", n ? ", " : "", d.x, d.y, d.scale, d.cos, d.sin, d.shade, u32(d.paint));
    }
    std::fprintf(f, "], \"texts\": [");
    for (u32 t = 0; t < recur::Frame::text_count; ++t) {
        const opxy::draw::Text& d = frame.texts[t];
        std::fprintf(f, "%s[%d, %d, %d, \"%s\"]", t ? ", " : "", d.x, d.y, d.scale, d.chars);
    }
    std::fprintf(f, "]}\n");
    std::fclose(f);
}

struct Counter {
    recur::Frame last;
    bool first = true;
    u32 frames = 0, total = 0, peak = 0, invalid = 0;
    void count(const recur::Frame& frame) {
        if (!opxy::draw::valid(frame)) ++invalid;
        u32 changed = 0;
        if (!first) {
            for (u32 n = 0; n < recur::Frame::node_count; ++n) {
                if (std::memcmp(&frame.nodes[n], &last.nodes[n], sizeof(opxy::draw::Node))) ++changed;
            }
            if (changed > 20 && std::getenv("CHURN")) {
                std::printf("frame %u: %u changes:", frames, changed);
                for (u32 n = 0; n < recur::Frame::node_count; ++n)
                    if (std::memcmp(&frame.nodes[n], &last.nodes[n], sizeof(opxy::draw::Node))) std::printf(" %u", n);
                std::printf("\n");
            }
            ++frames;
            total += changed;
            if (changed > peak) peak = changed;
        }
        first = false;
        last = frame;
    }
};
}

int main(int argc, char** argv) {
    if (argc < 3 || !load(argv[1])) { std::fprintf(stderr, "usage: screen source.wav folder [every]\n"); return 1; }
    const std::string folder = argv[2];
    const u32 every = argc > 3 ? u32(std::atoi(argv[3])) : 0;   // also write every nth frame, for a GIF
    static recur::DisplayState state;
    static recur::Frame frame;
    opxy::draw::Input input = {};
    const double defaults[8] = {0, 0.5, 0.583333, 0.083333, 0, 0.4375, 0.7, 0.071429};
    for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
    input.track = 1;
    input.elapsed = 17;   // 1/60 s, Q10
    recur::display_reset(state);
    if (argc > 3 && std::string(argv[3]) == "prints") {
        // Each map's print at rest and scrambled (depth full, half way round
        // its period) on each grid.
        const char* maps[6] = {"cat", "baker", "gold", "pell", "shear", "hex"};
        for (u32 m = 0; m < 6; ++m)
            for (u32 g = 0; g < 3; ++g)
                for (u32 scrambled = 0; scrambled < 2; ++scrambled) {
                    for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
                    input.slots[recur::kMap] = choice_of(m, recur::map_count);
                    input.slots[recur::kGrid] = choice_of(g, recur::grid_count);
                    input.slots[recur::kDepth] = at(1);
                    input.slots[recur::kStep] = at(scrambled ? 0.5 : 0);
                    frame = recur::Frame();
                    recur::display_reset(state);
                    for (u32 f = 0; f < 40; ++f) recur::display(state, input, frame);
                    char name[64];
                    std::snprintf(name, sizeof name, "/print-%s-%u-%s.json", maps[m], 2u << g, scrambled ? "scrambled" : "rest");
                    dump(frame, folder + name);
                }
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "sheet") {
        // The contact sheet: each control at 0, 0.5 and 1 (map at each of its
        // six), the rest at their defaults, at rest; and the screen with no sample.
        const char* names[8] = {"pos", "grid", "slice", "map", "step", "rate", "depth", "chrd"};
        for (u32 c = 0; c < 8; ++c)
            for (u32 v = 0; v <= (c == recur::kMap ? 5u : 2u); ++v) {
                for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
                input.slots[c] = c == recur::kMap ? choice_of(v, recur::map_count) : at(v * 0.5);
                frame = recur::Frame();
                recur::display_reset(state);
                for (u32 f = 0; f < 40; ++f) recur::display(state, input, frame);
                char name[64];
                if (c == recur::kMap) std::snprintf(name, sizeof name, "/sheet-map-%u.json", v);
                else std::snprintf(name, sizeof name, "/sheet-%s-%u.json", names[c], v * 50);
                dump(frame, folder + name);
            }
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        overview.assigned = 0;
        frame = recur::Frame();
        recur::display_reset(state);
        for (u32 f = 0; f < 5; ++f) recur::display(state, input, frame);
        dump(frame, folder + "/sheet-empty.json");
        return 0;
    }
    Counter counter, playing;
    u32 serial = 0, offs = 0, t = 0, written = 0;
    auto frame_at = [&](bool held, bool count_playing) {
        input.held_notes = held ? 1 : 0;
        recur::display(state, input, frame);
        counter.count(frame);
        if (count_playing) playing.count(frame);
        if (every && t % every == 0) {
            char name[64];
            std::snprintf(name, sizeof name, "/gif-%04u.json", written++);
            dump(frame, folder + name);
        }
        ++t;
    };
    auto hold = [&](u32 n) { for (u32 i = 0; i < n; ++i) frame_at(true, true); };
    auto rest = [&](u32 n) { for (u32 i = 0; i < n; ++i) frame_at(false, false); };
    auto press = [&]() { input.note_serial = ++serial; playing.first = true; };
    auto release = [&]() { input.note_off_serial = ++offs; };

    rest(60);
    dump(frame, folder + "/rest.json");
    // A note at the defaults: the cat on 4 x 4 tiles, a new iteration each bar
    // (2 s at 120 BPM), and the cat's period on 4 x 4 is 3.
    press();
    hold(60);
    hold(90);
    dump(frame, folder + "/first.json");      // 2.5 s: iteration 1
    hold(120);
    dump(frame, folder + "/second.json");     // 4.5 s: iteration 2
    hold(120);
    dump(frame, folder + "/home.json");       // 6.5 s: iteration 0, home again
    release();
    rest(30);
    // 8 x 8 tiles, the shear, an iteration each 1/4 note, depth down: pins hold half.
    input.slots[recur::kGrid] = choice_of(2, recur::grid_count); rest(20);
    input.slots[recur::kMap] = choice_of(recur::kShear, recur::map_count);
    input.slots[recur::kRate] = choice_of(5, recur::rate_count);
    input.slots[recur::kDepth] = at(0.45);
    input.slots[recur::kSlice] = choice_of(2, recur::slice_count);
    rest(30);
    press();
    hold(150);
    dump(frame, folder + "/shear.json");
    input.slots[recur::kChrd] = choice_of(4, recur::chord_count);
    input.slots[recur::kMap] = choice_of(recur::kBaker, recur::map_count);
    hold(120);
    dump(frame, folder + "/baker.json");
    release();
    rest(30);
    // hiss: the print stays put and fades to noise, tile by tile, and back.
    input.slots[recur::kSlice] = choice_of(0, recur::slice_count);
    input.slots[recur::kMap] = choice_of(recur::kCat, recur::map_count);
    input.slots[recur::kRate] = choice_of(7, recur::rate_count);
    rest(30);
    press();
    hold(90);
    dump(frame, folder + "/hiss.json");
    release();
    rest(60);
    std::printf("frames %u, invalid %u\n", counter.frames, counter.invalid);
    std::printf("node churn over the whole session: mean %.1f, peak %u a frame\n", double(counter.total) / counter.frames, counter.peak);
    std::printf("while notes play: mean %.1f, peak %u a frame\n", double(playing.total) / playing.frames, playing.peak);
    if (every) std::printf("%u GIF frames\n", written);
    return counter.invalid ? 1 : 0;
}
