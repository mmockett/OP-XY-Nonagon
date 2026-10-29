// Butterfly's sound checks, run by sdk.py test:
//  - the tables against a double-precision Jacobi solve of the chain, on the
//    grid and between its points;
//  - at flux p/q the spectrum gathers into q clusters;
//  - below lambda = 1 the strike excites many partials, above it a few;
//  - sweeps of every control, and the wingbeat, have no clicks;
//  - a note sounds in its first block, a pad holds, a struck note dies;
//  - the lowest band sits on the key across the keyboard;
//  - levels beside Moth's, and chords within full scale.
#include "../engine.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "presets.h"

namespace {
using namespace butterfly;
int failures = 0;
bool report = std::getenv("BUTTERFLY_REPORT") != 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }
opxy::u32 increment(double hz) { return static_cast<opxy::u32>(std::llround(hz / 44100.0 * 4294967296.0)); }
u32 turns(double a) { return static_cast<u32>(std::llround(a * 4294967296.0) & 0xffffffffLL); }

// Jacobi's method in double: eigenvalues and eigenvectors (columns of u).
void jacobi(int n, std::vector<double> a, std::vector<double>& e, std::vector<double>& u) {
    u.assign(n * n, 0);
    for (int i = 0; i < n; ++i) u[i * n + i] = 1;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += a[p * n + q] * a[p * n + q];
        if (off < 1e-24) break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (std::fabs(apq) < 1e-30) continue;
                const double th = (a[q * n + q] - a[p * n + p]) / (2 * apq);
                const double t = (th >= 0 ? 1 : -1) / (std::fabs(th) + std::sqrt(th * th + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double x = a[k * n + p], y = a[k * n + q];
                    a[k * n + p] = c * x - s * y;
                    a[k * n + q] = s * x + c * y;
                }
                for (int k = 0; k < n; ++k) {
                    const double x = a[p * n + k], y = a[q * n + k];
                    a[p * n + k] = c * x - s * y;
                    a[q * n + k] = s * x + c * y;
                }
                for (int k = 0; k < n; ++k) {
                    const double x = u[k * n + p], y = u[k * n + q];
                    u[k * n + p] = c * x - s * y;
                    u[k * n + q] = s * x + c * y;
                }
            }
    }
    e.resize(n);
    for (int i = 0; i < n; ++i) e[i] = a[i * n + i];
}
// The chain's energies (rising) and each state's weight at distance d.
void direct(double alpha, double lambda, u32 d, std::vector<double>& energy, std::vector<double>& weight) {
    const int n = partials;
    std::vector<double> a(n * n, 0), e, u;
    for (int i = 0; i < n; ++i) {
        a[i * n + i] = 2 * lambda * std::cos(2 * 3.14159265358979 * alpha * (i - 8));
        if (i + 1 < n) a[i * n + i + 1] = a[(i + 1) * n + i] = 1;
    }
    jacobi(n, a, e, u);
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return e[x] < e[y]; });
    energy.resize(n);
    weight.resize(n);
    for (int k = 0; k < n; ++k) {
        energy[k] = e[order[k]];
        const double x = u[(8 + d) * n + order[k]];
        weight[k] = x * x;
    }
}

