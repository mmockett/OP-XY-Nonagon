// Mach's screen under the track's amp envelope (SDK 1.10, amp.h), run by
// sdk.py test:
// - with the envelope unknown, every frame is as it was before the screen
//   read the envelope (a checksum of a scene's frames, taken from the screen
//   before this change);
// - a short envelope ends the flight soon after the note-off, a long one
//   keeps it flying until its release ends (or the flight its own); a slow
//   attack's sound waves reach less far at first than an instant one's, and
//   a release draws them in;
// - churn under a held note and its release: on average at most 20 nodes
//   change a frame while the screen moves, with a low peak, and none at rest;
// - every outline the screen reshapes under an envelope draws cleanly.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
typedef mach::Frame F;
mach::DisplayState state;
F frame, last;
int failures = 0;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
// The Init sound.
const double init[8] = {0.6667, 0.301, 0.7, 0.5805, 0.4, 0.15, 1.0, 0.0};
// The heaviest: Mach 3, a long flight, a full crack, open air, the tightest
// waves and the plate.
const double heavy[8] = {1.0, 0.6, 0.9, 1.0, 1.0, 0.0, 1.0, 0.6};

struct AmpShape { double attack, decay, sustain, release; bool known; };
const AmpShape unknown = {0, 0, 0, 0, false};
// An instant attack and a release of about 40 ms (amp.h's curve) ...
const AmpShape short_one = {0, 0.5, 1, 0.16, true};
// ... against a 1.25 s attack and a 3.4 s release.
const AmpShape long_one = {0.5, 0.5, 0.75, 0.7, true};

