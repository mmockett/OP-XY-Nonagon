// Measures what each control does at 0, 25, 50, 75 and 100, the others at the
// default (Seance), on a held A3: the README's control table. With --fifths,
// each measure at 21 points and its flattest fifth of travel, as a share of
// its whole change (a name fragment after it picks the measures).
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/controls.cpp -o controls && ./controls [--fifths [name]]
#include "../tests/analysis.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {
using namespace measure;
const double seance[8] = {0.6, 0.6, 0.6, 0.4, 0.2, 0.2, 0.4, 0.5};

// One voice held for seconds, its start angle set, into left and right.
void hold(const double* values, double seconds, std::vector<double>& left, std::vector<double>& right,
          u32 start_angle = 0, bool with_bed = false) {
    static i32 out[2 * opxy::block_frames], parameters[8];
    static u32 pitch[opxy::block_frames];
    for (u32 i = 0; i < 8; ++i) parameters[i] = i32(values[i] * 2147483647.0);
    for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = u32(220 / fs * 4294967296.0 + 0.5);
    const std::vector<i16>& rec = voice_recording();
    const opxy::SampleView view = with_bed ? opxy::SampleView{rec.data(), u32(rec.size()), 44100, 1} : opxy::SampleView{0, 0, 44100, 1};
    opxy::SampleBlock block = opxy::SampleBlock();
    block.output = out; block.parameters = parameters; block.pitch = pitch;
    block.region.begin = 0; block.region.end = view.frames; block.region.level = opxy::sample_unity_level;
    block.output_channels = 2;
    static ghost::Voice voice;
    ghost::initialize(voice, view);
    ghost::start(voice);
    voice.cents = 0;
    voice.start_angle = start_angle;
    const u32 n = u32(seconds * fs) / opxy::block_frames * opxy::block_frames;
    left.resize(n); right.resize(n);
    for (u32 i = 0; i < n; i += opxy::block_frames) {
        ghost::render_sample(voice, block);
        for (u32 k = 0; k < opxy::block_frames; ++k) { left[i + k] = out[2 * k] / 2147483648.0; right[i + k] = out[2 * k + 1] / 2147483648.0; }
    }
}
std::vector<double> tail(const std::vector<double>& x, double from) { return std::vector<double>(x.begin() + size_t(from * fs), x.end()); }
double power_db(const std::vector<double>& l, const std::vector<double>& r) { return 10 * std::log10((rms(l) * rms(l) + rms(r) * rms(r)) / 2); }
// The left ear's even harmonics against its odd (dB), over a short window.
double balance(const std::vector<double>& l, size_t at) {
    std::vector<double> a(l.begin() + at, l.begin() + at + 8192);
    Spectra s(a, a, 4096);
    double even = 0, odd = 0;
    for (u32 k = 1; k <= 8; ++k) (k % 2 ? odd : even) += s.harmonic(s.left, 220, k);
    return 10 * std::log10(even / odd);
}
// How far the level swells and sinks: the spread (dB) of 25 ms RMS windows,
// 10th to 90th percentile.
double swell(const std::vector<double>& x) {
    const size_t w = size_t(0.025 * fs);
    std::vector<double> d;
    for (size_t at = 0; at + w <= x.size(); at += w) d.push_back(db(rms(std::vector<double>(x.begin() + at, x.begin() + at + w))));
    std::sort(d.begin(), d.end());
    return d[d.size() * 9 / 10] - d[d.size() / 10];
}
std::vector<double> mono(const std::vector<double>& l, const std::vector<double>& r) {
    std::vector<double> m(l.size());
    for (size_t i = 0; i < l.size(); ++i) m[i] = (l[i] + r[i]) / 2;
    return m;
}