// 1. The tables. On the grid they are the solve itself, rounded; between
// its points they are interpolated. Weights are compared as the cumulative
// weight below each energy (a mirror pair's weight sits on one state).
void tables_match() {
    const double fluxes[] = {0.0, 1.0 / 8, 1.0 / 5, 1.0 / 3, 3.0 / 8, 2.0 / 5, 0.5, 0.6, 2.0 / 3, 0.875};
    const double couplings[] = {0.0, 0.5, 0.8, 1.0, 1.25, 1.6, 2.0};
    double worst_e = 0, worst_w = 0;
    for (double alpha : fluxes)
        for (double lambda : couplings)
            for (u32 d = 0; d < half_chain; d += 2) {
                Spectrum s;
                spectrum(turns(alpha), static_cast<i32>(std::lround(lambda * 4096)), static_cast<i32>(d << 16), s);
                std::vector<double> e, w;
                direct(alpha, lambda, d, e, w);
                double ce = 0, cw = 0;
                for (u32 k = 0; k < partials; ++k) {
                    worst_e = std::max(worst_e, std::fabs(s.energy[k] / 4096.0 - e[k]));
                    ce += w[k];
                    cw += s.weight[k] / 65536.0;
                    if (k + 1 == partials || e[k + 1] - e[k] > 1e-3) worst_w = std::max(worst_w, std::fabs(ce - cw));
                }
            }
    // Between the grid's points: the interpolated spectrum against the solve.
    double mid_e = 0, mid_w = 0, sum_e = 0, sum_w = 0;
    long weights = 0;
    long count = 0;
    for (int i = 0; i < 200; ++i) {
        const double alpha = 0.013 + 0.4871 * ((i * 37) % 200) / 200.0, lambda = 2.0 * ((i * 61) % 200) / 200.0;
        Spectrum s;
        spectrum(turns(alpha), static_cast<i32>(std::lround(lambda * 4096)), 3 << 16, s);
        std::vector<double> e, w;
        direct(alpha, lambda, 3, e, w);
        double ce = 0, cw = 0;
        for (u32 k = 0; k < partials; ++k) {
            const double de = std::fabs(s.energy[k] / 4096.0 - e[k]);
            mid_e = std::max(mid_e, de);
            sum_e += de * de;
            ++count;
            ce += w[k];
            cw += s.weight[k] / 65536.0;
            if (k + 1 == partials || e[k + 1] - e[k] > 0.05) {
                mid_w = std::max(mid_w, std::fabs(ce - cw));
                sum_w += (ce - cw) * (ce - cw);
                ++weights;
            }
        }
    }
    std::printf("      on the grid: energies within %.1e, cumulative weight within %.3f; between it: energies within %.3f "
                "(rms %.4f), cumulative weight within %.3f (rms %.3f)\n", worst_e, worst_w, mid_e, std::sqrt(sum_e / count), mid_w,
                std::sqrt(sum_w / weights));
    check(worst_e < 1e-3 && worst_w < 0.02, "on the grid the tables are the double-precision solve (to Q12 and Q8, mirror pairs merged)");
    check(mid_e < 0.35 && std::sqrt(sum_e / count) < 0.04 && std::sqrt(sum_w / weights) < 0.06,
          "between the grid's points, energies within 0.35 (rms 0.04, of a span of 8) and weights rms 0.06");
}

// 2. At alpha = p/q (snap lands exactly there) the 17 levels form q runs,
// gaps over 2.5 median spacings, a run of one an edge state.
void clusters() {
    struct Case { u32 p, q; } cases[] = {{1, 2}, {1, 3}, {2, 3}, {1, 5}, {2, 5}, {3, 7}};
    bool all = true;
    for (const Case& c : cases) {
        u32 alpha = 0;
        for (u32 f = 0; f < tables::fraction_count; ++f)
            if (tables::numerator[f] == c.p && tables::denominator[f] == c.q) alpha = tables::fraction[f];
        Spectrum s;
        spectrum(alpha, 4096, 0, s);
        std::vector<double> gaps;
        for (u32 k = 1; k < partials; ++k) gaps.push_back((s.energy[k] - s.energy[k - 1]) / 4096.0);
        std::vector<double> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        u32 runs = 0, length = 1;
        for (u32 k = 1; k <= partials; ++k) {
            if (k == partials || gaps[k - 1] > 2.5 * median) {
                if (length >= 2) ++runs;
                length = 1;
            } else {
                ++length;
            }
        }
        if (report) std::printf("      %u/%u at lambda 1: %u clusters\n", c.p, c.q, runs);
        if (runs != c.q) all = false;
    }
    check(all, "at p/q (q = 2, 3, 5, 7) the 17 partials fall into q clusters");
}

