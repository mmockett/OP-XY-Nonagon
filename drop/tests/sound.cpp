// Drop's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. They play single drops through the
// engine and measure what comes out:
//   - under the Leidenfrost point the drop is gone within 0.6 s, as broadband
//     crackle; over it, it lives seconds and beats periodically;
//   - its lobes step down one at a time, 8 to 2, then a sphere, then gone;
//   - the beat rate heard follows the star's mode, and falls back at each step;
//   - each beat lasts as many vents as the star has lobes: shorter each step;
//   - the tone inside the beats is at the key;
//   - the last, round drop's beats quicken into a rising chirp;
//   - level, chords, the declared peak and silence at the end.
#include "../engine.cpp"
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace {
const double rate = 44100, block_rate = rate / 64;
int failures = 0;
void check(bool ok, const char* what, double a = 0, double b = 0) {
    std::printf("%s %s (%.4g, %.4g)\n", ok ? "ok  " : "FAIL", what, a, b);
    if (!ok) ++failures;
}

opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + 0.5); }
opxy::u32 increment(double hz) { return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5); }
double key(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

struct Controls { double c[8]; };
// heat, size, liquid, beat, groove, vapour, life, crackle
const Controls floating = {{0.5, 1.0, 0, 1.0, 0, 0.4, 0.55, 0.3}};   // water at 250 C, eight lobes, the papers' 23 Hz beats
const Controls boiling = {{0.2, 1.0, 0, 1.0, 0, 0.4, 0.55, 0.3}};    // water at 160 C
const Controls counted = {{0.5, 1.0, 0, 0.18, 0, 0.35, 0.75, 0.25}}; // slow beats, about one a second, a longer life
const Controls defaults = {{0.5, 0.85, 0, 0.18, 0, 0.35, 0.55, 0.25}};   // definition.json's

// One voice's life: its output and, each block, the drop as the engine keeps it.
struct Life {
    std::vector<double> out;
    std::vector<unsigned> lobes, beats;
    std::vector<double> model_rate;   // the model's beat rate, Hz
    unsigned gone = 0;                // the block the drop was gone, or 0
    unsigned quiet = 0;               // the block the voice fell silent, or 0
    bool bounded = true;
};
Life play(const Controls& controls, int midi, double seconds) {
    static drop::Voice voice;
    static drop::Shared shared;
    Life life;
    opxy::i32 parameters[8];
    for (int k = 0; k < 8; ++k) parameters[k] = q31(controls.c[k]);
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = increment(key(midi));
    shared.sounding = 0;
    drop::initialize(voice, shared, 1);
    drop::start(voice, shared, 1);
    const drop::physics::Plate plate = drop::physics::plate(parameters);
    const unsigned blocks = static_cast<unsigned>(seconds * block_rate);
    for (unsigned b = 0; b < blocks; ++b) {
        opxy::i32 o[64];
        drop::render(voice, shared, 1, o, parameters, pitch);
        for (int i = 0; i < 64; ++i) {
            life.out.push_back(o[i] / 2147483648.0);
            life.bounded = life.bounded && o[i] >= -1073741824 && o[i] <= 1073741824;
        }
        life.lobes.push_back(voice.drop.lobes);
        life.beats.push_back(voice.beats);
        life.model_rate.push_back(voice.drop.lobes ? std::pow(2.0, drop::physics::beat_rate(voice.drop, plate) / 65536.0) : 0);
        if (!life.gone && !voice.drop.lobes) life.gone = b;
        if (!life.quiet && voice.quiet) life.quiet = b;
    }
    return life;
}

double rms(const std::vector<double>& x, double t0, double t1) {
    double s = 0;
    const size_t a = static_cast<size_t>(t0 * rate), b = static_cast<size_t>(t1 * rate);
    for (size_t i = a; i < b && i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(b - a));
}
double db(double v) { return 20 * std::log10(v + 1e-12); }

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2 * M_PI / static_cast<double>(len);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> u(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<double> p = a[i + j], q = a[i + j + len / 2] * u;
                a[i + j] = p + q;
                a[i + j + len / 2] = p - q;
                u *= w;
            }
        }
    }
}
// The power spectrum of n samples from t0 (Blackman window), averaged over hops.
std::vector<double> spectrum(const std::vector<double>& x, double t0, double t1, size_t n) {
    std::vector<double> power(n / 2, 0);
    size_t start = static_cast<size_t>(t0 * rate), end = static_cast<size_t>(t1 * rate);
    int count = 0;
    for (size_t s = start; s + n <= end && s + n <= x.size(); s += n / 2) {
        std::vector<std::complex<double>> a(n);
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.42 - 0.5 * std::cos(2 * M_PI * i / (n - 1)) + 0.08 * std::cos(4 * M_PI * i / (n - 1));
            a[i] = x[s + i] * w;
        }
        fft(a);
        for (size_t i = 0; i < n / 2; ++i) power[i] += std::norm(a[i]);
        ++count;
    }
    for (double& p : power) p /= count ? count : 1;
    return power;
}
// Spectral flatness (geometric over arithmetic mean) from 200 Hz to 16 kHz.
double flatness(const std::vector<double>& p, size_t n) {
    double logs = 0, sum = 0;
    int k = 0;
    for (size_t i = static_cast<size_t>(200 * n / rate); i < static_cast<size_t>(16000 * n / rate); ++i, ++k) {
        logs += std::log(p[i] + 1e-30);
        sum += p[i];
    }
    return std::exp(logs / k) / (sum / k);
}
// The share of power above 2 kHz.
double high_share(const std::vector<double>& p, size_t n) {
    double hi = 0, all = 0;
    for (size_t i = 1; i < p.size(); ++i) {
        all += p[i];
        if (i * rate / n > 2000) hi += p[i];
    }
    return hi / all;
}