std::vector<double> l, r;
// The controls at Seance, control c at v.
const double* at(int c, double v) {
    static double p[8];
    std::copy(seance, seance + 8, p);
    p[c] = v;
    return p;
}
double hnr(double from) { Spectra s(tail(l, from), tail(r, from), 32768); return (s.hnr(s.left, 220, 5000) + s.hnr(s.right, 220, 5000)) / 2; }
double centroid(double from) { Spectra s(tail(l, from), tail(r, from), 8192); return (s.centroid(s.left) + s.centroid(s.right)) / 2; }

// Each measure takes the control's value and gives a number.
struct Measure { const char* name; const char* unit; std::function<double(double)> f; };
const Measure measures[] = {
    {"ring: harmonic to noise", "dB", [](double v) { hold(at(0, v), 5, l, r); return hnr(2); }},
    {"ring: rise to within 3 dB of its level (both ears; bed's gusts still)", "s", [](double v) { double* p = const_cast<double*>(at(0, v)); p[7] = 0; hold(p, 5, l, r);
        const double final_db = power_db(tail(l, 4), tail(r, 4));
        for (size_t t = 0; t + 2205 < l.size(); t += 2205) {
            const std::vector<double> a(l.begin() + t, l.begin() + t + 2205), b(r.begin() + t, r.begin() + t + 2205);
            if (power_db(a, b) > final_db - 3) return t / fs;
        }
        return 0.0; }},
    {"voice: spectral centroid", "Hz", [](double v) { hold(at(1, v), 4, l, r); return centroid(2); }},
    {"voice: harmonic to noise", "dB", [](double v) { hold(at(1, v), 4, l, r); return hnr(2); }},
    {"split: each of the first 8 harmonics louder in its own ear, at least (trade still)", "dB", [](double v) {
        double p[8]; std::copy(seance, seance + 8, p); p[2] = v; p[3] = 0; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 16384); double fewest = 100;
        for (u32 k = 1; k <= 8; ++k) { const double side = 10 * std::log10(s.harmonic(s.left, 220, k) / s.harmonic(s.right, 220, k)); fewest = std::fmin(fewest, k % 2 ? -side : side); }
        return fewest; }},
    {"split: the fundamental against the third harmonic, in the odd ear", "dB", [](double v) {
        double p[8]; std::copy(seance, seance + 8, p); p[2] = v; p[3] = 0; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 16384); return 10 * std::log10(s.harmonic(s.right, 220, 1) / s.harmonic(s.right, 220, 3)); }},
    {"trade: a full swap and back, measured with beat and bed's gusts still (0: still)", "s", [](double v) { double* p = const_cast<double*>(at(3, v)); p[4] = 0; p[7] = 0; hold(p, 54, l, r);
        // Crossings from even-left (above +3 dB) to odd-left (below -3 dB), with hysteresis.
        double first = -1; int side = 0;
        for (double t = 1.0; t < 53.5; t += 0.05) {
            const double b = balance(l, size_t(t * fs));
            if (b > 3 && side != 1) { if (side == -1) { if (first < 0) first = t; else return t - first; } side = 1; }
            else if (b < -3) side = -1;
        }
        return 0.0; }},
    {"beat: the right ear's fundamental above the left's", "Hz", [](double v) { hold(at(4, v), 8, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 131072); return s.fundamental(s.right, 220) - s.fundamental(s.left, 220); }},
    {"beat: how far the fold-down swells and sinks (25 ms windows, 10-90%)", "dB", [](double v) { hold(at(4, v), 8, l, r); return swell(tail(mono(l, r), 2)); }},
    {"orbit (a note begun 67 degrees right): how far the left ear's delay travels over 4 s", "samples", [](double v) {
        double p[8]; std::copy(seance, seance + 8, p); p[5] = v; p[1] = 0; p[2] = 0; p[4] = 0; hold(p, 6, l, r, 0x30000000u);
        int lo = 99, hi = -99;
        for (double t = 2; t < 6 - 0.1; t += 0.1) {
            const size_t a = size_t(t * fs);
            int best = 0; double top = -1e30;
            for (int d = -16; d <= 16; ++d) { double c = 0; for (size_t i = 20; i < 4410; ++i) c += l[a + i] * r[a + i - d]; if (c > top) { top = c; best = d; } }
            lo = std::min(lo, best); hi = std::max(hi, best);
        }
        return double(hi - lo); }},
    {"orbit: where it starts, as the left ear's delay", "samples", [](double v) {
        double p[8]; std::copy(seance, seance + 8, p); p[5] = v; p[1] = 0; p[2] = 0; p[4] = 0; hold(p, 2.2, l, r, 0x30000000u);
        const size_t a = size_t(2 * fs); int best = 0; double top = -1e30;
        for (int d = -16; d <= 16; ++d) { double c = 0; for (size_t i = 20; i < 4410; ++i) c += l[a + i] * r[a + i - d]; if (c > top) { top = c; best = d; } }
        return double(best); }},
    {"near (a note at the right, split and trade still): its level", "dBFS", [](double v) { double* p = const_cast<double*>(at(6, v)); p[5] = 0.25; p[2] = p[3] = 0; hold(p, 4, l, r, 0x40000000u);
        return power_db(tail(l, 2), tail(r, 2)); }},
    {"near: right over left", "dB", [](double v) { double* p = const_cast<double*>(at(6, v)); p[5] = 0.25; p[2] = p[3] = 0; hold(p, 4, l, r, 0x40000000u);
        return db(rms(tail(r, 2))) - db(rms(tail(l, 2))); }},
    {"near: spectral centroid", "Hz", [](double v) { double* p = const_cast<double*>(at(6, v)); p[5] = 0.25; p[2] = p[3] = 0; hold(p, 4, l, r, 0x40000000u); return centroid(2); }},
    {"bed (no sample: its own whisper): harmonic to noise", "dB", [](double v) { hold(at(7, v), 6, l, r); return hnr(2); }},
    {"bed (no sample): how far it swells and sinks (25 ms windows, 10-90%)", "dB", [](double v) { hold(at(7, v), 6, l, r); return swell(tail(mono(l, r), 2)); }},
    {"bed (no sample): spectral centroid", "Hz", [](double v) { hold(at(7, v), 6, l, r); return centroid(2); }},
    {"bed (a recorded voice loaded): harmonic to noise", "dB", [](double v) { hold(at(7, v), 4, l, r, 0, true); return hnr(2); }},
    {"bed (a recorded voice loaded): spectral centroid", "Hz", [](double v) { hold(at(7, v), 4, l, r, 0, true); return centroid(2); }},
};
}