// 3. Participation: how many partials the strike excites, over fluxes and sites.
double participation(double lambda) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < 12; ++i)
        for (u32 d = 0; d < half_chain; d += 2) {
            Spectrum s;
            Notes notes;
            spectrum(turns(0.3 + 0.015 * i), static_cast<i32>(std::lround(lambda * 4096)), static_cast<i32>(d << 16), s);
            shape(s, static_cast<i32>(std::lround(lambda * 4096)), notes);
            sum += notes.participation / 256.0;
            ++n;
        }
    return sum / n;
}
void localization() {
    const double lambdas[] = {0.5, 0.8, 1.0, 1.25, 1.6, 2.0};
    double pr[6];
    std::printf("      participation over fluxes 0.30 to 0.47 and five sites:");
    for (u32 i = 0; i < 6; ++i) {
        pr[i] = participation(lambdas[i]);
        std::printf(" %.1f", pr[i]);
    }
    std::printf(" (lambda 0.5, 0.8, 1, 1.25, 1.6, 2)\n");
    check(pr[0] > 6 && pr[4] < 3.2, "below lambda = 1 the strike excites over 6 of 17 partials, at 1.6 and over under 3.2");
    check(pr[1] / pr[4] > 2, "participation falls over 2 times from lambda 0.8 to 1.6");
}

// Voices rendered from controls that may change each block.
struct Render {
    std::vector<double> out;
    double peak = 0;   // of one voice
};
template <class Controls>
Render play(const double* hz, u32 voices, double seconds, Controls controls, const double* starts = 0) {
    static Voice v[8];
    static Shared shared;
    Render r;
    opxy::i32 p[8];
    opxy::u32 pitch[8][opxy::block_frames];
    for (u32 j = 0; j < voices; ++j) {
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[j][i] = increment(hz[j]);
        initialize(v[j], shared, j + 1);
    }
    bool on[8] = {};
    const u32 blocks = static_cast<u32>(seconds * 44100 / 64);
    for (u32 b = 0; b < blocks; ++b) {
        const double t = b * 64 / 44100.0;
        for (u32 j = 0; j < voices; ++j)
            if (!on[j] && t >= (starts ? starts[j] : 0)) {
                start(v[j], shared, j + 1);
                on[j] = true;
            }
        double c[8];
        for (u32 i = 0; i < 8; ++i) c[i] = presets[0].c[i];
        controls(t, c);
        for (u32 i = 0; i < 8; ++i) p[i] = q31(c[i]);
        double sum[opxy::block_frames] = {};
        for (u32 j = 0; j < voices; ++j) {
            if (!on[j]) continue;
            opxy::i32 o[opxy::block_frames];
            render(v[j], shared, j + 1, o, p, pitch[j]);
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                const double x = o[i] / 2147483648.0;
                r.peak = std::max(r.peak, std::fabs(x));
                sum[i] += x;
            }
        }
        for (u32 i = 0; i < opxy::block_frames; ++i) r.out.push_back(sum[i]);
    }
    return r;
}
double rms_db(const std::vector<double>& x, size_t from, size_t to) {
    double s = 0;
    to = std::min(to, x.size());
    for (size_t i = from; i < to; ++i) s += x[i] * x[i];
    return 10 * std::log10(s / (to - from) + 1e-30);
}

