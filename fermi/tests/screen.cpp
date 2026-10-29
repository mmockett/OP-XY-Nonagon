// Fermi's screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
//   - Every frame is valid, from reset through notes, control changes and
//     long runs at every masses and mode setting.
//   - The idle screen is still.
//   - Churn: nodes changed a frame stay at a mean of 20 or fewer, with a low peak.
//   - The screen's chain is the voice's chain: with the controls still, the
//     two hold the same state after the same time.
// Given a file name, it also writes a scene's frames as JSON lines for
// tools/make_gif.py.
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }

struct Screen {
    fermi::DisplayState state;
    fermi::Frame frame, before;
    opxy::draw::Input input;
    double controls[8] = {0.5, 2.5 / 7, 0, 0.4, 1, 0.125, 0, 0.5 / 64};
    unsigned frames = 0;
    double churn_sum = 0;
    unsigned churn_peak = 0;
    std::FILE* out = nullptr;
    Screen() {
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        std::memset(&input, 0, sizeof input);
        input.track = 1;
        fermi::display_reset(state);
    }
    unsigned changed() const {
        unsigned n = 0;
        for (unsigned i = 0; i < fermi::Frame::node_count; ++i) {
            const opxy::draw::Node& a = frame.nodes[i];
            const opxy::draw::Node& b = before.nodes[i];
            if (a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint)
                ++n;
            else if (i < fermi::Frame::path_count && frame.paths[i].key != before.paths[i].key)
                ++n;
        }
        return n;
    }
    // One frame of dt seconds.
    unsigned tick(double dt, bool count = true) {
        for (int k = 0; k < 8; ++k) input.slots[k] = q31(controls[k]);
        for (int k = 0; k < 4; ++k) input.parameters[k] = input.slots[k];
        input.elapsed = static_cast<opxy::i32>(dt * 1024 + 0.5);
        before = frame;
        fermi::display(state, input, frame);
        if (!opxy::draw::valid(frame)) {
            explain();
            check(false, "an invalid frame");
        }
        const unsigned c = changed();
        if (count) {
            ++frames;
            churn_sum += c;
            churn_peak = c > churn_peak ? c : churn_peak;
        }
        if (out) dump();
        return c;
    }
    void note() { ++input.note_serial; input.held_notes = 1; }
    void release() { input.held_notes = 0; ++input.note_off_serial; }
    void explain() {
        using namespace opxy::draw;
        for (unsigned p = 0; p < fermi::Frame::path_count; ++p)
            for (unsigned i = 0; i < fermi::Frame::point_count; ++i) {
                const Point& q = frame.paths[p].points[i];
                if (q.x < -path_extent * unit || q.x > path_extent * unit || q.y < -path_extent * unit || q.y > path_extent * unit)
                    std::printf("  path %u point %u at %d, %d\n", p, i, q.x, q.y);
            }
        for (unsigned n = 0; n < fermi::Frame::node_count; ++n) {
            const Node& d = frame.nodes[n];
            if (!placement(d.x, d.y, d.scale)) std::printf("  node %u placed at %d, %d scale %d\n", n, d.x, d.y, d.scale);
            if (!rotation_valid(d.cos, d.sin)) std::printf("  node %u turned %d, %d\n", n, d.cos, d.sin);
            if (d.shade >= fermi::Frame::palette_count || d.paint > fill_and_stroke) std::printf("  node %u paint %u %u\n", n, d.shade, d.paint);
        }
    }
    void dump() {
        std::fprintf(out, "{\"paths\":[");
        for (unsigned p = 0; p < fermi::Frame::path_count; ++p) {
            std::fprintf(out, "%s[", p ? "," : "");
            for (unsigned i = 0; i < fermi::Frame::point_count; ++i)
                std::fprintf(out, "%s%d,%d", i ? "," : "", frame.paths[p].points[i].x, frame.paths[p].points[i].y);
            std::fprintf(out, "]");
        }
        std::fprintf(out, "],\"nodes\":[");
        for (unsigned n = 0; n < fermi::Frame::node_count; ++n) {
            const opxy::draw::Node& d = frame.nodes[n];
            std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", n ? "," : "", d.x, d.y, d.scale, d.cos, d.sin, d.shade, d.paint);
        }
        std::fprintf(out, "],\"texts\":[");
        for (unsigned t = 0; t < fermi::Frame::text_count; ++t) {
            const opxy::draw::Text& x = frame.texts[t];
            std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", t ? "," : "", x.x, x.y, x.scale, x.chars);
        }
        std::fprintf(out, "]}\n");
    }
};