int main(int argc, char** argv) {
    const bool fifths = argc > 1 && !std::strcmp(argv[1], "--fifths");
    const char* only = argc > 2 ? argv[2] : 0;
    if (!fifths) std::printf("| Control | 0 | 25 | 50 | 75 | 100 |\n| --- | --- | --- | --- | --- | --- |\n");
    for (const Measure& m : measures) {
        if (only && !std::strstr(m.name, only)) continue;
        if (!fifths) {
            std::printf("| %s |", m.name);
            for (double v : {0.0, 0.25, 0.5, 0.75, 1.0}) std::printf(" %.2f %s |", m.f(v), m.unit);
            std::printf("\n");
            std::fflush(stdout);
            continue;
        }
        // The flattest fifth: the least change over any 4 of the 20 steps, as a share of the whole.
        double y[21];
        for (int i = 0; i <= 20; ++i) y[i] = m.f(i / 20.0);
        const double whole = *std::max_element(y, y + 21) - *std::min_element(y, y + 21);
        double least = 1e9; int where = 0;
        for (int i = 0; i + 4 <= 20; ++i) {
            const double d = *std::max_element(y + i, y + i + 5) - *std::min_element(y + i, y + i + 5);
            if (d < least) { least = d; where = i; }
        }
        std::printf("%s: range %.2f %s, flattest fifth %.2f (%.0f%%) from %d%%:", m.name, whole, m.unit, least, 100 * least / (whole ? whole : 1), where * 5);
        for (int i = 0; i <= 20; i += 2) std::printf(" %.2f", y[i]);
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}