// A click is broadband and local: each 256-sample window's peak third
// difference (a steep high-pass) against the median of its eight
// neighbours'; the worst such ratio.
double clickiness(const std::vector<double>& x, size_t from) {
    std::vector<double> peaks;
    for (size_t w = from + 3; w + 256 <= x.size(); w += 256) {
        double m = 0;
        for (size_t i = w; i < w + 256; ++i) m = std::max(m, std::fabs(x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3]));
        peaks.push_back(m);
    }
    double worst = 0;
    for (size_t i = 4; i + 4 < peaks.size(); ++i) {
        double around[8];
        u32 n = 0;
        for (size_t j = i - 4; j <= i + 4; ++j)
            if (j != i) around[n++] = peaks[j];
        std::sort(around, around + 8);
        worst = std::max(worst, peaks[i] / ((around[3] + around[4]) / 2 + 1e-12));
    }
    return worst;
}
// The sweeps under a held pad (strike 0), partials below 450 Hz.
void sweeps() {
    const double hz[1] = {110};
    auto pad = [](double, double* c) {
        c[STRIKE] = 0;
        c[SPREAD] = 0.75;
        c[WINGBEAT] = 0;
    };
    const double still = clickiness(play(hz, 1, 3, pad).out, 8820);
    std::vector<double> stepped = play(hz, 1, 3, pad).out;
    for (size_t i = stepped.size() / 2; i < stepped.size(); ++i) stepped[i] += 0.003;
    const double seen = clickiness(stepped, 8820);
    std::printf("      the detector on a held pad %.1f, with a 0.003 step added %.1f\n", still, seen);
    check(seen > 3, "the click detector sees a step of 0.003 in a held pad");
    struct Sweep { const char* name; int control; double from, to, seconds; } list[] = {
        {"flux 0.30 to 0.45 in 4 s", FLUX, 0.60, 0.90, 4}, {"flux 0.05 to 0.45 in 3 s (snapping)", FLUX, 0.1, 0.9, 3},
        {"coupling 0.3 to 0.8 (lambda 0.6 to 1.6) in 3 s", COUPLING, 0.3, 0.8, 3},
        {"site middle to end in 3 s", SITE, 0, 1, 3}, {"spread 3 to 48 semitones in 3 s", SPREAD, 0, 1, 3},
        {"tilt 0 to 3 in 3 s", TILT, 0, 1, 3}, {"wingbeat at full for 4 s", WINGBEAT, 1, 1, 4}};
    bool all = true;
    for (const Sweep& s : list) {
        auto controls = [&](double t, double* c) {
            pad(t, c);
            if (s.control == SPREAD) c[SPREAD] = 0.0;
            const double u = std::min(1.0, std::max(0.0, (t - 0.5) / s.seconds));
            c[s.control] = s.from + (s.to - s.from) * u;
        };
        const double k = clickiness(play(hz, 1, s.seconds + 1, controls).out, 8820);
        std::printf("      %s: %.1f (held pad %.1f)\n", s.name, k, still);
        if (k > std::max(2.0, still * 1.5)) all = false;
    }
    check(all, "sweeps of flux, coupling, site, spread and tilt, and the wingbeat, add no clicks to a held pad");
}

// A note sounds in its first block; a pad holds while held; a struck note dies.
void envelope() {
    const double a3[1] = {220};
    bool first = false;
    {
        static Voice v;
        static Shared shared;
        initialize(v, shared, 1);
        start(v, shared, 1);
        opxy::i32 p[8], o[opxy::block_frames];
        for (u32 i = 0; i < 8; ++i) p[i] = q31(presets[0].c[i]);
        opxy::u32 pitch[opxy::block_frames];
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(220);
        render(v, shared, 1, o, p, pitch);
        for (u32 i = 0; i < opxy::block_frames; ++i) first = first || o[i];
    }
    const Render pad = play(a3, 1, 5, [](double, double* c) {
        for (u32 i = 0; i < 8; ++i) c[i] = presets[3].c[i];
    });
    const double one = rms_db(pad.out, 44100, 88200), four = rms_db(pad.out, 176400, 220500);
    const Render bell = play(a3, 1, 5, [](double, double* c) { c[STRIKE] = 1; });
    const double ring = rms_db(bell.out, 0, 22050), late = rms_db(bell.out, 176400, 220500);
    std::printf("      the first block sounds: %s; Shimmer at 1 s %.1f dBFS, at 4 s %.1f; Specimen struck at 100, its first half second %.1f, its "
                "fifth second %.1f\n", first ? "yes" : "no", one, four, ring, late);
    check(first, "a note sounds in its first block (no wait for a spectrum)");
    check(std::fabs(one - four) < 2.5, "a pad (strike 0) holds within 2.5 dB while the note is held");
    check(ring - late > 15, "a struck note rings down by its glow");
}

