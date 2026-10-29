// Host-side rendering and measurement for Ghost's tests and tools/measure.cpp:
// Ghost's voices played as the OP-XY would sum them, and what a listener's
// ears would get from them, in numbers.
#pragma once
#include "../sample.cpp"
#include <cmath>
#include <complex>
#include <vector>

namespace measure {
using opxy::i16;
using opxy::i32;
using opxy::u32;
typedef std::complex<double> cx;
const double fs = 44100;
const char* const names[8] = {"ring", "voice", "split", "trade", "beat", "orbit", "near", "bed"};

struct Preset { const char* name; double values[8]; };
// definition.json's presets, in its order.
const Preset presets[8] = {
    {"Seance", {0.6, 0.6, 0.6, 0.3, 0.35, 0.2, 0.4, 0}},
    {"Whisper", {0.25, 0.0, 0.4, 0.2, 0.0, 0.22, 0.95, 0}},
    {"Choir", {0.75, 0.42, 0.8, 0.15, 0.25, 0.5, 0.3, 0}},
    {"Revenant", {0.95, 0.2, 0.5, 0.1, 0.6, 0.0, 0.5, 0}},
    {"Wraith", {0.5, 0.1, 0.3, 0.0, 0.2, 0.85, 0.6, 0}},
    {"Skull", {0.7, 0.8, 1.0, 0.6, 0.0, 0.0, 0.5, 0}},
    {"Glass", {0.9, 1.0, 0.5, 0.2, 0.3, 0.15, 0.5, 0}},
    {"Haunting", {0.7, 0.5, 0.7, 0.25, 0.3, 0.4, 0.4, 1.0}},
};

double midi_hz(double midi) { return 440 * std::pow(2.0, (midi - 69) / 12); }

// A recording to use as the bed: 6 s of a breathy, wordless voice, mono.
inline const std::vector<i16>& voice_recording() {
    static std::vector<i16> pcm;
    if (!pcm.empty()) return pcm;
    const u32 n = u32(6 * fs);
    pcm.resize(n);
    u32 seed = 99;
    double phase = 0, low = 0, band = 0, low2 = 0, band2 = 0;
    for (u32 i = 0; i < n; ++i) {
        const double t = i / fs;
        const double f0 = 180 + 25 * std::sin(2 * M_PI * 0.3 * t) + 10 * std::sin(2 * M_PI * 1.7 * t);
        phase += f0 / fs;
        phase -= std::floor(phase);
        seed = seed * 1664525u + 1013904223u;
        const double noise = (seed >> 8) / 8388608.0 - 1;
        const double pulse = std::pow(std::fmax(0.0, std::sin(2 * M_PI * phase)), 3) - 0.2;
        const double x = 0.6 * pulse + 0.25 * noise;
        // Two formants drifting between "ah" and "oh".
        const double f1 = 600 + 150 * std::sin(2 * M_PI * 0.21 * t), f2 = 1000 + 150 * std::sin(2 * M_PI * 0.13 * t);
        const double a1 = 2 * std::sin(M_PI * f1 / fs), a2 = 2 * std::sin(M_PI * f2 / fs);
        low += a1 * band; band += a1 * (x - low - 0.15 * band);
        low2 += a2 * band2; band2 += a2 * (x - low2 - 0.15 * band2);
        const double env = 0.6 + 0.4 * std::sin(2 * M_PI * 0.5 * t);
        pcm[i] = i16(std::lround(std::fmax(-1.0, std::fmin(1.0, 0.12 * (band + 0.7 * band2) * env)) * 32767));
    }
    return pcm;
}

struct Note { double start, length, midi; };

// Plays notes through Ghost (each its own voice, under a simple envelope:
// attack, full sustain, release) into left and right, 1.0 = full scale.
void play(const std::vector<Note>& notes, const double* values, double seconds, std::vector<double>& left,
          std::vector<double>& right, bool with_bed = false, double attack = 0.05, double release = 0.6) {
    const u32 total = u32(seconds * fs) / opxy::block_frames * opxy::block_frames;
    left.assign(total, 0);
    right.assign(total, 0);
    static i32 out[2 * opxy::block_frames], parameters[8];
    static u32 pitch[opxy::block_frames];
    for (u32 i = 0; i < 8; ++i) parameters[i] = i32(values[i] * 2147483647.0);
    const std::vector<i16>& bed = voice_recording();
    const opxy::SampleView view = with_bed ? opxy::SampleView{bed.data(), u32(bed.size()), 44100, 1} : opxy::SampleView{0, 0, 44100, 1};
    // Voices sound together, as on the device: each block renders every
    // sounding voice, so they share the line pool as they would there.
    std::vector<ghost::Voice> voices(notes.size());
    std::vector<bool> begun(notes.size(), false);
    opxy::SampleBlock block = opxy::SampleBlock();
    block.output = out; block.parameters = parameters; block.pitch = pitch;
    block.region.begin = 0; block.region.end = view.frames; block.region.level = opxy::sample_unity_level;
    block.output_channels = 2;
    for (u32 at = 0; at < total; at += opxy::block_frames) {
        for (u32 n = 0; n < notes.size(); ++n) {
            const double t0 = at / fs - notes[n].start;
            if (t0 < 0 || t0 > notes[n].length + release) continue;
            if (!begun[n]) { ghost::initialize(voices[n], view); ghost::start(voices[n]); begun[n] = true; }
            const u32 increment = u32(midi_hz(notes[n].midi) / fs * 4294967296.0 + 0.5);
            for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
            ghost::render_sample(voices[n], block);
            for (u32 k = 0; k < opxy::block_frames; ++k) {
                const double t = t0 + k / fs;
                double gain = attack > 0 && t < attack ? t / attack : 1;
                if (t > notes[n].length) gain *= std::fmax(0.0, 1 - (t - notes[n].length) / release);
                left[at + k] += gain * out[2 * k] / 2147483648.0;
                right[at + k] += gain * out[2 * k + 1] / 2147483648.0;
            }
        }
    }
}

void fft(std::vector<cx>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const cx step = std::polar(1.0, -2 * M_PI / double(len));
        for (size_t i = 0; i < n; i += len) {
            cx w = 1;
            for (size_t k = 0; k < len / 2; ++k, w *= step) {
                const cx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Welch averages over Hann frames: each ear's power, their cross-spectrum
// and the mono fold-down's power.
struct Spectra {
    size_t size;
    std::vector<double> left, right, mono;
    std::vector<cx> cross;
    Spectra(const std::vector<double>& l, const std::vector<double>& r, size_t n, size_t from = 0)
        : size(n), left(n / 2), right(n / 2), mono(n / 2), cross(n / 2) {
        std::vector<cx> a(n), b(n);
        for (size_t start = from; start + n <= l.size(); start += n / 2) {
            for (size_t i = 0; i < n; ++i) {
                const double h = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
                a[i] = l[start + i] * h;
                b[i] = r[start + i] * h;
            }
            fft(a); fft(b);
            for (size_t k = 0; k < n / 2; ++k) {
                left[k] += std::norm(a[k]);
                right[k] += std::norm(b[k]);
                mono[k] += std::norm((a[k] + b[k]) * 0.5);
                cross[k] += a[k] * std::conj(b[k]);
            }
        }
    }
    size_t bin(double hz) const { return size_t(hz * size / fs + 0.5); }
    double hz(size_t k) const { return k * fs / size; }
    double sum(const std::vector<double>& s, double lo, double hi) const {
        double t = 0;
        for (size_t k = bin(lo); k <= bin(hi) && k < s.size(); ++k) t += s[k];
        return t;
    }
    // Over a band: magnitude-squared coherence between the ears, and the
    // interaural phase (degrees) of their summed cross-spectrum.
    double coherence(double lo, double hi) const {
        cx c = 0; double pl = 0, pr = 0;
        for (size_t k = bin(lo); k <= bin(hi) && k < cross.size(); ++k) { c += cross[k]; pl += left[k]; pr += right[k]; }
        return std::norm(c) / (pl * pr + 1e-30);
    }
    double phase(double lo, double hi) const {
        cx c = 0;
        for (size_t k = bin(lo); k <= bin(hi) && k < cross.size(); ++k) c += cross[k];
        return std::arg(c) * 180 / M_PI;
    }
    // The power within 1.5% of each harmonic of f0 up to `top`, by harmonic.
    double harmonic(const std::vector<double>& s, double f0, u32 k) const { return sum(s, k * f0 * 0.985, k * f0 * 1.015); }
    // Harmonic to noise: power on the harmonics against the rest, up to top Hz, dB.
    double hnr(const std::vector<double>& s, double f0, double top) const {
        double on = 0, all = sum(s, 40, top);
        for (u32 k = 1; k * f0 < top; ++k) on += harmonic(s, f0, k);
        return 10 * std::log10(on / std::fmax(all - on, 1e-30));
    }
    // The fundamental near hz in a channel: the strongest bin within 3%, refined
    // by a parabola through its neighbours' log power.
    double fundamental(const std::vector<double>& s, double hz) const {
        size_t best = bin(hz * 0.97);
        for (size_t k = best; k <= bin(hz * 1.03); ++k) if (s[k] > s[best]) best = k;
        const double a = std::log(s[best - 1] + 1e-30), b = std::log(s[best] + 1e-30), c = std::log(s[best + 1] + 1e-30);
        const double d = a - 2 * b + c;
        return this->hz(best) + (d != 0 ? 0.5 * (a - c) / d : 0) * fs / size;
    }
    double centroid(const std::vector<double>& s) const {
        double a = 0, b = 0;
        for (size_t k = 1; k < s.size(); ++k) { a += hz(k) * s[k]; b += s[k]; }
        return a / b;
    }
};

double rms(const std::vector<double>& x, size_t from = 0) {
    double s = 0;
    for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / (x.size() - from));
}
double peak(const std::vector<double>& x) {
    double p = 0;
    for (double v : x) p = std::fmax(p, std::fabs(v));
    return p;
}
double db(double x) { return 20 * std::log10(std::fmax(x, 1e-12)); }
std::vector<double> fold(const std::vector<double>& l, const std::vector<double>& r) {
    std::vector<double> m(l.size());
    for (size_t i = 0; i < l.size(); ++i) m[i] = (l[i] + r[i]) * 0.5;
    return m;
}
// The mono fold-down's worst third-octave dip against the ears' own power
// (their mean), 100 Hz to 8 kHz, dB: a notch the sum would add.
double worst_fold_dip(const Spectra& s) {
    double worst = 0;
    for (double lo = 100; lo < 8000; lo *= 1.26) {
        const double ears = (s.sum(s.left, lo, lo * 1.26) + s.sum(s.right, lo, lo * 1.26)) / 2;
        const double mono = s.sum(s.mono, lo, lo * 1.26);
        if (ears > 1e-12) worst = std::fmin(worst, 10 * std::log10(mono / ears));
    }
    return worst;
}
}
