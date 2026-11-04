// Fermi's screen under the track's amp envelope (SDK 1.10, amp.h), run by
// sdk.py test:
// - with the envelope unknown, every frame is as it was before the screen
//   read the envelope (a checksum of a scene's frames, taken from the screen
//   before this change);
// - a short envelope stops the strobe soon after the note-off, a long one
//   keeps it running, the chain swinging, until its release ends; a slow
//   attack draws the chain out less at first, and a long release still
//   shows it swinging a second and a half after the note-off;
// - churn under a held note and its release: on average at most 20 nodes
//   change a frame while the screen moves, with a low peak, and none at rest.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
typedef fermi::Frame F;
fermi::DisplayState state;
F frame, last;
int failures = 0;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
// The init sound (definition.json's defaults).
const double defaults[8] = {0.5, 0.3571, 0, 0.39, 0.15, 0.125, 0, 0};
// The heaviest: 32 heavy bobs on cubic coils in chaos, the fourth mode, a
// fast regulator and the largest seed.
const double heavy[8] = {1, 1, 1, 0, 1, 1, 0, 1};

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
// The bobs' widest swing off the row, px.
double widest() {
    double most = 0;
    for (unsigned j = 0; j < fermi::chain::most; ++j) {
        const Node& d = frame.nodes[fermi::node::mass + j];
        if (d.x == hidden_x * unit) continue;
        const double off = (d.y - fermi::plate::row_y * unit) / 1024.0;
        most = off > most ? off : -off > most ? -off : most;
    }
    return most;
}