// The lowest band sits on the key. A band is a cluster of partials about a
// semitone wide, heard near its amplitude-weighted middle: the spectrum's
// centroid (by magnitude) across the band (4% of the span either side of
// the key: 96 cents at the default 24 semitones), against the key,
// across the keyboard; also its strongest peak there, and how loud the band
// is beside the loudest peak anywhere.
double magnitude_at(const std::vector<double>& x, double f, size_t from, size_t n) {
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * 3.14159265 * i / (n - 1));
        re += x[from + i] * w * std::cos(2 * 3.14159265 * f * i / 44100);
        im += x[from + i] * w * std::sin(2 * 3.14159265 * f * i / 44100);
    }
    return std::sqrt(re * re + im * im);
}
void tuning() {
    double worst = 0, worst_peak = 0, quietest = 0;
    const int notes[] = {36, 48, 60, 72, 84};
    const size_t n = 32768;
    for (const Preset& pr : {presets[0], presets[2], presets[4], presets[6]}) {
        for (int midi : notes) {
            const double hz[1] = {440 * std::pow(2.0, (midi - 69) / 12.0)};
            const Render r = play(hz, 1, 1, [&](double, double* c) {
                for (u32 i = 0; i < 8; ++i) c[i] = pr.c[i];
                c[WINGBEAT] = 0;
                c[STRIKE] = 0;   // held, so the window sees a steady spectrum
            });
            double best = 0, at = hz[0], loudest = 0, moment = 0, total = 0;
            const double span = 3 * std::pow(16.0, pr.c[SPREAD]) * 100, reach = 0.04 * span;   // the band: 7% of the span, about the key
            for (double cents = -reach; cents <= reach; cents += 0.5) {
                const double f = hz[0] * std::pow(2.0, cents / 1200);
                const double v = magnitude_at(r.out, f, 4410, n);
                moment += v * cents;
                total += v;
                if (v > best) {
                    best = v;
                    at = cents;
                }
            }
            for (double f = hz[0] * 0.9; f <= std::min(16000.0, hz[0] * 18); f *= 1.003) loudest = std::max(loudest, magnitude_at(r.out, f, 4410, n));
            const double centre = moment / total, db = 20 * std::log10(best / loudest);
            worst = std::max(worst, std::fabs(centre));
            worst_peak = std::max(worst_peak, std::fabs(at));
            quietest = std::min(quietest, db);
            if (report)
                std::printf("      %s, MIDI %d: the band's middle %+.1f cents, its strongest peak %+.1f, %.1f dB under the loudest\n",
                            pr.name, midi, centre, at, -db);
        }
    }
    std::printf("      the lowest band, Specimen, Gamelan, Struck Bass and Chime, C2 to C6: its middle within %.1f cents of the key, "
                "its strongest partial within %.1f, at most %.1f dB under the loudest peak\n", worst, worst_peak, -quietest);
    check(worst < 20 && quietest > -20, "the lowest band sounds on the key (its middle within 20 cents; the window resolves 36 at C2) across the keyboard, within 20 dB of the loudest");
}

void levels() {
    const double a3[1] = {220};
    const Render bell = play(a3, 1, 1, [](double, double*) {});
    const Render pad = play(a3, 1, 2, [](double, double* c) {
        for (u32 i = 0; i < 8; ++i) c[i] = presets[3].c[i];
    });
    const double struck = rms_db(bell.out, 0, 44100), held = rms_db(pad.out, 44100, 88200);
    std::printf("      A3: Specimen %.1f dBFS RMS over its first second (Moth's Bell -27.8), Shimmer %.1f held (Lantern -23.3)\n",
                struck, held);
    check(struck > -30.5 && struck < -25 && held > -26 && held < -21, "single notes sit beside Moth's Bell and Lantern");
    const double chord[8] = {130.8, 164.8, 196.0, 246.9, 261.6, 329.6, 392.0, 493.9};
    bool ok = true;
    for (const Preset& pr : presets) {
        const Render r = play(chord, 8, 3, [&](double, double* c) {
            for (u32 i = 0; i < 8; ++i) c[i] = pr.c[i];
        });
        double peak = 0;
        for (double x : r.out) peak = std::max(peak, std::fabs(x));
        std::printf("      chord of 8, %s: peak %.2f of full scale, a voice at most %.2f\n", pr.name, peak, r.peak);
        if (peak > 0.85 || peak < 0.3 || r.peak > 0.5) ok = false;
    }
    check(ok, "eight-note chords on every preset peak within 0.3-0.85 of full scale, each voice under half");
}