// The beat rate heard: the strongest periodicity of the output's envelope
// (its 5 ms RMS, or `width` samples') between `low` and `high` Hz, by autocorrelation.
double heard_rate(const std::vector<double>& x, double t0, double t1, double low, double high, int width = 220) {
    const size_t a = static_cast<size_t>(t0 * rate), b = static_cast<size_t>(t1 * rate);
    const int hop = 16;   // 2756 Hz envelope
    std::vector<double> env;
    double s = 0;
    for (size_t i = a; i < b; ++i) {
        s += x[i] * x[i];
        if (i >= a + static_cast<size_t>(width)) s -= x[i - width] * x[i - width];
        if ((i - a) % hop == 0) env.push_back(std::sqrt(s > 0 ? s / width : 0));
    }
    double mean = 0;
    for (double e : env) mean += e;
    mean /= env.size();
    for (double& e : env) e -= mean;
    const double er = rate / hop;
    double best = 0, best_lag = 0;
    std::vector<double> ac;
    const int lo = static_cast<int>(er / high), hi = static_cast<int>(er / low) + 1;
    for (int lag = 0; lag <= hi + 1; ++lag) {
        double c = 0;
        for (size_t i = 0; i + lag < env.size(); ++i) c += env[i] * env[i + lag];
        ac.push_back(c / static_cast<double>(env.size() - lag));
    }
    for (int lag = lo; lag <= hi; ++lag) {
        // the first strong peak: a local maximum above 0.3 of the zero lag
        if (ac[lag] > ac[lag - 1] && ac[lag] >= ac[lag + 1] && ac[lag] > 0.3 * ac[0]) {
            const double y0 = ac[lag - 1], y1 = ac[lag], y2 = ac[lag + 1];
            const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
            best_lag = lag + d;
            best = ac[lag];
            break;
        }
    }
    return best > 0 ? er / best_lag : 0;
}
// The share of the time the output's 2 ms envelope is over half its loud
// level (its 95th percentile): how long each beat lasts, as a share of it.
double beat_length(const std::vector<double>& x, double t0, double t1) {
    const size_t a = static_cast<size_t>(t0 * rate), b = static_cast<size_t>(t1 * rate);
    std::vector<double> env;
    double s = 0;
    const int width = 88;
    for (size_t i = a; i < b; ++i) {
        s += x[i] * x[i];
        if (i >= a + width) s -= x[i - width] * x[i - width];
        if (i >= a + width) env.push_back(s);
    }
    std::vector<double> sorted = env;
    std::sort(sorted.begin(), sorted.end());
    const double loud = sorted[sorted.size() * 95 / 100];
    size_t over = 0;
    for (double e : env) over += e > 0.25 * loud;   // half the amplitude: a quarter of the power
    return static_cast<double>(over) / env.size();
}
}

