// Sand's screen under the track's amp envelope (SDK 1.10, amp.h), run by
// sdk.py test:
// - with the envelope unknown, every frame is as it was before the screen
//   read the envelope (a checksum of a scene's frames, taken from the screen
//   before this change);
// - a short envelope ends the motion soon after the note-off, a long one
//   keeps the plate bowed, and its sand thrown, until its release ends; a
//   slow attack throws less sand early than an instant one;
// - churn under a held note and its release: on average at most 20 nodes
//   change a frame while the note sounds, with a low peak, and none at rest.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
typedef sand::Frame F;
sand::DisplayState state;
F frame, last;
int failures = 0;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
const double chladni[8] = {0.2083, 0.6, 0.6, 0.3, 0.1667, 0.5, 0, 0};

struct Amp { double attack, decay, sustain, release; bool known; };
const Amp unknown = {0, 0, 0, 0, false};
// An instant attack and a release of about 40 ms (amp.h's curve) ...
const Amp short_one = {0, 0.5, 1, 0.16, true};
// ... against a 1.25 s attack and a 3.4 s release.
const Amp long_one = {0.5, 0.5, 0.75, 0.7, true};

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

// A note held `hold` frames, released, then `after` frames; a second note
// part-way through the release when `again`. Counts what changes.
struct Run {
    unsigned long long hash;
    double mean;          // nodes changed a frame from the note to its last motion
    int peak, rest;       // the most in a frame; changes in the last second
    int last_change;      // the last frame after the note-off that changed anything
    int early;            // nodes changed in the first quarter second
};
Run play(const double* c, const Amp& e, int hold, int after, bool again) {
    Input input;
    prepare(input, c, e);
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    sand::display_reset(state);
    Run r = {14695981039346656037ull, 0, 0, 0, -1, 0};
    for (int t = 0; t < 6; ++t) sand::display(state, input, frame);
    static int count[4096];
    const int frames = hold + after;
    for (int t = 0; t < frames; ++t) {
        if (t == 0 || (again && t == hold + 30)) { ++input.note_serial; input.held_notes = 1; }
        if (t == hold || (again && t == hold + 60)) { ++input.note_off_serial; input.held_notes = 0; }
        last = frame;
        sand::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL frame %d: not valid\n", t); ++failures; }
        r.hash = fnv(r.hash, &frame, sizeof frame);
        const int n = count[t] = changed();
        if (t < 15) r.early += n;
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
    // 1. Unknown envelope: the frames of a scene (Chladni and the heaviest
    // settings, a held note, its release and a note during it) hash as the
    // screen drew them before it read the envelope.
    const double heavy[8] = {0.9, 0.1, 1, 1, 0.5, 0, 1, 1};
    unsigned long long hash = 14695981039346656037ull;
    for (int s = 0; s < 2; ++s) {
        const unsigned long long h = play(s ? heavy : chladni, unknown, 180, 240, true).hash;
        hash = fnv(hash, &h, sizeof h);
    }
    const unsigned long long before = 0x49d6f9a166aba698ull;  // the screen at origin/main, 59f7414
    if (hash != before) { std::printf("FAIL unknown envelope: frames hash %016llx, before %016llx\n", hash, before); ++failures; }

    // 2. A short envelope against a long one, Chladni and a struck plate.
    const double struck[8] = {0.2083, 0.1, 0.6, 0.3, 0.1667, 0.5, 0, 0};
    const double* scenes[2] = {chladni, struck};
    const char* names[2] = {"chladni", "struck"};
    for (int s = 0; s < 2; ++s) {
        const Run a = play(scenes[s], short_one, 60, 360, false), b = play(scenes[s], long_one, 60, 360, false);
        std::printf("envelope: %s: after the note-off, short moves for %.2f s, long for %.2f s; first 0.25 s %d against %d nodes\n",
                    names[s], (a.last_change + 1) / 60.0, (b.last_change + 1) / 60.0, a.early, b.early);
        if (a.last_change > 60) { std::printf("FAIL %s: a short release still moves %d frames after the note-off\n", names[s], a.last_change); ++failures; }
        if (b.last_change < 150) { std::printf("FAIL %s: a long release stops %d frames after the note-off\n", names[s], b.last_change); ++failures; }
        if (b.early >= a.early) { std::printf("FAIL %s: a slow attack moves as much early on (%d) as an instant one (%d)\n", names[s], b.early, a.early); ++failures; }
    }

    // 3. Churn under a held note and its release, at the defaults and the
    // heaviest settings, each envelope. (A slippery plate at full tilt, Settle
    // 0 and Tilt 100, runs its sand downhill for ever, with or without notes:
    // that one is left out of the rest check.)
    const double heaviest[8] = {0.9, 0.1, 1, 1, 0.5, 0.5, 1, 1};
    const double* churn[2] = {chladni, heaviest};
    const Amp* envelopes[2] = {&short_one, &long_one};
    double worst = 0;
    int peak = 0;
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e) {
            const Run r = play(churn[s], *envelopes[e], 180, 360, true);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
            std::printf("envelope: churn %s %s: %.1f a frame, peak %d, %d at rest, moving %.2f s after the note-off\n", s ? "heavy" : "chladni", e ? "long" : "short", r.mean, r.peak, r.rest, (r.last_change + 1) / 60.0);
            if (r.mean > 20) { std::printf("FAIL churn: %.1f nodes a frame\n", r.mean); ++failures; }
            if (r.rest) { std::printf("FAIL churn: %d nodes changed at rest\n", r.rest); ++failures; }
        }
    if (peak > 60) { std::printf("FAIL churn: a peak of %d nodes in a frame\n", peak); ++failures; }
    if (failures) { std::printf("envelope: %d failures\n", failures); return 1; }
    std::printf("envelope: unknown unchanged; churn at most %.1f nodes a frame on average, peak %d, none at rest\n", worst, peak);
    return 0;
}