// A ringing voice struck again fades out in one block first: no click.
void retrigger() {
    const double hz[1] = {220};
    static Voice v;
    static Shared shared;
    opxy::i32 p[8], o[opxy::block_frames];
    for (u32 i = 0; i < 8; ++i) p[i] = q31(presets[3].c[i]);   // a pad: still loud when struck again
    opxy::u32 pitch[opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(hz[0]);
    initialize(v, shared, 1);
    start(v, shared, 1);
    double last = 0, worst = 0, typical = 0;
    for (u32 blk = 0; blk < 600; ++blk) {
        if (blk == 400) start(v, shared, 1);
        render(v, shared, 1, o, p, pitch);
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            const double x = o[i] / 2147483648.0, d = std::fabs(x - last);
            if (blk >= 399 && blk <= 403) worst = std::max(worst, d);
            else if (blk > 200) typical = std::max(typical, d);
            last = x;
        }
    }
    std::printf("      retrigger: largest step %.4f around it, %.4f elsewhere\n", worst, typical);
    check(worst <= typical * 1.2, "striking a sounding voice again does not click");
}

// 9. Each control at 0, 25, 50, 75 and 100 on one note of 220 Hz, the others
// at Specimen's (wingbeat 0 except in its own row): the README's table.
// Model figures come from the engine's own spectrum and levels; the rest
// are measured in the rendered audio.
struct Model { double hz[partials], level[partials]; double participation; };
Model model(const double* c) {
    Model m;
    const u32 alpha = warp(flux(q31(c[FLUX])), snap_pull);
    const i32 lambda = coupling(q31(c[COUPLING]));
    Spectrum s;
    Notes notes;
    spectrum(alpha, lambda, site(q31(c[SITE])), s);
    shape(s, lambda, notes);
    const double octaves = std::pow(2.0, spread_log2(q31(c[SPREAD])) / 65536.0);
    for (u32 k = 0; k < partials; ++k) {
        m.hz[k] = 220 * std::pow(2.0, octaves * notes.place[k] / 65536.0);
        m.level[k] = notes.level[k] / 32768.0;
    }
    m.participation = notes.participation / 256.0;
    return m;
}
// Bands: runs of the 17 energies split by gaps over 2.5 median spacings, as clusters() counts them.
u32 bands(const double* c) {
    Spectrum s;
    spectrum(warp(flux(q31(c[FLUX])), snap_pull), coupling(q31(c[COUPLING])), 0, s);
    std::vector<double> gaps, sorted;
    for (u32 k = 1; k < partials; ++k) gaps.push_back((s.energy[k] - s.energy[k - 1]) / 4096.0);
    sorted = gaps;
    std::sort(sorted.begin(), sorted.end());
    const double median = std::max(sorted[sorted.size() / 2], 1e-3);
    u32 runs = 0, length = 1;
    for (u32 k = 1; k <= partials; ++k) {
        if (k == partials || gaps[k - 1] > 2.5 * median) {
            if (length >= 2) ++runs;
            length = 1;
        } else {
            ++length;
        }
    }
    return runs;
}
// The audio's centroid over the partials, as a multiple of the note: each
// partial's magnitude at its own frequency, in a window from `at` seconds.
double centroid(const std::vector<double>& x, const Model& m, double at) {
    double sum = 0, moment = 0;
    for (u32 k = 0; k < partials; ++k) {
        if (m.hz[k] > 8000) continue;
        const double a = magnitude_at(x, m.hz[k], static_cast<size_t>(at * 44100), 4096);
        sum += a;
        moment += a * m.hz[k];
    }
    return sum > 0 ? moment / sum / 220 : 0;
}
void controls_table() {
    const double a3[1] = {220};
    const char* names[8] = {"flux", "coupling", "spread", "strike", "glow", "site", "tilt", "wingbeat"};
    double rows[12][5];
    const char* labels[12];
    u32 row = 0;
    auto with = [](u32 control, double v, double* c) {
        for (u32 i = 0; i < 8; ++i) c[i] = presets[0].c[i];
        c[WINGBEAT] = 0;
        c[control] = v;
    };
    bool ok = true;
    // flux: bands at lambda 1, and partials within 20 dB of the loudest
    labels[row] = "flux: bands (lambda 1)";
    for (u32 v = 0; v < 5; ++v) {
        double c[8];
        with(FLUX, v / 4.0, c);
        rows[row][v] = bands(c);
    }
    ++row;
    // coupling: how many partials a strike excites
    labels[row] = "coupling: partials the strike excites";
    for (u32 v = 0; v < 5; ++v) {
        double c[8];
        with(COUPLING, v / 4.0, c);
        rows[row][v] = model(c).participation;
    }
    ok = ok && rows[row][0] > rows[row][4] * 2;
    ++row;
    // spread: the highest partial above the note
    labels[row] = "spread: highest partial, semitones above";
    for (u32 v = 0; v < 5; ++v) {
        double c[8];
        with(SPREAD, v / 4.0, c);
        const Model m = model(c);
        double top = 0;
        for (u32 k = 0; k < partials; ++k)
            if (m.level[k] > 0.01) top = std::max(top, 12 * std::log2(m.hz[k] / 220));
        rows[row][v] = top;
    }
    ok = ok && rows[row][4] > rows[row][0] * 4;
    ++row;
    // strike: the level held at 2 s, against an unstruck note's
    labels[row] = "strike: level at 2 s against strike 0, dB";
    double held0 = 0;
    for (u32 v = 0; v < 5; ++v) {
        const double value = v / 4.0;
        const Render r = play(a3, 1, 2.1, [&](double, double* c) { with(STRIKE, value, c); });
        const double l = rms_db(r.out, 88200 - 2205, 88200 + 2205);
        if (!v) held0 = l;
        rows[row][v] = l - held0;
    }
    ok = ok && rows[row][1] < 0 && rows[row][4] < rows[row][3] && rows[row][3] < rows[row][2];
    ++row;
    // glow: a pluck's fall to -60 dB, against the caption
    labels[row] = "glow: a pluck's fall to -60 dB (tilt 0), s";
    u32 caption_row = row;
    for (u32 v = 0; v < 5; ++v) {
        const double value = v / 4.0;
        const double seconds = 0.04 * std::pow(200.0, value) * 1.6 + 0.3;
        const Render r = play(a3, 1, seconds, [&](double, double* c) {
            with(GLOW, value, c);
            c[STRIKE] = 1;
            c[TILT] = 0;
        });
        double peak = -300, fall = seconds;
        for (size_t w = 0; w + 441 <= r.out.size(); w += 441) {
            const double l = rms_db(r.out, w, w + 441);
            peak = std::max(peak, l);
            if (l < peak - 60) {
                fall = w / 44100.0;
                break;
            }
        }
        rows[row][v] = fall;
    }
    ++row;
    // site: the audio's centroid, and partials within 20 dB
    labels[row] = "site: centroid, x the note";
    for (u32 v = 0; v < 5; ++v) {
        const double value = v / 4.0;
        double c[8];
        with(SITE, value, c);
        const Render r = play(a3, 1, 0.3, [&](double, double* cc) { with(SITE, value, cc); });
        rows[row][v] = centroid(r.out, model(c), 0.05);
    }
    ++row;
    labels[row] = "site: partials within 20 dB of the loudest";
    for (u32 v = 0; v < 5; ++v) {
        double c[8];
        with(SITE, v / 4.0, c);
        const Model m = model(c);
        double loudest = 0;
        for (u32 k = 0; k < partials; ++k) loudest = std::max(loudest, m.level[k]);
        u32 n = 0;
        for (u32 k = 0; k < partials; ++k) n += m.level[k] > loudest * 0.1;
        rows[row][v] = n;
    }
    ++row;
    // tilt: the centroid 1 s into a pluck, against at its strike
    labels[row] = "tilt: centroid at 1 s against at the strike";
    for (u32 v = 0; v < 5; ++v) {
        const double value = v / 4.0;
        double c[8];
        with(TILT, value, c);
        const Render r = play(a3, 1, 1.2, [&](double, double* cc) { with(TILT, value, cc); });
        const Model m = model(c);
        rows[row][v] = centroid(r.out, m, 1.0) / centroid(r.out, m, 0.01);
    }
    ok = ok && rows[row][4] < rows[row][0] - 0.05;
    ++row;
    // wingbeat: how far it moves the partials (each, by rank, from the beat's
    // one extreme to the other, weighted by level) and how fast it beats
    labels[row] = "wingbeat: the partials' glide, cents";
    for (u32 v = 0; v < 5; ++v) {
        double c[8];
        with(WINGBEAT, v / 4.0, c);
        const i32 w = q31(c[WINGBEAT]);
        Model ends[2];
        for (u32 e = 0; e < 2; ++e) {
            const u32 beat = e ? 0xc0000000u : 0x40000000u;   // a quarter and three quarters of a beat
            const u32 alpha = warp(flux(q31(c[FLUX])), snap_pull) + static_cast<u32>(wingbeat_flux(w, beat));
            const i32 lambda = coupling(q31(c[COUPLING]));
            Spectrum sp;
            Notes notes;
            spectrum(alpha, lambda, clamp(site(q31(c[SITE])) + wingbeat_site(w, beat), 0, 8 << 16), sp);
            shape(sp, lambda, notes);
            const double octaves = std::pow(2.0, spread_log2(q31(c[SPREAD])) / 65536.0);
            for (u32 k = 0; k < partials; ++k) ends[e].hz[k] = 220 * std::pow(2.0, octaves * notes.place[k] / 65536.0);
        }
        const Model m = model(c);
        double glide = 0, weight = 0;
        for (u32 k = 0; k < partials; ++k) {
            glide += std::fabs(1200 * std::log2(ends[1].hz[k] / ends[0].hz[k])) * m.level[k];
            weight += m.level[k];
        }
        rows[row][v] = glide / weight;
    }
    ok = ok && rows[row][0] < 0.5 && rows[row][4] > 30 && rows[row][2] > rows[row][1];
    ++row;
    labels[row] = "wingbeat: beats a second";
    for (u32 v = 0; v < 5; ++v) rows[row][v] = wingbeat_rate(q31(v / 4.0)) / 4294967296.0 * 44100 / 64;
    ++row;
    std::printf("      %-46s %7s %7s %7s %7s %7s\n", "control (others at Specimen's)", "0", "25", "50", "75", "100");
    for (u32 i = 0; i < row; ++i) {
        std::printf("      %-46s", labels[i]);
        for (u32 v = 0; v < 5; ++v) std::printf(" %7.2f", rows[i][v]);
        std::printf("\n");
    }
    double worst = 1;
    for (u32 v = 0; v < 5; ++v) {
        const double caption = 0.04 * std::pow(200.0, v / 4.0);
        worst = std::max(worst, std::max(rows[caption_row][v] / caption, caption / rows[caption_row][v]));
    }
    std::printf("      glow: the fall within %.2f times the caption\n", worst);
    (void)names;
    check(ok, "each control's measure moves the way it should from 0 to 100");
    check(worst < 1.5, "a pluck falls 60 dB within half again of glow's caption");
}
}

int main() {
    tables_match();
    clusters();
    localization();
    sweeps();
    envelope();
    tuning();
    levels();
    retrigger();
    controls_table();
    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
