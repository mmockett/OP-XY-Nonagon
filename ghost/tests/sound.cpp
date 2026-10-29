// Ghost's own checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include <algorithm>
#include "analysis.h"
#include "header_font.h"
#include "../captions.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace measure;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

// One voice, held, with its controls and its start angle set by hand.
struct Rig {
    i32 out[2 * opxy::block_frames], parameters[8];
    u32 pitch[opxy::block_frames];
    opxy::SampleBlock block;
    ghost::Voice voice;
    Rig(double hz, const double* values, const opxy::i16* data = 0, u32 frames = 0) {
        for (u32 i = 0; i < 8; ++i) parameters[i] = i32(values[i] * 2147483647.0);
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = u32(hz / fs * 4294967296.0 + 0.5);
        block = opxy::SampleBlock();
        block.output = out; block.parameters = parameters; block.pitch = pitch;
        block.region.begin = 0; block.region.end = frames; block.region.level = opxy::sample_unity_level;
        block.output_channels = 2;
        const opxy::SampleView view = {data, frames, 44100, 1};
        ghost::initialize(voice, view);
        ghost::start(voice);
        voice.cents = 0;
    }
    void run(double seconds, std::vector<double>& left, std::vector<double>& right) {
        const u32 n = u32(seconds * fs) / opxy::block_frames * opxy::block_frames;
        left.resize(n); right.resize(n);
        for (u32 i = 0; i < n; i += opxy::block_frames) {
            ghost::render_sample(voice, block);
            for (u32 k = 0; k < opxy::block_frames; ++k) { left[i + k] = out[2 * k] / 2147483648.0; right[i + k] = out[2 * k + 1] / 2147483648.0; }
        }
    }
};
// Plain: no split, trade, beat, orbit or bed; a sung "ah" ringing 2 s.
const double plain[8] = {0.6, 0.6, 0, 0, 0, 0, 0.5, 0};

double sum_harmonics(const Spectra& s, const std::vector<double>& ch, double f0, u32 first, u32 step, u32 count) {
    double t = 0;
    for (u32 k = first, n = 0; n < count; k += step, ++n) t += s.harmonic(ch, f0, k);
    return t;
}
// A spectral peak's frequency near hz, by parabolic interpolation of log power.
double peak_near(const Spectra& s, const std::vector<double>& ch, double hz) {
    size_t best = s.bin(hz * 0.97);
    for (size_t k = best; k <= s.bin(hz * 1.03); ++k) if (ch[k] > ch[best]) best = k;
    const double a = std::log(ch[best - 1]), b = std::log(ch[best]), c = std::log(ch[best + 1]);
    return s.hz(best) + 0.5 * (a - c) / (a - 2 * b + c) * fs / s.size;
}
}