int main() {
    // ---- The Leidenfrost transition. ----
    {
        const Life below = play(boiling, 57, 3);
        const Life above = play(floating, 57, 12);
        check(below.gone > 0 && below.gone < 0.6 * block_rate, "160 C: the drop boils away within 0.6 s", below.gone / block_rate);
        check(below.quiet > 0 && below.quiet < 1.0 * block_rate, "160 C: silent within 1 s", below.quiet / block_rate);
        const size_t n = 2048;
        const std::vector<double> sizzle = spectrum(below.out, 0.02, 0.3, n);
        const std::vector<double> song = spectrum(above.out, 1, 3, n);
        check(flatness(sizzle, n) > 0.15, "160 C: its sound is broadband (spectral flatness)", flatness(sizzle, n));
        check(high_share(sizzle, n) > 0.3, "160 C: a third of its power is over 2 kHz", high_share(sizzle, n));
        check(flatness(song, n) < 0.5 * flatness(sizzle, n), "250 C: the song is tonal, far less flat", flatness(song, n));
        check(above.gone > 5 * block_rate, "250 C: the drop floats for seconds", above.gone / block_rate);
        const double r = heard_rate(above.out, 1.2, 2.2, 8, 120);
        check(r > 15 && r < 30, "250 C: it beats periodically, near water's 23 Hz", r);
        check(db(rms(above.out, 1, 3)) > db(rms(below.out, 1, 3)) + 40, "the floating drop outlives the boiling one",
              db(rms(above.out, 1, 3)), db(rms(below.out, 1, 3)));
        // Across the point the life jumps: 190 C boils, 210 C floats.
        Controls c190 = floating, c210 = floating;
        c190.c[drop::HEAT] = 0.3;
        c210.c[drop::HEAT] = 0.3667;
        const Life l190 = play(c190, 57, 12), l210 = play(c210, 57, 14);
        check(l190.gone < 0.6 * block_rate && l210.gone > 5 * block_rate, "crossing 200 C turns a sizzle into a song",
              l190.gone / block_rate, l210.gone / block_rate);
    }

    // ---- Lobes, beat rate and the chirp, over one floating drop. ----
    {
        const Life life = play(floating, 57, 12);
        bool monotone = true, single = true;
        unsigned seen[9] = {};
        for (size_t b = 1; b < life.lobes.size(); ++b) {
            monotone = monotone && life.lobes[b] <= life.lobes[b - 1];
            single = single && (life.lobes[b] == life.lobes[b - 1] || life.lobes[b] + 1 == life.lobes[b - 1]);
            if (life.lobes[b] <= 8) seen[life.lobes[b]] = 1;
        }
        check(monotone, "the lobes never increase");
        check(single && life.lobes[0] == 8, "they step down one at a time from eight");
        bool all = true;
        for (int n = 0; n <= 8; ++n) all = all && (seen[n] || n == 8);
        check(all, "through every count: 7 ... 2, a sphere (1), gone (0)");

        // Each mode: the rate heard in its middle against the model's, and the
        // drop in rate at each step.
        double previous_end = 0, previous_length = 1, first_length = 0;
        int steps_down = 0, tracked = 0, modes = 0, shorter = 0;
        for (unsigned n = 7; n >= 2; --n) {
            size_t first = 0, last = 0;
            for (size_t b = 0; b < life.lobes.size(); ++b)
                if (life.lobes[b] == n) { if (!first) first = b; last = b; }
            const double t0 = first / block_rate, t1 = last / block_rate;
            if (t1 - t0 < 0.5) continue;
            ++modes;
            const double m0 = t0 + 0.15, m1 = t1 - 0.05;   // past the new star's growth
            double model = 0;
            int k = 0;
            for (size_t b = static_cast<size_t>(m0 * block_rate); b < static_cast<size_t>(m1 * block_rate); ++b, ++k) model += life.model_rate[b];
            model /= k;
            const double heard = heard_rate(life.out, m0, m1, 8, 60);
            const bool close = std::fabs(heard / model - 1) < 0.06;
            tracked += close;
            const double length = beat_length(life.out, m0, m1);
            shorter += length < previous_length;
            if (!first_length) first_length = length;
            previous_length = length;
            std::printf("     %u lobes, %.2f to %.2f s: model %.1f Hz, heard %.1f Hz, beats %.0f%% of the time\n", n, t0, t1, model,
                        heard, 100 * length);
            if (previous_end > 0 && life.model_rate[first + 1] < previous_end) ++steps_down;
            previous_end = life.model_rate[last];
        }
        check(modes >= 5 && tracked == modes, "the beat rate heard follows each mode's within 6%", tracked, modes);
        check(steps_down >= 4, "the rate falls back at each step down", steps_down);
        check(shorter >= modes - 1 && previous_length < 0.75 * first_length, "and the beats shorten with the lobes (fewer vents)", shorter, modes);

        // The sphere's chirp: beats a second over three parts of its last life.
        size_t round = 0;
        for (size_t b = 0; b < life.lobes.size(); ++b) if (life.lobes[b] == 1) { round = b; break; }
        const size_t end = life.gone;
        double counts[3];
        for (int part = 0; part < 3; ++part) {
            const size_t a = round + (end - round) * part / 3, z = round + (end - round) * (part + 1) / 3;
            counts[part] = (life.beats[z] - life.beats[a]) / ((z - a) / block_rate);
        }
        check(round > 0 && counts[1] > counts[0] * 1.2 && counts[2] > counts[1] * 1.5, "the last round drop's beats quicken",
              counts[0], counts[2]);
        check(counts[2] > 4 * counts[0], "a chirp of two octaves or more", counts[2] / counts[0]);
        const double early = heard_rate(life.out, round / block_rate + 0.02, (round + (end - round) / 3) / block_rate, 15, 90);
        check(early > counts[0] * 0.8 && early < counts[0] * 1.25, "and they are heard so", early, counts[0]);
        check(life.quiet > 0 && life.quiet < life.gone + block_rate / 2, "then nothing", life.quiet / block_rate);
    }

    // ---- Slow beats: a bar of as many pulses as lobes, slowing at each step. ----
    {
        const Life life = play(counted, 57, 30);
        int modes = 0, tracked = 0, slower = 0;
        double previous = 1e9;
        for (unsigned n = 7; n >= 2; --n) {
            size_t first = 0, last = 0;
            for (size_t b = 0; b < life.lobes.size(); ++b)
                if (life.lobes[b] == n) { if (!first) first = b; last = b; }
            const double t0 = first / block_rate + 0.15, t1 = last / block_rate - 0.05;
            if (t1 - t0 < 0.9) continue;   // four pulses or more
            ++modes;
            double cycle = 0;
            int k = 0;
            for (size_t b = static_cast<size_t>(t0 * block_rate); b < static_cast<size_t>(t1 * block_rate); ++b, ++k) cycle += life.model_rate[b];
            cycle /= k;
            const double heard = heard_rate(life.out, t0, t1, 1.5, 30, 882);
            tracked += std::fabs(heard / (n * cycle) - 1) < 0.08;
            slower += heard < previous * 0.95;
            previous = heard;
            std::printf("     %u lobes: a beat %.2f a second, pulses heard %.2f a second (%.2f expected)\n", n, cycle, heard, n * cycle);
        }
        check(modes >= 5 && tracked == modes, "slow beats: as many pulses a beat as lobes, heard within 8%", tracked, modes);
        check(slower == modes, "each lobe given up slows the pulse a step", slower, modes);
    }

    // ---- Liquid nitrogen boils on a cold enough plate, floats on a warm one. ----
    {
        Controls cold = boiling, warm = floating;
        cold.c[drop::LIQUID] = warm.c[drop::LIQUID] = 1.0;
        const Life a = play(cold, 57, 2), b = play(warm, 57, 3);
        check(a.gone > 0 && a.gone < 0.6 * block_rate && b.gone > 0.6 * block_rate, "liquid nitrogen at -140 C sizzles; at -50 C it floats",
              a.gone / block_rate, b.gone / block_rate);
    }

    // ---- The tone inside the beats is at the key. ----
    // Each beat's tone starts afresh, so its spectrum is a band about each
    // harmonic, not a line, and its sidebands at the beat rate move the peak
    // about: take each of the first four harmonics' power-weighted centre
    // (within 6% of it), weighted by their power, and check the fundamental's
    // band is among the loudest.
    for (int midi : {45, 57, 69, 76, 84}) {
        const Life life = play(floating, midi, 4);
        const size_t n = 8192;
        const std::vector<double> p = spectrum(life.out, 0.5, 3.8, n);
        const double f0 = key(midi);
        double weight = 0, cents_sum = 0;
        for (int h = 1; h <= 4; ++h) {
            double power = 0, moment = 0;
            for (size_t i = static_cast<size_t>(0.94 * h * f0 * n / rate) + 1; i <= static_cast<size_t>(1.06 * h * f0 * n / rate); ++i) {
                power += p[i];
                moment += p[i] * (i * rate / n);
            }
            cents_sum += power * 1200 * std::log2(moment / power / (h * f0));
            weight += power;
        }
        const double best_cents = cents_sum / weight;
        double top = 0, fundamental = 0;
        for (size_t i = static_cast<size_t>(30 * n / rate); i < p.size(); ++i) top = p[i] > top ? p[i] : top;
        for (size_t i = static_cast<size_t>(0.97 * f0 * n / rate); i <= static_cast<size_t>(1.03 * f0 * n / rate) + 1; ++i)
            fundamental = p[i] > fundamental ? p[i] : fundamental;
        char what[96];
        std::snprintf(what, sizeof what, "MIDI %d: the tone is at the key (cents, fundamental dB)", midi);
        check(std::fabs(best_cents) <= 8 && fundamental > 0.25 * top, what, best_cents, 10 * std::log10(fundamental / top));
    }

    // ---- Level: a single A3 as Hexagon's Wane and Orbit, a chord's peak, the declared peak. ----
    {
        const Life life = play(defaults, 57, 4);
        const double level = db(rms(life.out, 0.5, 3.5));
        check(level > -26.5 && level < -23.5, "the default A3 at -24 to -26 dBFS RMS", level);
        static drop::Voice voices[8];
        static drop::Shared shared;
        opxy::i32 parameters[8];
        for (int k = 0; k < 8; ++k) parameters[k] = q31(defaults.c[k]);
        const int chord[8] = {45, 52, 57, 60, 64, 67, 69, 72};
        shared.sounding = 0;
        for (int v = 0; v < 8; ++v) { drop::initialize(voices[v], shared, v + 1); drop::start(voices[v], shared, v + 1); }
        double peak = 0, when = 0;
        for (int b = 0; b < 3 * 689; ++b) {
            double mix[64] = {};
            for (int v = 0; v < 8; ++v) {
                opxy::u32 pitch[64];
                for (int i = 0; i < 64; ++i) pitch[i] = increment(key(chord[v]));
                opxy::i32 o[64];
                drop::render(voices[v], shared, v + 1, o, parameters, pitch);
                for (int i = 0; i < 64; ++i) mix[i] += o[i] / 2147483648.0;
            }
            for (int i = 0; i < 64; ++i)
                if (std::fabs(mix[i]) > peak) { peak = std::fabs(mix[i]); when = b / block_rate; }
        }
        check(peak > 0.35 && peak < 0.8, "an eight-note chord of equal drops peaks at 0.4 to 0.8 (peak, when)", peak, when);
        // Every corner of the controls stays under the declared peak.
        bool bounded = true;
        for (int corner = 0; corner < 256; corner += 5) {
            Controls c;
            for (int k = 0; k < 8; ++k) c.c[k] = (corner >> k) & 1 ? 1.0 : 0.0;
            c.c[drop::LIFE] = 0.3;
            for (int midi : {24, 60, 108}) bounded = bounded && play(c, midi, 1.5).bounded;
        }
        check(bounded, "every corner of the controls, low to high keys, stays under the declared peak");
    }
    return failures ? 1 : 0;
}
