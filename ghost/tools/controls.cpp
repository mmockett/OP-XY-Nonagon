// Measures what each control does at 0, 25, 50, 75 and 100, the others at the
// default (Seance), on a held A3: the README's control table.
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/controls.cpp -o controls && ./controls
#include "../tests/analysis.h"
#include <cstdio>

namespace {
using namespace measure;
const double seance[8] = {0.6, 0.6, 0.6, 0.3, 0.35, 0.2, 0.4, 0};

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
}

int main() {
    const double steps[5] = {0, 0.25, 0.5, 0.75, 1};
    std::vector<double> l, r;
    std::printf("| Control | 0 | 25 | 50 | 75 | 100 |\n| --- | --- | --- | --- | --- | --- |\n");

    std::printf("| ring: harmonic to noise |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[0] = v; hold(p, 5, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 32768); std::printf(" %.0f dB |", (s.hnr(s.left, 220, 5000) + s.hnr(s.right, 220, 5000)) / 2); }
    std::printf("\n| ring: rise to within 3 dB of its level |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[0] = v; hold(p, 5, l, r);
        const double final_db = db(rms(tail(l, 4))); double t = 0;
        for (size_t at = 0; at + 2205 < l.size(); at += 2205) { std::vector<double> w(l.begin() + at, l.begin() + at + 2205); if (db(rms(w)) > final_db - 3) { t = at / fs; break; } }
        std::printf(" %.2f s |", t); }

    std::printf("\n| voice: spectral centroid |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[1] = v; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 8192); std::printf(" %.0f Hz |", (s.centroid(s.left) + s.centroid(s.right)) / 2); }
    std::printf("\n| voice: harmonic to noise |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[1] = v; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 32768); std::printf(" %.0f dB |", (s.hnr(s.left, 220, 5000) + s.hnr(s.right, 220, 5000)) / 2); }

    std::printf("\n| split: each harmonic in its own ear (trade still) |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[2] = v; p[3] = 0; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 16384); double fewest = 100;
        for (u32 k = 1; k <= 8; ++k) { const double side = 10 * std::log10(s.harmonic(s.left, 220, k) / s.harmonic(s.right, 220, k)); fewest = std::fmin(fewest, k % 2 ? -side : side); }
        std::printf(" %+.1f dB |", fewest); }
    std::printf("\n| split: the fundamental against the third harmonic, in the odd ear |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[2] = v; p[3] = 0; hold(p, 4, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 16384);
        std::printf(" %+.0f dB |", 10 * std::log10(s.harmonic(s.right, 220, 1) / s.harmonic(s.right, 220, 3))); }

    std::printf("\n| trade: a full swap and back (measured) |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[3] = v; hold(p, 70, l, r);
        // Crossings from even-left (above +3 dB) to odd-left (below -3 dB), with hysteresis.
        double first = -1, second = -1; int side = 0;
        for (double t = 1.0; t < 69.5; t += 0.1) {
            const double b = balance(l, size_t(t * fs));
            if (b > 3 && side != 1) { if (side == -1) { if (first < 0) first = t; else { second = t; break; } } side = 1; }
            else if (b < -3) side = -1;
        }
        if (second > 0) std::printf(" %.1f s |", second - first); else std::printf(" still (or over 35 s) |"); }

    std::printf("\n| beat: the right ear's fundamental above the left's |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[4] = v; hold(p, 8, l, r);
        Spectra s(tail(l, 2), tail(r, 2), 131072); std::printf(" %.2f Hz |", s.fundamental(s.right, 220) - s.fundamental(s.left, 220)); }

    std::printf("\n| orbit (a note begun 67 degrees right): where it is, as the left ear's delay over 4 s |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[5] = v; p[1] = 0; p[2] = 0; p[4] = 0; hold(p, 6, l, r, 0x30000000u);
        int lo = 99, hi = -99;
        for (double t = 2; t < 6 - 0.1; t += 0.1) {
            const size_t at = size_t(t * fs);
            int best = 0; double top = -1e30;
            for (int d = -16; d <= 16; ++d) { double c = 0; for (size_t i = 20; i < 4410; ++i) c += l[at + i] * r[at + i - d]; if (c > top) { top = c; best = d; } }
            lo = std::min(lo, best); hi = std::max(hi, best);
        }
        if (lo == hi) std::printf(" %+d samples |", lo); else std::printf(" %+d to %+d samples |", lo, hi); }

    std::printf("\n| near (a note at the right): its level, and right over left |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[6] = v; p[5] = 0.25; hold(p, 4, l, r, 0x40000000u);
        std::vector<double> a = tail(l, 2), b = tail(r, 2); std::printf(" %.1f dBFS, %+.1f dB |", power_db(a, b), db(rms(b)) - db(rms(a))); }

    std::printf("\n| bed (a recorded voice loaded): harmonic to noise |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[7] = v; hold(p, 4, l, r, 0, true);
        Spectra s(tail(l, 2), tail(r, 2), 32768); std::printf(" %.0f dB |", (s.hnr(s.left, 220, 5000) + s.hnr(s.right, 220, 5000)) / 2); }
    std::printf("\n| bed: spectral centroid |");
    for (double v : steps) { double p[8]; std::copy(seance, seance + 8, p); p[7] = v; hold(p, 4, l, r, 0, true);
        Spectra s(tail(l, 2), tail(r, 2), 8192); std::printf(" %.0f Hz |", (s.centroid(s.left) + s.centroid(s.right)) / 2); }
    std::printf("\n");
    return 0;
}