int main() {
    std::vector<double> left, right;

    {   // Every preset, with no sample and with one: a held A3 at about
        // Hexagon's level (-24 dBFS), never over half scale, and an eight-note
        // chord that stays below full scale.
        for (u32 sampled = 0; sampled < 2; ++sampled)
        for (const Preset& p : presets) {
            play({{0, 4, 57}}, p.values, 4.3, left, right, sampled != 0, 0.05, 0.2);
            std::vector<double> l(left.begin() + size_t(1.5 * fs), left.begin() + size_t(4 * fs)), r(right.begin() + size_t(1.5 * fs), right.begin() + size_t(4 * fs));
            const double level = 10 * std::log10((rms(l) * rms(l) + rms(r) * rms(r)) / 2);
            check(level > -29 && level < -21, "every preset sounds at about Hexagon's level");
            check(peak(left) <= 0.5 && peak(right) <= 0.5, "a voice stays within half scale");
            std::vector<Note> chord;
            const int notes[8] = {48, 52, 55, 59, 62, 66, 69, 72};
            for (int n : notes) chord.push_back({0, 2.5, double(n)});
            play(chord, p.values, 3, left, right, sampled != 0, 0, 0.3);
            check(std::fmax(peak(left), peak(right)) < 0.95, "an eight-note chord stays below full scale");
            // The mono fold-down: no third of an octave loses more than 10 dB
            // against the ears, and the whole loses at most 6 dB.
            Spectra s(l, r, 8192);
            const std::vector<double> m = fold(l, r);
            check(worst_fold_dip(s) > -10, "the mono fold-down has no deep notch");
            check(db(rms(m)) - 10 * std::log10((rms(l) * rms(l) + rms(r) * rms(r)) / 2) > -6, "the mono fold-down keeps its level");
        }
    }
    {   // In tune: the fused note's fundamental at 110, 220, 440 and 880 Hz, within 3 cents.
        for (double hz : {110.0, 220.0, 440.0, 880.0}) {
            Rig r(hz, plain);
            r.run(4, left, right);
            std::vector<double> l(left.begin() + size_t(1.5 * fs), left.end()), rr(right.begin() + size_t(1.5 * fs), right.end());
            Spectra s(l, rr, 65536);
            const double f = peak_near(s, s.mono, hz);
            check(std::fabs(1200 * std::log2(f / hz)) < 3, "the note is in tune");
        }
    }
    {   // split: each ear gets half the harmonics, even to the left and odd to
        // the right; the fold-down keeps all of them.
        double v[8] = {0.6, 0.6, 0.74, 0, 0, 0, 0.5, 0};
        Rig r(220, v);
        r.run(4, left, right);
        std::vector<double> l(left.begin() + size_t(1.5 * fs), left.end()), rr(right.begin() + size_t(1.5 * fs), right.end());
        Spectra s(l, rr, 16384);
        // Each harmonic is louder in its own ear: even left, odd right
        // ((1 + 0.74) / (1 - 0.74), 16.5 dB, in theory).
        double fewest = 100;
        for (u32 k = 1; k <= 8; ++k) {
            const double side = 10 * std::log10(s.harmonic(s.left, 220, k) / s.harmonic(s.right, 220, k));
            fewest = std::fmin(fewest, k % 2 ? -side : side);
            if (std::getenv("GHOST_REPORT")) std::printf("  h%u: %+.1f dB left over right\n", k, side);
        }
        std::printf("split 0.74: each of the first 8 harmonics at least %.1f dB louder in its own ear\n", fewest);
        check(fewest > 10, "split divides the harmonics between the ears");
        // Each harmonic lives in one ear, so the fold-down holds it 3 dB down
        // against the ears' mean, and never cancels it.
        double weakest = 0;
        for (u32 k = 1; k <= 8; ++k)
            weakest = std::fmin(weakest, 10 * std::log10(s.harmonic(s.mono, 220, k) / ((s.harmonic(s.left, 220, k) + s.harmonic(s.right, 220, k)) / 2)));
        std::printf("split 0.74: the fold-down's weakest harmonic %+.1f dB against the ears\n", weakest);
        check(weakest > -4.5, "the fold-down keeps every harmonic");
    }
    {   // split at full: the fundamental is gone from both ears and the fold-down,
        // so the note's pitch exists only in what its harmonics imply.
        double v[8] = {0.6, 0.6, 1, 0, 0, 0, 0.5, 0};
        Rig r(220, v);
        r.run(4, left, right);
        std::vector<double> l(left.begin() + size_t(1.5 * fs), left.end()), rr(right.begin() + size_t(1.5 * fs), right.end());
        Spectra s(l, rr, 16384);
        const double fundamental = s.harmonic(s.right, 220, 1) + s.harmonic(s.left, 220, 1);
        const double next = s.harmonic(s.left, 220, 2) + s.harmonic(s.right, 220, 3);
        std::printf("split 1: the fundamental %.1f dB under the lowest harmonics\n", 10 * std::log10(fundamental / next));
        check(10 * std::log10(fundamental / next) < -15, "split at full takes out the fundamental");
    }
    {   // trade swaps the halves between the ears: at full a swap and back every 1.5 s.
        double v[8] = {0.6, 0.6, 0.74, 1, 0, 0, 0.5, 0};
        Rig r(220, v);
        r.run(5, left, right);
        double balance[2];
        for (u32 w = 0; w < 2; ++w) {   // around 1.49 s (cos 0) and 2.23 s (cos pi): a half cycle apart
            const size_t at = size_t((1.31 + 0.744 * w) * fs);
            std::vector<double> l(left.begin() + at, left.begin() + at + 16384), rr(right.begin() + at, right.begin() + at + 16384);
            Spectra s(l, rr, 4096);
            balance[w] = 10 * std::log10(sum_harmonics(s, s.left, 220, 2, 2, 4) / sum_harmonics(s, s.left, 220, 1, 2, 4));
        }
        std::printf("trade: the left ear's even over odd %+.1f dB, then %+.1f dB\n", balance[0], balance[1]);
        check(balance[0] * balance[1] < 0, "trade swaps the halves between the ears");
    }
    {   // beat: the right ear sharp by beat Hz (3.57 Hz at 0.7).
        double v[8] = {0.8, 0.6, 0, 0, 0.7, 0, 0.5, 0};
        Rig r(220, v);
        r.run(8, left, right);
        std::vector<double> l(left.begin() + size_t(2 * fs), left.end()), rr(right.begin() + size_t(2 * fs), right.end());
        Spectra s(l, rr, 131072);
        const double fl = peak_near(s, s.left, 220), fr = peak_near(s, s.right, 220);
        std::printf("beat 0.7: left %.2f Hz, right %.2f Hz: a %.2f Hz beat between the ears (asked %.2f)\n",
                    fl, fr, fr - fl, ghost::beat_mhz(ghost::level(r.parameters[ghost::kBeat])) / 1000.0);
        check(std::fabs(fr - fl - 3.57) < 0.25, "beat detunes the right ear by beat Hz");
    }
    {   // The head: at the right, near, the right ear is louder and brighter and
        // the left later by about 12 samples (0.27 ms).
        double v[8] = {0.4, 0.0, 0, 0, 0, 0.25, 1, 0};
        Rig r(220, v);
        r.voice.start_angle = 0x40000000u;   // right
        r.run(3, left, right);
        std::vector<double> l(left.begin() + size_t(fs), left.end()), rr(right.begin() + size_t(fs), right.end());
        Spectra s(l, rr, 8192);
        const double ild = db(rms(rr)) - db(rms(l));
        size_t lag = 0;
        double best = -1;
        for (size_t d = 0; d < 30; ++d) {
            double c = 0;
            for (size_t i = 100; i < l.size(); ++i) c += l[i] * rr[i - d];
            if (c > best) { best = c; lag = d; }
        }
        const double low_ild = 10 * std::log10(s.sum(s.right, 200, 400) / s.sum(s.left, 200, 400));
        const double high_ild = 10 * std::log10(s.sum(s.right, 3000, 6000) / s.sum(s.left, 3000, 6000));
        std::printf("at the right ear: right louder by %.1f dB (%.1f dB at 200-400 Hz, %.1f dB at 3-6 kHz), left later by %zu samples\n",
                    ild, low_ild, high_ild, lag);
        check(ild > 8, "a near source at the right is louder in the right ear");
        check(lag >= 10 && lag <= 14, "and reaches the left ear about 12 samples later");
        check(high_ild > low_ild + 6, "and the head shadows the far ear's highs");
    }
    {   // Behind sounds darker than in front.
        double v[8] = {0.4, 0.0, 0, 0, 0, 0.25, 0.5, 0};
        double centre[2];
        for (u32 k = 0; k < 2; ++k) {
            Rig r(220, v);
            r.voice.start_angle = k ? 0x80000000u : 0;
            r.run(3, left, right);
            std::vector<double> l(left.begin() + size_t(fs), left.end()), rr(right.begin() + size_t(fs), right.end());
            Spectra s(l, rr, 8192);
            centre[k] = 10 * std::log10((s.sum(s.left, 3000, 8000) + s.sum(s.right, 3000, 8000)) / (s.sum(s.left, 50, 8000) + s.sum(s.right, 50, 8000)));
        }
        std::printf("behind: the highs (3-8 kHz) %.1f dB of the whole, against %.1f dB in front\n", centre[1], centre[0]);
        check(centre[1] < centre[0] - 3, "behind is darker than in front");
    }
    {   // orbit circles: at full, the apparition is back where it began after 2 s.
        double v[8] = {0.4, 0.0, 0, 0, 0, 1, 0.5, 0};
        Rig r(220, v);
        r.voice.start_angle = 0;
        r.run(1.0, left, right);
        const double at_half = std::fabs(double(i32(r.voice.circled)) / 2147483648.0);
        r.run(1.0, left, right);
        const double turn = double(r.voice.circled) / 4294967296.0;
        check(std::fabs(at_half - 1) < 0.05 && std::fmin(turn, 1 - turn) < 0.03, "orbit circles the head every 2 s at full");
    }
    {   // The bed: the loaded sample excites the resonators; without one, breath does.
        const std::vector<i16>& recording = voice_recording();
        double v[8] = {0.6, 0.6, 0.5, 0, 0, 0, 0.5, 1};
        Rig with(220, v, recording.data(), u32(recording.size())), without(220, v);
        with.run(3, left, right);
        const double a = rms(left);
        without.run(3, left, right);
        const double b = rms(left);
        check(a > 0.01 && b > 0.01, "the bed plays, and without a sample breath does");
    }
    {   // A loaded sample tells at the defaults (bed at 0): the held note's
        // third-octave spectrum moves by 8 dB or more on average.
        std::vector<double> bands[2];
        for (u32 k = 0; k < 2; ++k) {
            play({{0, 4, 57}}, presets[0].values, 4.3, left, right, k == 1, 0.05, 0.2);
            std::vector<double> l(left.begin() + size_t(1.5 * fs), left.begin() + size_t(4 * fs)), rr(right.begin() + size_t(1.5 * fs), right.begin() + size_t(4 * fs));
            Spectra s(l, rr, 8192);
            double total = 0;
            for (double f = 50; f < 10000; f *= 1.26) { bands[k].push_back(s.sum(s.mono, f, f * 1.26) + 1e-20); total += bands[k].back(); }
            for (double& e : bands[k]) e = 10 * std::log10(e / total);
        }
        double moved = 0;
        for (size_t i = 0; i < bands[0].size(); ++i) moved += std::fabs(bands[1][i] - bands[0][i]) / bands[0].size();
        std::printf("a loaded sample at the defaults moves the spectrum %.1f dB a third-octave\n", moved);
        check(moved > 8, "a loaded sample changes the default sound");
    }
    {   // Without a sample, bed is Ghost's own whisper: noisier, and in gusts.
        double v[8] = {0.6, 0.6, 0.6, 0, 0, 0, 0.5, 0};
        double noise[2], spread[2];
        for (u32 k = 0; k < 2; ++k) {
            v[7] = k;
            Rig r(220, v);
            r.run(6, left, right);
            std::vector<double> l(left.begin() + size_t(2 * fs), left.end()), rr(right.begin() + size_t(2 * fs), right.end());
            Spectra s(l, rr, 32768);
            noise[k] = (s.hnr(s.left, 220, 5000) + s.hnr(s.right, 220, 5000)) / 2;
            std::vector<double> d;   // 25 ms levels, 10th to 90th percentile
            for (size_t at = 0; at + 1102 <= l.size(); at += 1102) d.push_back(db(rms(std::vector<double>(l.begin() + at, l.begin() + at + 1102))));
            std::sort(d.begin(), d.end());
            spread[k] = d[d.size() * 9 / 10] - d[d.size() / 10];
        }
        std::printf("bed without a sample: harmonic to noise %.1f dB, then %.1f dB at full; level spread %.1f dB, then %.1f dB\n", noise[0], noise[1], spread[0], spread[1]);
        check(noise[1] < noise[0] - 5 && spread[1] > spread[0] + 3, "without a sample, bed whispers in gusts");
    }
    {   // Sixteen notes sound at once on every track; a seventeenth waits silent,
        // and sounds once one has ended.
        std::vector<Rig*> rigs;
        for (u32 n = 0; n < 17; ++n) rigs.push_back(new Rig(110 + 20 * n, plain));
        for (u32 b = 0; b < 40; ++b) for (Rig* r : rigs) ghost::render_sample(r->voice, r->block);
        i32 loudest = 0;
        for (u32 i = 0; i < 2 * opxy::block_frames; ++i) loudest = std::max(loudest, std::abs(rigs[16]->out[i]));
        check(loudest == 0, "a seventeenth note waits for a line");
        for (u32 b = 0; b < 4; ++b) for (u32 n = 1; n < 17; ++n) ghost::render_sample(rigs[n]->voice, rigs[n]->block);   // the first ends
        for (u32 b = 0; b < 40; ++b) ghost::render_sample(rigs[16]->voice, rigs[16]->block);
        loudest = 0;
        for (u32 i = 0; i < 2 * opxy::block_frames; ++i) loudest = std::max(loudest, std::abs(rigs[16]->out[i]));
        check(loudest > 0, "and sounds once a line is free");
        for (Rig* r : rigs) delete r;
    }
    {   // Mono output.
        Rig r(220, plain);
        r.block.output_channels = 1;
        for (u32 b = 0; b < 40; ++b) ghost::render_sample(r.voice, r.block);
        bool sound = false;
        for (u32 i = 0; i < opxy::block_frames; ++i) sound = sound || r.out[i] != 0;
        check(sound, "mono output sounds");
    }
    {   // Every caption fits its header column on the device, in its own
        // face (tests/header_font.h): each control's value, swept in 4097
        // steps, within 53 px at 20 px, and each title within 56 px at 10 px.
        struct Width {
            static double of(const char* text, double px) {
                double units = 0;
                for (; *text; ++text) units += header_font_advance[(*text >= 32 && *text <= 126 ? *text : '?') - 32];
                return units * px / header_font_units;
            }
        };
        double widest = 0;
        char which[opxy::draw::header_capacity] = "";
        for (u32 c = 0; c < 8; ++c)
            for (u32 v = 0; v <= 4096; ++v) {
                opxy::draw::Input input = {};
                input.slots[c] = v == 4096 ? 0x7fffffff : i32(v << 19);
                char values[8][opxy::draw::header_capacity] = {};
                ghost::captions::fill(input, (v & 1) != 0, values);   // with a sample and without
                const double w = Width::of(values[c], 20);
                if (w > widest) { widest = w; opxy::draw::copy(which, sizeof which, values[c]); }
            }
        double widest_title = 0;
        for (const char* title : ghost::captions::titles) widest_title = std::fmax(widest_title, Width::of(title, 10));
        std::printf("captions: the widest value \"%s\" %.1f px (53 allowed), the widest title %.1f px (56)\n", which, widest, widest_title);
        check(widest <= 53 && widest_title <= 56, "every caption fits its column on the device");
    }
    if (failures) { std::printf("%d failures\n", failures); return 1; }
    std::printf("ghost: all sound checks passed\n");
    return 0;
}
