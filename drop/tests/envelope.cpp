// Drop's screen under the track's amp envelope (SDK 1.10, amp.h), run by
// sdk.py test:
// - with the envelope unknown, every frame is as it was before the screen
//   read the envelope (a checksum of a scene's frames, taken from the screen
//   before this change);
// - a short envelope boils the drop away soon after the note-off, a long one
//   keeps it on the iron until its release ends; a slow attack swells the
//   star's lobes, so it reshapes less early on than an instant one;
// - churn under a held note (and a chord) and its release: on average at
//   most 20 nodes change a frame while the screen moves, with a low peak,
//   and none at rest;
// - every outline the screen draws under the long envelope, its slow attack
//   and its long release included, is one the OP-XY draws cleanly.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
typedef drop::Frame F;
drop::DisplayState state;
F frame, last;
int failures = 0;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
// heat, size, liquid, beat, groove, vapour, life, crackle: definition.json's defaults
const double defaults[8] = {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25};
// The busiest screen: the hottest plate, the biggest drop, the deepest,
// slowest stars, running round the deepest teeth, the thickest vapour, a
// billowing plume and full crackle.
const double heaviest[8] = {1, 1, 0, 0, 1, 1, 0.3, 1};

struct Amp { double attack, decay, sustain, release; bool known; };
const Amp unknown = {0, 0, 0, 0, false};
// Stock's times (amp_curve.h), where a larger release is SHORTER: an
// instant attack and release 0.996 (word 0x7f..., stock's shortest, about
// 6 ms) ...
const Amp short_one = {0, 0.5, 1, 0.996, true};
// ... against attack 0.5 (about 1.3 s) and release 0.5 (about 3 s to -66 dB).
const Amp long_one = {0.5, 0.5, 0.75, 0.5, true};

