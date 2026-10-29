// Runs Ghost's screen through a scripted session on the host: checks every
// frame, counts the nodes that change each frame (a path whose key changes
// counts as its node), and writes chosen frames as JSON for tools/frame2svg.py.
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/frames.cpp -o frames && ./frames <folder> [fps]
//   ./frames clear: checks nothing drawn ever reaches the footlights or the hint
#include "../display.cpp"
#include <algorithm>
#include <cmath>
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

// A sample loaded, its overview's peaks a breathy phrase.
void load() {
    overview.assigned = overview.loaded = 1; overview.frames = 264600; overview.rate = 44100; overview.end = 264600;
    for (u32 i = 0; i < 64; ++i) overview.peaks[i] = opxy::u8(60 + (i * 53 % 29) * 4 + ((i / 8) % 2) * 50);
}

// What the OP-XY does with a shown reshaping outline, beyond the SDK's rules
// (as MNMD's tools/check_frame.cpp checks): it merges neighbouring points
// within an eighth of a pixel on both axes, draws a closing edge under a
// quarter of a pixel as a long sliver, and draws an outline with no area (a
// line traced out and back, or a turn straight back at a point) as streaks.
// A repeated last point only pads. Says what breaks, or nothing.
const i32 apart = unit / 8;
u32 outline_faults = 0;
void check_outlines(const ghost::Frame& f, const char* when) {
    for (u32 p = 0; p < ghost::Frame::path_count; ++p) {
        if (f.nodes[p].x >= hidden_x * unit) continue;
        Point q[64];
        u32 m = 0;
        for (u32 i = 0; i < 64; ++i) {
            const Point& r = f.paths[p].points[i];
            if (!m || r.x != q[m - 1].x || r.y != q[m - 1].y) q[m++] = r;
        }
        const char* fault = 0;
        u32 at = 0;
        for (u32 i = 0; i < m && !fault; ++i) {
            const Point& a = q[i], &b = q[(i + 1) % m];
            const i32 dx = b.x - a.x, dy = b.y - a.y;
            if (i + 1 < m && dx > -apart && dx < apart && dy > -apart && dy < apart) { fault = "neighbouring points merge"; at = i; }
        }
        if (!fault && m > 2) {
            const i32 dx = q[0].x - q[m - 1].x, dy = q[0].y - q[m - 1].y;
            const i32 span = std::max(std::abs(dx), std::abs(dy));
            if (span < 2 * apart) { fault = "closing edge under a quarter of a pixel"; at = m - 1; }
        }
        double area = 0;
        for (u32 i = 0; i < m && !fault; ++i) {
            const Point& a = q[(i + m - 1) % m], &b = q[i], &c = q[(i + 1) % m];
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            const double cross = ux * vy - uy * vx, dot = ux * vx + uy * vy, lengths = std::hypot(ux, uy) * std::hypot(vx, vy);
            if (dot < 0 && std::fabs(cross) < 0.02 * lengths) { fault = "turns straight back"; at = i; }
            area += (double(b.x) * c.y - double(c.x) * b.y) / 2;
        }
        if (!fault && std::fabs(area) < double(unit) * unit) fault = "has no area";
        if (fault) {
            if (outline_faults < 12) std::printf("%s: path %u %s (point %u of %u)\n", when, p, fault, at, m);
            ++outline_faults;
        }
    }
}

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
        const double defaults[8] = {0.6, 0.6, 0.6, 0.4, 0.2, 0.2, 0.4, 0.5};   // definition.json's defaults (Seance)
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
            check_outlines(frame, folder.c_str());
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

