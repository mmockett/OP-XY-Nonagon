// Coin's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with a note's whole animation at 60 fps, the rest at Euler's, then
// the settings that load it most: each frame valid, at most two of the four
// outlines reloaded (the dearest thing this screen does), on average at most
// 20 nodes changed a frame while it plays, and none at rest. It also walks each
// control in steps of 8 at rest and fails if one moves nothing on screen.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
int failures = 0;
coin::DisplayState state;
coin::Frame frame, last;

opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
void prepare(Input& input, const double* c) {
    std::memset(&input, 0, sizeof input);
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
}
struct Result { double mean; int peak, reloaded, rest; };
Result play(const double* c, int hold, const char* name) {
    Input input;
    prepare(input, c);
    std::memset(&frame, 0, sizeof frame);
    coin::display_reset(state);
    coin::display(state, input, frame);
    input.elapsed = 17;
    for (int t = 0; t < 6; ++t) coin::display(state, input, frame);
    input.note_serial++;
    input.held_notes = 1;
    Result r = {0, 0, 0, 0};
    double sum = 0;
    int frames = 0;
    for (int t = 0; t < 480; ++t) {
        if (t == hold) input.held_notes = 0;
        last = frame;
        coin::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; return r; }
        int changed = 0, outlines = 0;
        for (unsigned i = 0; i < coin::Frame::node_count; ++i) {
            bool c_ = !same(frame.nodes[i], last.nodes[i]);
            if (i < coin::Frame::path_count && frame.paths[i].key != last.paths[i].key) { c_ = true; ++outlines; }
            if (c_) ++changed;
        }
        if (state.disk[0].rolling || state.since < 512) {
            sum += changed;
            ++frames;
            if (changed > r.peak) r.peak = changed;
        }
        if (outlines > r.reloaded) r.reloaded = outlines;
    }
    // At rest: a spin's end, and the ripples' fade after it, can take a while.
    for (int t = 0; t < 7200 && state.disk[0].rolling; ++t) coin::display(state, input, frame);
    for (int t = 0; t < 720; ++t) coin::display(state, input, frame);
    last = frame;
    for (int t = 0; t < 60 && !state.disk[0].rolling; ++t) {
        coin::display(state, input, frame);
        for (unsigned i = 0; i < coin::Frame::node_count; ++i)
            if (!same(frame.nodes[i], last.nodes[i])) ++r.rest;
        last = frame;
    }
    r.mean = frames ? sum / frames : 0;
    if (r.mean > 20) { std::printf("FAIL %s: %.1f nodes a frame\n", name, r.mean); ++failures; }
    if (r.reloaded > 2) { std::printf("FAIL %s: %d outlines reloaded in a frame\n", name, r.reloaded); ++failures; }
    if (r.rest) { std::printf("FAIL %s: %d nodes changed at rest\n", name, r.rest); ++failures; }
    return r;
}
}

int main() {
    const double euler[8] = {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125};
    const char* names[8] = {"spin", "size", "disk", "base", "wobble", "air", "ring", "many"};
    double worst = 0;
    int peak = 0;
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 128; ++v) {
            double c[8];
            std::memcpy(c, euler, sizeof c);
            c[k] = v / 127.0;
            char name[48];
            std::snprintf(name, sizeof name, "%s at %d", names[k], v);
            const Result r = play(c, 60, name);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
        }
    const double heaviest[3][8] = {{1, 0.3, 0.125, 0.1667, 1, 1, 1, 1}, {1, 0.9, 0.125, 0.1667, 1, 1, 1, 1}, {1, 0.3, 0.875, 0.5, 1, 1, 1, 1}};
    for (int i = 0; i < 3; ++i) {
        const Result r = play(heaviest[i], 400, "heaviest");
        if (r.mean > worst) worst = r.mean;
        if (r.peak > peak) peak = r.peak;
    }
    // Each control moves the drawing.
    for (int k = 0; k < 8; ++k) {
        Input input;
        prepare(input, euler);
        std::memset(&frame, 0, sizeof frame);
        coin::display_reset(state);
        int moved = 0;
        for (int v = 0; v < 128; v += 8) {
            input.slots[k] = q31(v / 127.0);
            input.elapsed = 17;
            for (int t = 0; t < 20; ++t) coin::display(state, input, frame);
            if (v) {
                bool any = false;
                for (unsigned i = 12; i < coin::Frame::node_count; ++i) any = any || !same(frame.nodes[i], last.nodes[i]);
                for (unsigned p = 0; p < coin::Frame::path_count; ++p) any = any || frame.paths[p].key != last.paths[p].key;
                if (any) ++moved;
            }
            last = frame;
        }
        if (!moved) { std::printf("FAIL %s moves nothing on screen\n", names[k]); ++failures; }
        std::printf("churn: %s moves the drawing in %d of 15 steps\n", names[k], moved);
    }
    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average, peak %d, two outlines a frame, none at rest\n", worst, peak);
    return 0;
}