// The scene the GIF shows: energy drawing the chain back, a breathing note,
// the crossing into chaos, then eight masses on cubic coils, damped, with a seed.
void scene(Screen& s, double fps) {
    const double dt = 1 / fps;
    s.controls[0] = 0.25; s.controls[3] = 0.16; s.controls[4] = 0.2;
    for (int f = 0; f < fps * 0.6; ++f) s.tick(dt);
    for (int f = 0; f < fps * 0.8; ++f) { s.controls[0] = 0.25 + 0.25 * f / (fps * 0.8); s.tick(dt); }   // drawn further
    s.note();
    for (int f = 0; f < fps * 8; ++f) s.tick(dt);                       // breathing: two returns
    for (int f = 0; f < fps * 3; ++f) { s.controls[0] = 0.5 + 0.5 * f / (fps * 3); s.tick(dt); }   // into chaos
    for (int f = 0; f < fps * 3; ++f) s.tick(dt);                       // the shiver
    s.release();
    for (int f = 0; f < fps * 2.4; ++f) s.tick(dt);
    s.controls[1] = 0.5 / 7;                                             // eight weights
    for (int f = 0; f < fps * 0.8; ++f) s.tick(dt);
    s.controls[2] = 1; s.controls[0] = 0.55;                             // cubic coils
    for (int f = 0; f < fps * 0.8; ++f) s.tick(dt);
    s.controls[4] = 1; s.controls[6] = 0.25; s.controls[7] = 9.5 / 64;  // dispersion, the damper, a seed
    for (int f = 0; f < fps * 0.8; ++f) s.tick(dt);
    s.note();
    for (int f = 0; f < fps * 6; ++f) s.tick(dt);
    s.release();
    for (int f = 0; f < fps * 2.5; ++f) s.tick(dt);
}

void valid_everywhere() {
    Screen s;
    for (int masses = 0; masses < 7; ++masses)
        for (int mode = 0; mode < 4; ++mode)
            for (int e = 0; e < 3; ++e) {
                s.controls[1] = (masses + 0.5) / 7;
                s.controls[5] = (mode + 0.5) / 4;
                s.controls[0] = e * 0.5;
                s.controls[2] = e == 2 ? 0.6 : 0;
                s.controls[3] = 0;
                s.controls[7] = (e * 20 + 0.5) / 64;
                s.note();
                for (int f = 0; f < 90; ++f) s.tick(1 / 30.0, false);
                s.release();
                for (int f = 0; f < 10; ++f) s.tick(1 / 30.0, false);
            }
    // Every control at its corners, and a long run that doubles the sheet many times.
    for (int k = 0; k < 8; ++k) {
        s.controls[k] = 1;
        s.tick(1 / 30.0, false);
        s.controls[k] = 0;
        s.tick(1 / 30.0, false);
    }
    s.controls[0] = 0.5; s.controls[1] = 0.5 / 7; s.controls[3] = 0;
    s.note();
    for (int f = 0; f < 30 * 120; ++f) s.tick(1 / 30.0, false);
    check(s.state.chain.ticks > 8u * 100000, "a long run does not run the chain on");
    std::printf("screen: valid through %s (a 2-minute note runs its chain %u units)\n",
                failures ? "SOME" : "every masses, mode and energy setting, the control corners and long runs", s.state.chain.ticks / 8);
}

void still_when_idle() {
    Screen s;
    s.tick(1 / 60.0, false);
    unsigned moving = 0;
    for (int f = 0; f < 120; ++f) moving += s.tick(1 / 60.0, false);
    std::printf("screen: idle, %u nodes change over 2 s\n", moving);
    check(moving == 0, "the idle screen moves");
}

void churn() {
    const double rates[2] = {60, 30};
    for (double fps : rates) {
        Screen s;
        s.tick(1 / fps, false);
        scene(s, fps);
        const double mean = s.churn_sum / s.frames;
        std::printf("screen: churn at %.0f fps over the GIF's scene, mean %.1f and peak %u nodes a frame\n", fps, mean, s.churn_peak);
        check(mean <= 20, "churn above a mean of 20 nodes a frame");
        check(s.churn_peak <= 30, "churn peaks above 30 nodes a frame");
    }
}