// A note held `hold` frames, released, then `after` frames; a second note
// part-way through the release when `again`. Counts what changes.
struct Run {
    unsigned long long hash;
    double mean;          // nodes changed a frame from the note to its last motion
    int peak, rest;       // the most in a frame; changes in the last second
    int last_change;      // the last frame after the note-off that changed anything
    int stopped;          // the frame after the note-off the strobe stopped (-1: never)
    double early;         // the widest swing a quarter second into the note, px
    double late;          // the widest swing 0.75 s after the note-off, px
};
Run play(const double* c, const Amp& e, int hold, int after, bool again) {
    Input input;
    prepare(input, c, e);
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    fermi::display_reset(state);
    Run r = {14695981039346656037ull, 0, 0, 0, -1, -1, 0, 0};
    for (int t = 0; t < 60; ++t) fermi::display(state, input, frame);   // the chain drawn back, settled
    static int count[4096];
    const int frames = hold + after;
    for (int t = 0; t < frames; ++t) {
        if (t == 0 || (again && t == hold + 30)) { ++input.note_serial; input.held_notes = 1; }
        if (t == hold || (again && t == hold + 60)) { ++input.note_off_serial; input.held_notes = 0; }
        last = frame;
        fermi::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL frame %d: not valid\n", t); ++failures; }
        r.hash = fnv(r.hash, &frame, sizeof frame);
        const int n = count[t] = changed();
        if (t == 15) r.early = widest();
        if (t == hold + 45) r.late = widest();
        if (t >= hold && !state.running && r.stopped < 0) r.stopped = t - hold;
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
    // 1. Unknown envelope: the frames of a scene (the defaults and the
    // heaviest settings, a held note, its release and a note during it) hash
    // as the screen drew them before it read the envelope.
    unsigned long long hash = 14695981039346656037ull;
    for (int s = 0; s < 2; ++s) {
        const unsigned long long h = play(s ? heavy : defaults, unknown, 180, 240, true).hash;
        hash = fnv(hash, &h, sizeof h);
    }
    std::printf("envelope: unknown, frames hash %016llx\n", hash);
    const unsigned long long before = 0xe68259ccfd266030ull;  // the screen at origin/main, 59f7414
    if (hash != before) { std::printf("FAIL unknown envelope: frames hash %016llx, before %016llx\n", hash, before); ++failures; }

    // 2. A short envelope against a long one: the init sound, and the same
    // drawn further, into chaos.
    const double chaos[8] = {0.9, 0.3571, 0, 0.39, 0.15, 0.125, 0, 0};
    const double* scenes[2] = {defaults, chaos};
    const char* names[2] = {"init", "chaos"};
    for (int s = 0; s < 2; ++s) {
        const Run a = play(scenes[s], short_one, 60, 360, false), b = play(scenes[s], long_one, 60, 360, false);
        std::printf("envelope: %s: after the note-off the strobe runs %.2f s short, %.2f s long; the screen moves %.2f s against %.2f s\n",
                    names[s], a.stopped / 60.0, b.stopped / 60.0, (a.last_change + 1) / 60.0, (b.last_change + 1) / 60.0);
        std::printf("envelope: %s: widest swing 0.25 s in %.1f px short, %.1f px long; 0.75 s after the note-off %.1f px long\n",
                    names[s], a.early, b.early, b.late);
        if (a.stopped < 0 || a.stopped > 6) { std::printf("FAIL %s: a short release runs the strobe %d frames after the note-off\n", names[s], a.stopped); ++failures; }
        if (a.last_change > 60) { std::printf("FAIL %s: a short release still moves %d frames after the note-off\n", names[s], a.last_change); ++failures; }
        if (b.stopped < 180) { std::printf("FAIL %s: a long release stops the strobe %d frames after the note-off\n", names[s], b.stopped); ++failures; }
        if (b.early * 2 > a.early) { std::printf("FAIL %s: a slow attack draws the chain out as far early on (%.1f px) as an instant one (%.1f)\n", names[s], b.early, a.early); ++failures; }
        if (b.late < 2) { std::printf("FAIL %s: a long release shows no swing 0.75 s after the note-off (%.1f px)\n", names[s], b.late); ++failures; }
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
            lasted[k] = play(scenes[0], e, 60, 600, false).last_change;
            e = short_one;
            e.attack = attacks[k];
            swell[k] = static_cast<double>(play(scenes[0], e, 60, 60, false).early);
        }
        std::printf("envelope: release 0.25, 0.5, 0.996 move on %.2f, %.2f and %.2f s; attack 0, 0.5, 0.625 move %g, %g and %g early on\n",
                    (lasted[0] + 1) / 60.0, (lasted[1] + 1) / 60.0, (lasted[2] + 1) / 60.0, swell[0], swell[1], swell[2]);
        for (int k = 0; k < 2; ++k) {
            if (!(lasted[k] > lasted[k + 1])) { std::printf("FAIL release %.3f doesn't end the motion sooner than %.3f\n", releases[k + 1], releases[k]); ++failures; }
            if (!(swell[k] > swell[k + 1])) { std::printf("FAIL attack %.3f doesn't swell slower than %.3f\n", attacks[k + 1], attacks[k]); ++failures; }
        }
    }

    // 3. Churn under a held note and its release, at the defaults and the
    // heaviest settings, each envelope.
    const double* churn[2] = {defaults, heavy};
    const Amp* envelopes[2] = {&short_one, &long_one};
    double worst = 0;
    int peak = 0;
    for (int s = 0; s < 2; ++s)
        for (int e = 0; e < 2; ++e) {
            const Run r = play(churn[s], *envelopes[e], 180, 360, true);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
            std::printf("envelope: churn %s %s: %.1f a frame, peak %d, %d at rest, moving %.2f s after the note-off\n", s ? "heavy" : "init",
                        e ? "long" : "short", r.mean, r.peak, r.rest, (r.last_change + 1) / 60.0);
            if (r.mean > 20) { std::printf("FAIL churn: %.1f nodes a frame\n", r.mean); ++failures; }
            if (r.rest) { std::printf("FAIL churn: %d nodes changed at rest\n", r.rest); ++failures; }
        }
    if (peak > 30) { std::printf("FAIL churn: a peak of %d nodes in a frame\n", peak); ++failures; }
    if (failures) { std::printf("envelope: %d failures\n", failures); return 1; }
    std::printf("envelope: unknown unchanged; churn at most %.1f nodes a frame on average, peak %d, none at rest\n", worst, peak);
    return 0;
}