void prepare(Input& input, const double* c, const AmpShape& e) {
    std::memset(&input, 0, sizeof input);
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    input.track = 1;
    input.elapsed = 17;  // 60 frames a second
    if (e.known) {
        input.amp.attack = q31(e.attack);
        input.amp.decay = q31(e.decay);
        input.amp.sustain = q31(e.sustain);
        input.amp.release = q31(e.release);
    } else {
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
    }
}
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
// Nodes changed since the last frame, an outline counted when its points change.
int changed() {
    int n = 0;
    for (unsigned i = 0; i < F::node_count; ++i) {
        bool c = !same(frame.nodes[i], last.nodes[i]);
        if (i < F::path_count && std::memcmp(&frame.paths[i], &last.paths[i], sizeof frame.paths[i])) c = true;
        n += c;
    }
    for (unsigned t = 0; t < F::text_count; ++t) n += std::memcmp(&frame.texts[t], &last.texts[t], sizeof frame.texts[t]) != 0;
    return n;
}
unsigned long long fnv(unsigned long long h, const void* data, unsigned bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (unsigned i = 0; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
// The sound waves shown, and the furthest one's radius in pixels.
int waves(int& widest) {
    int n = 0;
    widest = 0;
    for (unsigned i = 0; i < mach::node::rings; ++i) {
        const Node& r = frame.nodes[mach::node::ring + i];
        if (r.x >= hidden_x * unit) continue;
        ++n;
        const int px = r.scale * mach::plate::ring_radius * static_cast<int>(i + 1) / 1024;
        if (px > widest) widest = px;
    }
    return n;
}

// What the OP-XY draws as streaks or slivers (as tests/screen.cpp checks):
// a shown reshaping outline with no area or turning straight back,
// neighbouring points within an eighth of a pixel, a closing edge under a
// quarter of a pixel.
int outline_faults = 0;
void outlines(int t) {
    const unsigned n = F::point_count;
    for (unsigned p = 0; p < F::path_count; ++p) {
        if (frame.nodes[p].x >= hidden_x * unit) continue;
        const Point* q = frame.paths[p].points;
        const char* fault = 0;
        double area = 0;
        for (unsigned i = 0; i < n && !fault; ++i) {
            const Point &a = q[(i + n - 1) % n], &b = q[i], &c = q[(i + 1) % n];
            area += static_cast<double>(b.x) * c.y - static_cast<double>(c.x) * b.y;
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            const double span = std::fabs(vx) > std::fabs(vy) ? std::fabs(vx) : std::fabs(vy);
            const double dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
            if (i + 1 < n && std::fabs(vx) < 128 && std::fabs(vy) < 128) fault = "neighbouring points merge";
            else if (i + 1 == n && span < 256) fault = "its closing edge is under a quarter of a pixel";
            else if (dot < 0 && std::fabs(cross) < 0.035 * -dot) fault = "it turns straight back";
        }
        if (!fault && std::fabs(area) / 2 < 1024.0 * 1024.0 / 4) fault = "it has no area";
        if (fault) {
            if (outline_faults < 12) std::printf("FAIL outline %u at frame %d: %s\n", p, t, fault);
            ++outline_faults;
        }
    }
}

// A note held `hold` frames, released, then `after` frames; a second note
// part-way through the release when `again`. Counts what changes.
struct Run {
    unsigned long long hash;
    double mean;          // nodes changed a frame from the note to its last motion
    int peak, rest;       // the most in a frame; changes in the last second
    int last_change;      // the last frame after the note-off that changed anything
    int early;            // the furthest wave in the first quarter second, pixels
    int held;             // ... at the end of the hold
    int released;         // ... half a second after the note-off
    int flown;            // the last frame after the note-off with a flight shown
};
Run play(const double* c, const AmpShape& e, int hold, int after, bool again) {
    Input input;
    prepare(input, c, e);
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    mach::display_reset(state);
    Run r = {14695981039346656037ull, 0, 0, 0, -1, 0, 0, 0, -1};
    for (int t = 0; t < 6; ++t) mach::display(state, input, frame);
    static int count[4096];
    const int frames = hold + after;
    for (int t = 0; t < frames; ++t) {
        if (t == 0 || (again && t == hold + 30)) { ++input.note_serial; input.held_notes = 1; }
        if (t == hold || (again && t == hold + 60)) { ++input.note_off_serial; input.held_notes = 0; }
        last = frame;
        mach::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL frame %d: not valid\n", t); ++failures; }
        if (e.known) outlines(t);
        r.hash = fnv(r.hash, &frame, sizeof frame);
        const int n = count[t] = changed();
        int widest = 0;
        waves(widest);
        if (t < 15 && widest > r.early) r.early = widest;
        if (t == hold - 1) r.held = widest;
        if (t == hold + 30) r.released = widest;
        if (t >= hold && state.flying) r.flown = t - hold;
        if (t >= hold && n) r.last_change = t - hold;
        if (t >= frames - 60) r.rest += n;
        else if (n > r.peak) r.peak = n;
    }
    // The mean while the screen moves: from the note to its last change.
    const int end = r.last_change < 0 ? hold : hold + r.last_change + 1;
    double sum = 0;
    for (int t = 0; t < end; ++t) sum += count[t];
    r.mean = sum / end;
    return r;
}
}

int main() {
    // 1. Unknown envelope: the frames of a scene (the Init sound and the
    // heaviest settings, a held note, its release and a note during it) hash
    // as the screen drew them before it read the envelope.
    unsigned long long hash = 14695981039346656037ull;
    for (int s = 0; s < 2; ++s) {
        const unsigned long long h = play(s ? heavy : init, unknown, 180, 240, true).hash;
        hash = fnv(hash, &h, sizeof h);
    }
    // The screen at origin/main (59f7414) with the clean exit past the field's
    // right side applied to it: the exit changed these frames (they were
    // 46a1bddae409221f), and an envelope still changes none of them.
    const unsigned long long before = 0x5f94adfd3d5832f2ull;
    std::printf("envelope: unknown frames hash %016llx\n", hash);
    if (hash != before) { std::printf("FAIL unknown envelope: frames hash %016llx, before %016llx\n", hash, before); ++failures; }

    // 2. A short envelope against a long one: the Init sound, and a long
    // flight (dur 8 s) whose own sound outlasts both releases, so the long
    // one flies until its release ends. (Its waves spread slowly, on a scale
    // that fits the long flight, so they stay well within any envelope's
    // reach for the first seconds: only the Init sound's are compared.)
    double flyby[8];
    for (int k = 0; k < 8; ++k) flyby[k] = init[k];
    flyby[3] = 1.0;
    const double* scenes[2] = {init, flyby};
    const char* names[2] = {"init", "long flight"};
    for (int s = 0; s < 2; ++s) {
        const Run a = play(scenes[s], short_one, 60, 360, false), b = play(scenes[s], long_one, 60, 360, false);
        std::printf("envelope: %s: after the note-off, short flies %.2f s and moves %.2f s, long flies %.2f s and moves %.2f s\n",
                    names[s], (a.flown + 1) / 60.0, (a.last_change + 1) / 60.0, (b.flown + 1) / 60.0, (b.last_change + 1) / 60.0);
        std::printf("envelope: %s: furthest wave, first 0.25 s %d against %d px; held %d against %d px; 0.5 s after the note-off %d against %d px\n",
                    names[s], a.early, b.early, a.held, b.held, a.released, b.released);
        if (a.flown > 6) { std::printf("FAIL %s: a short release still flies %d frames after the note-off\n", names[s], a.flown); ++failures; }
        if (a.last_change > 30) { std::printf("FAIL %s: a short release still moves %d frames after the note-off\n", names[s], a.last_change); ++failures; }
        if (b.flown < 60) { std::printf("FAIL %s: a long release stops flying %d frames after the note-off\n", names[s], b.flown); ++failures; }
        if (s == 1 && (b.flown < 200 || b.flown > 215)) { std::printf("FAIL %s: flies %d frames after the note-off, not until the release ends (3.4 s)\n", names[s], b.flown); ++failures; }
        if (s == 1) continue;
        if (b.early >= a.early) { std::printf("FAIL %s: a slow attack's waves reach as far early on (%d px) as an instant one's (%d)\n", names[s], b.early, a.early); ++failures; }
        if (b.released >= b.held) { std::printf("FAIL %s: the release doesn't draw the waves in (%d px held, %d after)\n", names[s], b.held, b.released); ++failures; }
    }

    // 3. Churn under a held note and its release, at the defaults and the
    // heaviest settings, each envelope.
    const double* churn[2] = {init, heavy};
    const AmpShape* envelopes[2] = {&short_one, &long_one};
    double worst = 0;
    int peak = 0;
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e) {
            const Run r = play(churn[s], *envelopes[e], 180, 360, true);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
            std::printf("envelope: churn %s %s: %.1f a frame, peak %d, %d at rest, moving %.2f s after the note-off\n", s ? "heavy" : "init", e ? "long" : "short", r.mean, r.peak, r.rest, (r.last_change + 1) / 60.0);
            if (r.mean > 20) { std::printf("FAIL churn: %.1f nodes a frame\n", r.mean); ++failures; }
            if (r.rest) { std::printf("FAIL churn: %d nodes changed at rest\n", r.rest); ++failures; }
        }
    if (peak > 32) { std::printf("FAIL churn: a peak of %d nodes in a frame\n", peak); ++failures; }
    if (outline_faults) { std::printf("FAIL outlines: %d faults under an envelope\n", outline_faults); ++failures; }
    if (failures) { std::printf("envelope: %d failures\n", failures); return 1; }
    std::printf("envelope: unknown unchanged; churn at most %.1f nodes a frame on average, peak %d, none at rest; outlines clean\n", worst, peak);
    return 0;
}