void same_chain() {
    // The voice and the screen, held still, at the same moment.
    Screen s;
    s.controls[0] = 0.9;   // near chaos, where any difference would grow
    s.controls[3] = 0.2;
    s.tick(1 / 60.0, false);
    s.note();
    fermi::Voice voice;
    fermi::Shared shared;
    opxy::i32 parameters[8], out[64];
    opxy::u32 pitch[64];
    for (int k = 0; k < 8; ++k) parameters[k] = q31(s.controls[k]);
    for (int i = 0; i < 64; ++i) pitch[i] = 21424179u;
    fermi::initialize(voice, shared, 1);
    fermi::start(voice, shared, 1);
    // A frame of exactly 64/44100 x 16 s, so the screen owes whole blocks.
    unsigned blocks = 0;
    s.input.elapsed = 0;
    fermi::display(s.state, s.input, s.frame);   // the screen sees the note and loads its chain
    for (int f = 0; f < 600; ++f) {
        s.input.elapsed = 0;
        for (int k = 0; k < 8; ++k) s.input.slots[k] = parameters[k];
        // 16 blocks: elapsed Q10 x 44100 = 16 x 65536 exactly needs elapsed = 23.78; feed the owed directly.
        s.state.owed += 16u * 65536u;
        fermi::display(s.state, s.input, s.frame);
        for (int b = 0; b < 16; ++b) fermi::render(voice, shared, 1, out, parameters, pitch);
        blocks += 16;
    }
    // render loads the chain on its first block, before its first steps, as load() does.
    const bool same = !std::memcmp(voice.chain.x, s.state.chain.x, sizeof voice.chain.x) &&
                      !std::memcmp(voice.chain.v, s.state.chain.v, sizeof voice.chain.v) && voice.chain.ticks == s.state.chain.ticks;
    std::printf("screen: after %u blocks at energy 0.9 the screen's chain and the voice's %s (%u ticks)\n", blocks,
                same ? "are identical" : "DIFFER", s.state.chain.ticks);
    check(same, "the screen's chain is not the voice's");
}

// The contact sheet: each control at 0, 0.5 and 1 with the others at their
// defaults, idle, once the screen has eased (24 frames, a control a row).
void contact(const char* path) {
    const double defaults[8] = {0.5, 2.5 / 7, 0, 0.22, 0.2, 0.125, 0, 0.5 / 64};
    std::FILE* f = std::fopen(path, "w");
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 3; ++v) {
            Screen s;
            for (int j = 0; j < 8; ++j) s.controls[j] = defaults[j];
            s.controls[k] = v * 0.5;
            if (k == 1) s.controls[k] = v == 0 ? 0.5 / 7 : v == 1 ? 3.5 / 7 : 6.5 / 7;   // 8, 20, 32 masses
            if (k == 5) s.controls[k] = v == 0 ? 0.5 / 4 : v == 1 ? 1.5 / 4 : 3.5 / 4;   // modes 1, 2, 4
            if (k == 7) s.controls[k] = v == 0 ? 0.5 / 64 : v == 1 ? 32.5 / 64 : 63.5 / 64;   // seed off, 32, 63
            for (int t = 0; t < 30; ++t) s.tick(1 / 30.0, false);
            s.out = f;
            s.tick(1 / 30.0, false);
            s.out = nullptr;
        }
    std::fclose(f);
}
}

// Each control's caption at 0, 25, 50, 75 and 100, the others at defaults.
void captions() {
    const double defaults[8] = {0.5, 2.5 / 7, 0, 0.22, 0.2, 0.125, 0, 0.5 / 64};
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 5; ++v) {
            opxy::draw::Input in;
            std::memset(&in, 0, sizeof in);
            for (int j = 0; j < 8; ++j) in.slots[j] = q31(j == k ? v * 0.25 : defaults[j]);
            fermi::chain::Setup s;
            fermi::chain::configure(s, in.slots);
            char values[8][opxy::draw::header_capacity];
            fermi::screen::captions(values, s, in);
            std::printf("%d %d %s\n", k, v, values[k]);
        }
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "--captions")) {
        captions();
        return 0;
    }
    if (argc > 2 && !std::strcmp(argv[1], "--contact")) {
        contact(argv[2]);
        return 0;
    }
    if (argc > 1) {
        // Frames for the GIF: the scene at the given rate.
        Screen s;
        s.out = std::fopen(argv[1], "w");
        const double fps = argc > 2 ? std::atof(argv[2]) : 15;
        s.tick(1 / fps, false);
        scene(s, fps);
        std::fclose(s.out);
        std::printf("wrote %u frames\n", s.frames);
        return 0;
    }
    valid_everywhere();
    still_when_idle();
    churn();
    same_chain();
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
