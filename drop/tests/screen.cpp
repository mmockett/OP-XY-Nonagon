// Drop's screen checks, compiled for the host and run by sdk.py test: the
// display draws a valid frame at every step of five scripted sessions, it is
// still when idle, its drops keep the voices' lobes, and its churn (nodes
// whose transform or paint changed, outlines re-keyed) stays within budget.
// With arguments it measures one scenario and prints chosen frames as JSON
// for tools/frame2svg.py (after Mach's):
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tests/screen.cpp -o screen
//   ./screen <life|chord|sizzle|ratchet|session|gif> [fps] [snapshot seconds ...]
//   ./screen gif 60 every 5     (every fifth frame: tools/make_gif.py)
//   ./screen contact <control> <value>   (one frame for docs/controls.png: tools/make_contact.py)
#include "../display.cpp"
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }

struct Step { double at; int down; int control; double value; };

static void dump(const drop::Frame& f, double t) {
    std::printf("{\"t\":%.3f,\"nodes\":[", t);
    for (unsigned i = 0; i < drop::Frame::node_count; ++i) {
        const opxy::draw::Node& n = f.nodes[i];
        std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint));
    }
    std::printf("],\"paths\":[");
    for (unsigned p = 0; p < drop::Frame::path_count; ++p) {
        std::printf("%s[", p ? "," : "");
        for (unsigned i = 0; i < drop::Frame::point_count; ++i)
            std::printf("%s%d,%d", i ? "," : "", f.paths[p].points[i].x, f.paths[p].points[i].y);
        std::printf("]");
    }
    std::printf("],\"texts\":[");
    for (unsigned i = 0; i < drop::Frame::text_count; ++i) {
        const opxy::draw::Text& x = f.texts[i];
        std::printf("%s[%d,%d,%d,\"%s\"]", i ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::printf("]}\n");
}

struct Churn { double mean, playing, peak; bool valid, still, matched; };
static int every = 0;   // dump every n-th frame
static int contact_control = -1;   // the contact sheet's control, and its value
static double contact_value = 0;

static Churn run(const char* scenario, double fps, const std::vector<double>& snaps, bool quiet);

int main(int argc, char** argv) {
    if (argc > 3 && !std::strcmp(argv[1], "contact")) {
        contact_control = std::atoi(argv[2]);
        contact_value = std::atof(argv[3]);
        std::vector<double> snaps(1, 1.8);
        return run("contact", 60, snaps, true).valid ? 0 : 1;
    }
    if (argc > 1) {
        std::vector<double> snaps;
        if (argc > 4 && !std::strcmp(argv[3], "every")) every = std::atoi(argv[4]);
        else for (int i = 3; i < argc; ++i) snaps.push_back(std::atof(argv[i]));
        const Churn c = run(argv[1], argc > 2 ? std::atof(argv[2]) : 60, snaps, false);
        return c.valid ? 0 : 1;
    }
    bool ok = true;
    // However long its life, a drop boils away within a second of the key's release.
    for (int life = 0; life <= 4; ++life) {
        static drop::DisplayState state;
        static drop::Frame frame, before;
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        drop::display_reset(state);
        opxy::draw::Input input = {};
        input.track = 1;
        input.elapsed = 17;
        for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
        double controls[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, life / 4.0, 0.25};
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
        for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
        for (int n = 0; n < 240; ++n) {
            if (n == 10) { input.note_serial = 1; input.held_notes = 1; }
            if (n == 120) { input.note_off_serial = 1; input.held_notes = 0; }
            before = frame;
            drop::display(state, input, frame);
            if (n >= 120 + 60 && std::memcmp(&frame.nodes, &before.nodes, sizeof frame.nodes)) {
                std::printf("FAIL the screen still moves %.2f s after release at life %d/4\n", (n - 120) / 60.0, life);
                ok = false;
                break;
            }
        }
    }
    for (const char* scenario : {"life", "chord", "sizzle", "ratchet", "session"})
        for (double fps : {60.0, 30.0}) {
            const Churn c = run(scenario, fps, std::vector<double>(), true);
            const bool pass = c.valid && c.still && c.matched && c.mean <= 20 && c.peak <= 32;
            std::printf("%s %s at %.0f fps: churn %.2f a frame on average, %.2f while drops show, %.0f at most%s%s\n",
                        pass ? "ok  " : "FAIL", scenario, fps, c.mean, c.playing, c.peak, c.still ? "" : "; moves when idle",
                        c.matched ? "" : "; lobes differ from the voice's");
            ok = ok && pass;
        }
    return ok ? 0 : 1;
}

static Churn run(const char* scenario, double fps, const std::vector<double>& snaps, bool quiet) {
    // heat, size, liquid, beat, groove, vapour, life, crackle: definition.json's defaults
    double controls[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25};
    std::vector<Step> steps;
    double length = 10;
    if (!std::strcmp(scenario, "life")) {
        steps = {{0.5, 1, -1, 0}, {3.5, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "chord")) {
        controls[6] = 0.45;
        steps = {{0.5, 1, -1, 0}, {0.5, 1, -1, 0}, {0.5, 1, -1, 0}, {2.5, 0, -1, 0}, {2.5, 0, -1, 0}, {2.5, 0, -1, 0},
                 {3.0, 0, 1, 0.5}, {3.5, 1, -1, 0}, {5.0, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "sizzle")) {
        length = 4;
        controls[0] = 0.2;
        steps = {{0.5, 1, -1, 0}, {0.7, 0, -1, 0}, {1.5, 1, -1, 0}, {1.6, 1, -1, 0}, {1.8, 0, -1, 0}, {1.8, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "ratchet")) {
        controls[4] = 1;
        controls[1] = 0.95;
        steps = {{0.5, 1, -1, 0}, {4.0, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "gif")) {
        // docs/drop.gif: a drop's life, a sizzle under the point, a chord, the ratchet.
        length = 15;
        controls[6] = 0.45;
        controls[1] = 1;
        steps = {{0.4, 1, -1, 0}, {1.4, 0, -1, 0},
                 {5.5, 0, 0, 0.3}, {5.6, 0, 0, 0.25}, {6.1, 1, -1, 0}, {6.3, 0, -1, 0},
                 {7.0, 0, 0, 0.35}, {7.1, 0, 0, 0.45}, {7.5, 1, -1, 0}, {7.55, 1, -1, 0}, {7.6, 1, -1, 0},
                 {8.5, 0, -1, 0}, {8.5, 0, -1, 0}, {8.5, 0, -1, 0},
                 {11.0, 0, 4, 0.5}, {11.1, 0, 4, 1.0}, {11.4, 1, -1, 0}, {12.0, 0, -1, 0}};
    } else if (!std::strcmp(scenario, "contact")) {
        // One control set, the others at their defaults: a drop 1.5 s old and a new one 0.2 s old.
        length = 1.85;
        if (contact_control >= 0) controls[contact_control] = contact_value;
        steps = {{0.3, 1, -1, 0}, {1.0, 0, -1, 0}, {1.6, 1, -1, 0}};
    } else if (!std::strcmp(scenario, "session")) {
        length = 16;
        steps = {{0.3, 0, 0, 0.25}, {0.5, 1, -1, 0}, {0.8, 0, -1, 0},   // a sizzle
                 {1.5, 0, 0, 0.4}, {1.7, 0, 0, 0.55}, {2.0, 1, -1, 0}, {2.05, 1, -1, 0}, {3.0, 0, -1, 0}, {3.0, 0, -1, 0},
                 {3.5, 0, 4, 0.7}, {3.6, 0, 5, 0.9}, {3.8, 0, 2, 0.5}, {4.0, 0, 3, 0.8},
                 {5.0, 1, -1, 0}, {6.0, 0, -1, 0}, {7.0, 0, 7, 0.9}, {7.2, 0, 1, 0.4}, {7.4, 0, 6, 0.2},
                 {8.0, 1, -1, 0}, {8.1, 1, -1, 0}, {8.2, 1, -1, 0}, {8.3, 1, -1, 0}, {9.0, 0, -1, 0}, {9.0, 0, -1, 0},
                 {9.0, 0, -1, 0}, {9.0, 0, -1, 0}, {10.0, 0, 0, 0.1}, {11.0, 0, 0, 0.6}, {11.2, 0, 2, 1.0},
                 {12.0, 1, -1, 0}, {13.0, 0, -1, 0}};
    }
    static drop::DisplayState state;
    static drop::Frame frame;
    static drop::Frame last;
    drop::display_reset(state);
    // A voice beside the screen for the first drop of each scenario, to check
    // the screen keeps the same lobes.
    static drop::Voice voice;
    static drop::Shared shared;
    shared.sounding = 0;
    drop::initialize(voice, shared, 1);
    bool voice_on = false, matched = true;
    opxy::draw::Input input = {};
    input.track = 1;
    for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
    const int frames = static_cast<int>(length * fps);
    const opxy::i32 tick = static_cast<opxy::i32>(std::lround(1024 / fps));
    size_t next = 0, snap = 0;
    int held = 0;
    double total = 0, worst = 0, worst_at = 0, total_playing = 0, voice_blocks = 0;
    int playing_frames = 0;
    bool still = true;
    unsigned first_slot = 99;
    bool showed = false;   // drops showed last frame
    double turned_at = -1;  // when a control last turned: the header eases for 0.15 s
    for (int n = 0; n < frames; ++n) {
        const double t = n / fps;
        bool turned = false;
        while (next < steps.size() && steps[next].at <= t) {
            const Step& s = steps[next++];
            if (s.control >= 0) {
                controls[s.control] = s.value;
                turned = true;
                turned_at = t;
                for (unsigned i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(s.control >= 4 ? 4 + i : i);   // its page
            }
            else if (s.down) {
                ++held;
                ++input.note_serial;
                if (!voice_on && first_slot == 99) { drop::start(voice, shared, 1); voice_on = true; first_slot = 0; }
            } else { --held; ++input.note_off_serial; }
        }
        input.held_notes = static_cast<opxy::u32>(held);
        input.elapsed = n ? tick : 0;
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = q31(controls[i]);
        for (unsigned i = 0; i < 4; ++i) input.parameters[i] = input.slots[i];
        drop::display(state, input, frame);
        // The voice, block by block to the same time.
        if (voice_on) {
            voice_blocks += 44100.0 / 64 / fps;
            while (voice_blocks >= 1) {
                opxy::u32 pitch[64];
                for (int i = 0; i < 64; ++i) pitch[i] = 21420000u;
                opxy::i32 o[64];
                drop::render(voice, shared, 1, o, input.slots, pitch);
                voice_blocks -= 1;
            }
            // While its key is held the screen's first drop has its lobes, give or take a block's timing.
            const drop::ScreenDrop& d = state.drops[0];
            if (held && d.used && d.order == 1 && voice.drop.lobes &&
                (d.drop.lobes > voice.drop.lobes + 1 || d.drop.lobes + 1 < voice.drop.lobes)) matched = false;
        }
        if (!opxy::draw::valid(frame)) {
            std::printf("FAIL invalid frame in %s at %.3f s\n", scenario, t);
            return Churn{0, 0, 0, false, false, false};
        }
        int churn = 0;
        if (n) {
            for (unsigned i = 0; i < drop::Frame::node_count; ++i) {
                const opxy::draw::Node &a = frame.nodes[i], &b = last.nodes[i];
                if (a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint)
                    ++churn;
                else if (i < drop::Frame::path_count && frame.paths[i].key != last.paths[i].key) ++churn;
            }
            total += churn;
            bool any = false;
            for (unsigned k = 0; k < 3; ++k) any = any || state.drops[k].used;
            if (any) { total_playing += churn; ++playing_frames; }
            else if (churn && !turned && !showed && t > 0.25 && t > turned_at + 0.2) {   // idle, untouched: nothing moves (once the header settles)
                still = false;
                if (!quiet) for (unsigned i = 0; i < drop::Frame::node_count; ++i)
                    if (std::memcmp(&frame.nodes[i], &last.nodes[i], sizeof frame.nodes[i])) std::fprintf(stderr, "idle %.3f: node %u\n", t, i);
            }
            showed = any;
            if (churn > worst) { worst = churn; worst_at = t; }
        }
        last = frame;
        if (snap < snaps.size() && t + 0.5 / fps >= snaps[snap]) { dump(frame, t); ++snap; }
        if (every && n % every == 0) dump(frame, t);
    }
    const Churn c = {total / (frames - 1), playing_frames ? total_playing / playing_frames : 0.0, worst, true, still, matched};
    if (!quiet)
        std::fprintf(stderr, "%s at %.0f fps: %d frames, churn mean %.2f, while drops show %.2f, peak %.0f at %.2f s%s%s\n", scenario, fps,
                     frames, c.mean, c.playing, worst, worst_at, still ? "" : ", moves when idle", matched ? "" : ", lobes differ");
    return c;
}
