// Runs Ghost's screen through a scripted session on the host: checks every
// frame, counts the nodes that change each frame (a path whose key changes
// counts as its node), and writes chosen frames as JSON for tools/frame2svg.py.
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/frames.cpp -o frames && ./frames <folder> [fps]
#include "../display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
opxy::draw::SampleOverview overview;
}
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

i32 at(double v) { return i32(v * 2147483647.0); }

void dump(const ghost::Frame& f, const std::string& path) {
    FILE* out = std::fopen(path.c_str(), "w");
    std::fprintf(out, "{\"nodes\":[");
    for (u32 i = 0; i < ghost::Frame::node_count; ++i) {
        const Node& n = f.nodes[i];
        std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, u32(n.paint));
    }
    std::fprintf(out, "],\"paths\":[");
    for (u32 p = 0; p < ghost::Frame::path_count; ++p) {
        std::fprintf(out, "%s[", p ? "," : "");
        for (u32 i = 0; i < ghost::Frame::point_count; ++i)
            std::fprintf(out, "%s[%d,%d]", i ? "," : "", f.paths[p].points[i].x, f.paths[p].points[i].y);
        std::fprintf(out, "]");
    }
    std::fprintf(out, "],\"texts\":[");
    for (u32 i = 0; i < ghost::Frame::text_count; ++i)
        std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", i ? "," : "", f.texts[i].x, f.texts[i].y, f.texts[i].scale, f.texts[i].chars);
    std::fprintf(out, "]}\n");
    std::fclose(out);
}

struct Session {
    ghost::DisplayState state;
    ghost::Frame frame, last;
    Input input;
    double t = 0;
    u32 fps;
    u32 frames = 0, changed = 0, peak = 0, peak_frame = 0;
    u32 held_frames = 0, held_changed = 0, idle_frames = 0, idle_changed = 0, invalid = 0;
    std::string folder;
    Session(const std::string& dir, u32 rate) : fps(rate), folder(dir) {
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        std::memset(&input, 0, sizeof input);
        const double defaults[8] = {0.6, 0.6, 0.6, 0.3, 0.35, 0.2, 0.4, 0};   // definition.json's defaults (Seance)
        for (u32 i = 0; i < 8; ++i) input.slots[i] = at(defaults[i]);
        for (u32 i = 0; i < 4; ++i) { input.shown[i] = opxy::u8(i); input.parameters[i] = input.slots[i]; }
        input.track = 1;
        ghost::display_reset(state);
    }
    u32 count_changes() {
        u32 n = 0;
        for (u32 i = 0; i < ghost::Frame::node_count; ++i) {
            const bool path_changed = i < ghost::Frame::path_count && frame.paths[i].key != last.paths[i].key;
            if (path_changed || std::memcmp(&frame.nodes[i], &last.nodes[i], sizeof(Node))) ++n;
        }
        return n;
    }
    // Runs for seconds, the controls and notes as they are.
    u32 every = 0, dumped = 0;   // gif mode: dump every n-th frame as frame-NNNN.json
    void run(double seconds, const char* shot = 0) {
        const u32 count = u32(seconds * fps + 0.5);
        for (u32 k = 0; k < count; ++k) {
            if (every && frames % every == 0) {
                char name[32];
                std::snprintf(name, sizeof name, "/frame-%04u.json", dumped++);
                dump(frame, folder + name);
            }
            input.elapsed = i32(1024 / fps);
            for (u32 i = 0; i < 4; ++i) input.parameters[i] = input.slots[input.shown[i]];
            ghost::display(state, input, frame);
            if (!valid(frame)) ++invalid;
            const u32 c = frames ? count_changes() : 0;
            if (frames) {
                changed += c;
                if (c > peak) { peak = c; peak_frame = frames; }
                if (input.held_notes) { ++held_frames; held_changed += c; }
                else { ++idle_frames; idle_changed += c; }
            }
            last = frame;
            ++frames;
            t += 1.0 / fps;
        }
        if (shot) dump(frame, folder + "/" + shot + ".json");
    }
    void press(u32 notes = 1) { input.held_notes += notes; input.note_serial += notes; }
    void release() { input.note_off_serial += input.held_notes; input.held_notes = 0; }
    void turn(u32 slot, double to, double seconds) {
        const double from = input.slots[slot] / 2147483647.0;
        const u32 count = u32(seconds * fps + 0.5);
        for (u32 k = 1; k <= count; ++k) { input.slots[slot] = at(from + (to - from) * k / count); run(1.0 / fps); }
    }
    void page(u32 p) { for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(i + 4 * p); }
};
}