void prepare(Input& input, const double* c, const Amp& e) {
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

// The outline checks of tests/frame_rules.cpp, on the drops' reshaping
// outlines as shown: area, no neighbouring points within an eighth of a
// pixel, no turn straight back, a closing edge of a quarter pixel or more.
int outline_failures = 0;
void check_outlines(const char* when) {
    for (unsigned p = 0; p < F::path_count; ++p) {
        const Node& node = frame.nodes[p];
        if (node.x >= hidden_x * unit) continue;
        const double k = node.scale / 1024.0 / 1024.0;
        double x[F::point_count], y[F::point_count];
        for (unsigned i = 0; i < F::point_count; ++i) { x[i] = frame.paths[p].points[i].x * k; y[i] = frame.paths[p].points[i].y * k; }
        double area = 0, perimeter = 0;
        bool bad = false;
        for (unsigned i = 0; i < F::point_count; ++i) {
            const unsigned j = (i + 1) % F::point_count, h = (i + F::point_count - 1) % F::point_count;
            const double dx = x[j] - x[i], dy = y[j] - y[i], edge = std::hypot(dx, dy);
            area += x[i] * y[j] - x[j] * y[i];
            perimeter += edge;
            if (edge < (j ? 0.125 : 0.25)) bad = true;
            // A turn straight back at point i: its edges antiparallel.
            const double ex = x[i] - x[h], ey = y[i] - y[h];
            if (ex * dx + ey * dy < 0 && std::fabs(ex * dy - ey * dx) < 0.02 * std::hypot(ex, ey) * edge) bad = true;
        }
        // Its mean width, twice its area over its perimeter, at least an eighth of a pixel.
        if (perimeter > 0 && std::fabs(area) / perimeter < 0.125) bad = true;
        if (bad) {
            if (outline_failures < 5) std::printf("FAIL outline %u drawn as a streak (%s)\n", p, when);
            ++outline_failures;
        }
    }
}

// Notes (one, or a chord) held `hold` frames, released, then `after` frames;
// another part-way through the release when `again`. Counts what changes.
struct Run {
    unsigned long long hash;
    double mean;          // nodes changed a frame from the note to its last change
    int peak, rest;       // the most in a frame; changes in the last second
    int last_change;      // the last frame after the note-off that changed anything
    int shown;            // frames after the note-off a drop still shows
    int early;            // outlines reshaped in the first quarter second
};
Run play(const double* c, const Amp& e, int hold, int after, bool again, int notes, bool outlines) {
    Input input;
    prepare(input, c, e);
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    drop::display_reset(state);
    Run r = {14695981039346656037ull, 0, 0, 0, -1, 0, 0};
    for (int t = 0; t < 6; ++t) drop::display(state, input, frame);
    static int count[4096];
    const int frames = hold + after;
    for (int t = 0; t < frames; ++t) {
        if (t == 0 || (again && t == hold + 30)) {
            input.note_serial += static_cast<opxy::u32>(notes);
            input.held_notes = static_cast<opxy::u32>(notes);
        }
        if (t == hold || (again && t == hold + 60)) { input.note_off_serial += input.held_notes; input.held_notes = 0; }
        last = frame;
        drop::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL frame %d: not valid\n", t); ++failures; }
        if (outlines) {
            char when[64];
            std::snprintf(when, sizeof when, "frame %d", t);
            check_outlines(when);
        }
        r.hash = fnv(r.hash, &frame, sizeof frame);
        const int n = count[t] = changed();
        if (t < 15)
            for (unsigned p = 0; p < F::path_count; ++p) r.early += frame.paths[p].key != last.paths[p].key;
        if (t >= hold && n) r.last_change = t - hold;
        if (t >= hold)
            for (unsigned p = 0; p < F::path_count; ++p)
                if (frame.nodes[p].x < hidden_x * unit) { r.shown = t - hold + 1; break; }
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
    // 1. Unknown envelope: the frames of a scene (the defaults and the
    // heaviest settings, a held note, its release and a note during it) hash
    // as the screen drew them before it read the envelope.
    unsigned long long hash = 14695981039346656037ull;
    for (int s = 0; s < 2; ++s) {
        const unsigned long long h = play(s ? heaviest : defaults, unknown, 180, 240, true, 1, false).hash;
        hash = fnv(hash, &h, sizeof h);
    }
    const unsigned long long before = 0x0ebb8b526e4045d8ull;  // the screen at origin/main, 59f7414
    std::printf("envelope: unknown frames hash %016llx\n", hash);
    if (hash != before) { std::printf("FAIL unknown envelope: frames hash %016llx, before %016llx\n", hash, before); ++failures; }

    // 2. A short envelope against a long one: a second's note at the
    // defaults, and on Kettle's long-lived drop.
    const double kettle[8] = {0.5, 1, 0, 0.1, 0, 0, 0.9, 0.15};
    const double* scenes[2] = {defaults, kettle};
    const char* names[2] = {"defaults", "kettle"};
    for (int s = 0; s < 2; ++s) {
        const Run a = play(scenes[s], short_one, 60, 360, false, 1, false), b = play(scenes[s], long_one, 60, 360, false, 1, false);
        std::printf("envelope: %s: after the note-off, short shows the drop %.2f s and moves %.2f s, long %.2f s and %.2f s; "
                    "outlines reshaped in the first 0.25 s %d against %d\n",
                    names[s], a.shown / 60.0, (a.last_change + 1) / 60.0, b.shown / 60.0, (b.last_change + 1) / 60.0, a.early, b.early);
        if (a.last_change > 30) { std::printf("FAIL %s: a short release still moves %d frames after the note-off\n", names[s], a.last_change); ++failures; }
        if (b.shown < 45 || b.shown <= a.shown) { std::printf("FAIL %s: a long release boils the drop away %d frames after the note-off\n", names[s], b.shown); ++failures; }
        if (b.last_change > 240) { std::printf("FAIL %s: the screen moves %d frames after the note-off, past the release\n", names[s], b.last_change); ++failures; }
        if (b.early >= a.early) { std::printf("FAIL %s: a slow attack reshapes the star as much early on (%d) as an instant one (%d)\n", names[s], b.early, a.early); ++failures; }
    }

    // Stock's directions (amp_curve.h): a larger release word ends the motion
    // sooner (0.25, 0.5 and 0.996: about 7.9 s, 3 s and 6 ms to -66 dB), and a
    // larger attack word moves less early on (0, 0.5 and 0.625: one block,
    // about 1.3 s and 5.4 s).
    {
        const double releases[3] = {0.25, 0.5, 0.996}, attacks[3] = {0, 0.5, 0.625};
        int lasted[3];
        double swell[3];
        for (int k = 0; k < 3; ++k) {
            Amp e = short_one;
            e.release = releases[k];
            lasted[k] = play(scenes[0], e, 60, 600, false, 1, false).last_change;
            e = short_one;
            e.attack = attacks[k];
            swell[k] = static_cast<double>(play(scenes[0], e, 60, 60, false, 1, false).early);
        }
        std::printf("envelope: release 0.25, 0.5, 0.996 move on %.2f, %.2f and %.2f s; attack 0, 0.5, 0.625 move %g, %g and %g early on\n",
                    (lasted[0] + 1) / 60.0, (lasted[1] + 1) / 60.0, (lasted[2] + 1) / 60.0, swell[0], swell[1], swell[2]);
        for (int k = 0; k < 2; ++k) {
            if (!(lasted[k] > lasted[k + 1])) { std::printf("FAIL release %.3f doesn't end the motion sooner than %.3f\n", releases[k + 1], releases[k]); ++failures; }
            if (!(swell[k] > swell[k + 1])) { std::printf("FAIL attack %.3f doesn't swell slower than %.3f\n", attacks[k + 1], attacks[k]); ++failures; }
        }
    }

    // 3. Churn under a held note and a chord and their release, at the
    // defaults and the heaviest settings, each envelope; their outlines
    // checked as the OP-XY draws them.
    const double* churn[2] = {defaults, heaviest};
    const Amp* envelopes[2] = {&short_one, &long_one};
    double worst = 0;
    int peak = 0;
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e)
            for (int notes = 1; notes <= 3; notes += 2) {
                const Run r = play(churn[s], *envelopes[e], 180, 360, true, notes, true);
                if (r.mean > worst) worst = r.mean;
                if (r.peak > peak) peak = r.peak;
                std::printf("envelope: churn %s %s %s: %.1f a frame, peak %d, %d at rest, moving %.2f s after the note-off\n",
                            s ? "heaviest" : "defaults", e ? "long" : "short", notes > 1 ? "chord" : "note", r.mean, r.peak, r.rest,
                            (r.last_change + 1) / 60.0);
                if (r.mean > 20) { std::printf("FAIL churn: %.1f nodes a frame\n", r.mean); ++failures; }
                if (r.rest) { std::printf("FAIL churn: %d nodes changed at rest\n", r.rest); ++failures; }
            }
    if (peak > 32) { std::printf("FAIL churn: a peak of %d nodes in a frame\n", peak); ++failures; }
    if (outline_failures) { std::printf("FAIL %d outlines drawn as streaks\n", outline_failures); ++failures; }
    if (failures) { std::printf("envelope: %d failures\n", failures); return 1; }
    std::printf("envelope: unknown unchanged; churn at most %.1f nodes a frame on average, peak %d, none at rest; outlines clean\n", worst, peak);
    return 0;
}