// The contact sheets (docs/controls.png, controls-1x.png): each control at 0,
// 50 and 100, the others at their defaults, a sample loaded and a note held
// 0.7 s; then no sample, bed at 0, 50 and 100: the hint, and bed's whisper.
int sheet(const std::string& dir) {
    const char* const names[8] = {"ring", "voice", "split", "trade", "beat", "orbit", "near", "bed"};
    const double values[3] = {0, 0.5, 1};
    for (u32 c = 0; c < 8; ++c)
        for (u32 v = 0; v < 3; ++v) {
            Session s(dir, 60);
            load();   // a sample loaded, so the hint is away
            s.input.slots[c] = at(values[v]);
            s.press();
            char name[48];
            std::snprintf(name, sizeof name, "sheet-%s-%u", names[c], u32(values[v] * 100));
            s.run(0.7, name);
        }
    overview.assigned = overview.loaded = 0;
    for (u32 v = 0; v < 3; ++v) {   // no sample: the hint, and bed is Ghost's own whisper
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
    overview.assigned = overview.loaded = 0;   // no sample: the hint shows, and Ghost plays on
    s.run(0.6);
    s.press(); s.run(1.6); s.release(); s.run(0.8);
    load();                                    // a sample loaded: the hint goes
    s.turn(ghost::kVoice, 0.0, 0.6); s.turn(ghost::kVoice, 1.0, 0.8); s.turn(ghost::kVoice, 0.6, 0.4);
    s.turn(ghost::kSplit, 1.0, 0.3); s.turn(ghost::kTrade, 1.0, 0.2);
    s.press(); s.run(2.0); s.release(); s.run(0.4);
    s.page(1); s.run(0.3);
    s.turn(ghost::kOrbit, 0.9, 0.3); s.turn(ghost::kNear, 0.85, 0.3); s.turn(ghost::kBeat, 0.8, 0.2);
    s.press(3); s.run(3.0); s.release(); s.run(1.2);
    std::printf("%u frames\n", s.dumped);
    return 0;
}

// The footlights and the hint under the apron are never crossed: over a
// sweep of every control that moves or sizes what is drawn, with three notes
// held and let go, with no sample and with one, the lowest point any figure,
// halo, mouth or the chalk path reaches over the apron (x 60..420) stays
// above the footlights' hoods (y 193.8), nothing reaches the pilasters (x 58
// and 422), and every outline keeps the OP-XY's drawing rules
// (check_outlines). Fails otherwise.
int clear() {
    const double ring[4] = {0, 0.5, 0.8, 1}, two[2] = {0, 1};
    double lowest = 0, left = 480, right = 0;
    u32 sessions = 0;
    for (u32 loaded = 0; loaded < 2; ++loaded)   // no sample, then one: bed frays by its peaks
    for (double rg : ring) for (u32 n = 0; n <= 10; ++n) for (u32 o = 0; o <= 10; ++o)
    for (double vo : two) for (double sp : two) for (double tr : two) for (double bt : two) for (double bd : two) {
        if (loaded) load(); else overview.assigned = overview.loaded = 0;
        Session s(".", 30);
        const double v[8] = {rg, vo, sp, tr, bt, o / 10.0, n / 10.0, bd};
        for (u32 i = 0; i < 8; ++i) s.input.slots[i] = at(v[i]);
        s.input.note_serial = sessions * 7;
        s.press(3);
        for (u32 f = 0; f < 120; ++f) {
            if (f == 90) s.release();
            s.run(1.0 / 30);
            for (u32 i = 0; i < ghost::Frame::node_count; ++i) {
                if ((i >= ghost::header_first && i < ghost::header_first + 8) || i == ghost::hint_cover || i == ghost::mask || i == ghost::twin_mask) continue;   // the silhouettes lie inside the figures
                const Node& d = s.frame.nodes[i];
                if (d.x == hidden_x * unit && d.y == hidden_y * unit) continue;
                // The node's outline: its path, or the shape make_panel.py draws it with.
                double px[64], py[64]; u32 count = 0;
                if (i < ghost::Frame::path_count) {
                    for (u32 k = 0; k < 64; ++k) { px[k] = s.frame.paths[i].points[k].x / 1024.0; py[k] = s.frame.paths[i].points[k].y / 1024.0; }
                    count = 64;
                } else if (i == ghost::twin_mouth) {
                    for (u32 k = 0; k < 24; ++k) { px[k] = 3 * std::cos(k * M_PI / 12); py[k] = 3.5 * std::sin(k * M_PI / 12); }
                    count = 24;
                } else if (i >= ghost::extra_first && i < ghost::extra_first + 2) {
                    for (u32 k = 0; k < 64; ++k) { px[k] = ghost::figure_bowed[2 * k] * 0.2; py[k] = ghost::figure_bowed[2 * k + 1] * 0.2; }
                    count = 64;
                } else {   // a halo
                    for (u32 k = 0; k < 40; ++k) { px[k] = 15 * std::cos(k * M_PI / 20); py[k] = 15 * std::sin(k * M_PI / 20); }
                    count = 40;
                }
                const double k = d.scale / 1024.0, c = d.cos / 1024.0, sn = d.sin / 1024.0;
                for (u32 j = 0; j < count; ++j) {
                    const double x = k * (c * px[j] - sn * py[j]) + d.x / 1024.0, y = k * (sn * px[j] + c * py[j]) + d.y / 1024.0;
                    if (x > 60 && x < 420 && y > lowest) lowest = y;
                    if (i != ghost::floor_path && y > 72) { left = std::fmin(left, x); right = std::fmax(right, x); }
                }
            }
        }
        ++sessions;
    }
    std::printf("footlights and hint: over %u settings, the lowest point drawn over the apron is at y %.1f (the footlights' hoods start at 193.8)\n", sessions, lowest);
    std::printf("  and between the pilasters, from x %.1f to %.1f (their inner faces at 58 and 422)\n", left, right);
    std::printf("  outlines breaking the OP-XY's drawing rules: %u\n", outline_faults);
    return lowest < 193 && left > 59 && right < 421 && !outline_faults ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "clear") return clear();
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
    std::printf("DisplayState %u bytes; outlines breaking the OP-XY's drawing rules: %u\n", u32(sizeof(ghost::DisplayState)), outline_faults);
    return s.invalid || idle || outline_faults ? 1 : 0;
}