// The contact sheet (docs/controls.png): each control at 0, 50 and 100, the
// others at their defaults, a note held 0.7 s; then the empty state.
int sheet(const std::string& dir) {
    const char* const names[8] = {"ring", "voice", "split", "trade", "beat", "orbit", "near", "bed"};
    const double values[3] = {0, 0.5, 1};
    for (u32 c = 0; c < 8; ++c)
        for (u32 v = 0; v < 3; ++v) {
            Session s(dir, 60);
            if (c == ghost::kBed) {   // a sample loaded, its peaks a breathy phrase
                overview.assigned = overview.loaded = 1; overview.frames = 264600; overview.rate = 44100; overview.end = 264600;
                for (u32 i = 0; i < 64; ++i) overview.peaks[i] = opxy::u8(60 + (i * 53 % 29) * 4 + ((i / 8) % 2) * 50);
            } else overview.assigned = overview.loaded = 0;
            s.input.slots[c] = at(values[v]);
            s.press();
            char name[48];
            std::snprintf(name, sizeof name, "sheet-%s-%u", names[c], u32(values[v] * 100));
            s.run(0.7, name);
        }
    overview.assigned = overview.loaded = 0;
    for (u32 v = 0; v < 3; ++v) {   // no sample: bed 0 plays on; bed asking for one shows the hint
        Session s(dir, 60);
        s.input.slots[ghost::kBed] = at(values[v]);
        s.press();
        char name[48];
        std::snprintf(name, sizeof name, "sheet-empty-%u", u32(values[v] * 100));
        s.run(0.7, name);
    }
    return 0;
}

// A short performance for docs/ghost.gif: 12 frames a second of a 60 fps screen.
int gif(const std::string& dir) {
    Session s(dir, 60);
    s.every = 5;
    s.run(0.6);
    s.press(); s.run(1.6); s.release(); s.run(0.8);
    s.turn(ghost::kVoice, 0.0, 0.6); s.turn(ghost::kVoice, 1.0, 0.8); s.turn(ghost::kVoice, 0.6, 0.4);
    s.turn(ghost::kSplit, 1.0, 0.3); s.turn(ghost::kTrade, 1.0, 0.2);
    s.press(); s.run(2.0); s.release(); s.run(0.4);
    s.page(1); s.run(0.3);
    s.turn(ghost::kOrbit, 0.9, 0.3); s.turn(ghost::kNear, 0.85, 0.3); s.turn(ghost::kBeat, 0.8, 0.2);
    s.press(3); s.run(3.0); s.release(); s.run(1.2);
    std::printf("%u frames\n", s.dumped);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "gif") return gif(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "sheet") return sheet(argv[2]);
    const std::string dir = argc > 1 ? argv[1] : ".";
    const u32 fps = argc > 2 ? u32(std::atoi(argv[2])) : 60;
    Session s(dir, fps);
    s.run(1.0, "rest");
    s.press(); s.run(0.15, "arriving"); s.run(1.5, "seance");
    s.release(); s.run(1.2, "fading");
    s.turn(ghost::kVoice, 0.0, 0.4); s.turn(ghost::kVoice, 1.0, 0.6); s.run(0.3, "voice-ee");
    s.turn(ghost::kVoice, 0.6, 0.3);
    s.turn(ghost::kSplit, 1.0, 0.3); s.turn(ghost::kTrade, 1.0, 0.3);
    s.press(); s.run(1.6, "split-trade"); s.release(); s.run(0.8);
    s.turn(ghost::kSplit, 0.6, 0.2); s.turn(ghost::kTrade, 0.3, 0.2);
    s.page(1); s.run(0.3);
    s.turn(ghost::kBeat, 0.8, 0.3); s.turn(ghost::kOrbit, 0.9, 0.3); s.turn(ghost::kNear, 0.9, 0.3);
    s.press(3); s.run(2.0, "circling-three"); s.release(); s.run(1.0);
    s.turn(ghost::kOrbit, 0.25, 0.2); s.turn(ghost::kNear, 0.1, 0.3); s.run(0.4, "far-spread");
    // A bed with a sample loaded.
    overview.assigned = overview.loaded = 1; overview.frames = 441000; overview.rate = 44100; overview.end = 441000;
    s.turn(ghost::kBed, 1.0, 0.4); s.press(); s.run(1.2, "bed"); s.release(); s.run(0.6);
    s.page(0); s.run(0.3);
    // Reduced motion: notes snap, nothing moves while held.
    s.input.reduced_motion = 1; s.press(); s.run(1.0, "reduced"); s.release(); s.run(0.5);
    // Idle: once the last fade is over, nothing changes.
    s.input.reduced_motion = 0; s.run(3.0);
    const u32 before = s.changed;
    s.run(1.0);
    const u32 idle = s.changed - before;
    std::printf("%u frames at %u fps, %u invalid; nodes changed a frame: mean %.2f, peak %u (frame %u)\n",
                s.frames, fps, s.invalid, double(s.changed) / (s.frames - 1), s.peak, s.peak_frame);
    std::printf("  while notes are held: mean %.2f over %u frames; otherwise (including control turns): mean %.2f\n",
                double(s.held_changed) / s.held_frames, s.held_frames, double(s.idle_changed) / s.idle_frames);
    std::printf("  idle, a second after the last fade: %u nodes changed\n", idle);
    std::printf("DisplayState %u bytes\n", u32(sizeof(ghost::DisplayState)));
    return s.invalid || idle ? 1 : 0;
}
