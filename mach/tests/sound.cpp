// Mach's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. The host may use floating point: it
// is the reference the fixed-point engine is checked against.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

const double rate = 44100;
const double q26 = 67108864.0;

opxy::i32 q31(double v) { return static_cast<opxy::i32>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 2147483647.0)); }
double mach_control(double m) { return m <= 1 ? (m - 0.3) / 1.4 : 0.5 + (m - 1) / 4; }
double pass_control(double metres) { return std::log2(metres / 3) / std::log2(100.0); }
double dur_control(double seconds) { return std::log2(seconds / 0.5) / 4; }
double source_control(int s) { return (s + 0.5) / 5; }

struct Flight {
    double mach = 0.5, pass = 20, dur = 2, boom = 0.7, air = 0, tune = 0, echo = 0;
    int source = mach::ORGAN;
    double hz = 220;
    double strike = 0;   // the bell's strike period in seconds, overriding the engine's (0: its own)
};

// One note: its output, and the voice after each block for the checks that
// look at its roots.
struct Take {
    std::vector<double> out;          // full scale 1
    std::vector<mach::Voice> blocks;  // the voice after each block
};

Take play(const Flight& f, double seconds) {
    static mach::Voice voice;
    static mach::Shared shared;
    opxy::i32 parameters[8] = {q31(mach_control(f.mach)), q31(pass_control(f.pass)), q31(source_control(f.source)),
                               q31(dur_control(f.dur)), q31(f.boom), q31(f.air), q31(f.tune / 2 + 0.5), q31(f.echo)};
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = static_cast<opxy::u32>(std::lround(f.hz / rate * 4294967296.0));
    opxy::i32 output[opxy::block_frames];
    mach::initialize(voice, shared, 1);
    mach::start(voice, shared, 1);
    Take take;
    const int blocks = static_cast<int>(seconds * rate / opxy::block_frames);
    for (int b = 0; b < blocks; ++b) {
        mach::render(voice, shared, 1, output, parameters, pitch);
        if (b == 0 && f.strike > 0) {
            // Move the bell's strikes (set up in the first block, used from the
            // first sound on) so a check can look at its pure ring.
            voice.period = static_cast<opxy::i32>(4294967296.0 / (f.strike * 65536));
            voice.period_samples = static_cast<opxy::u32>(f.strike * rate);
        }
        for (unsigned i = 0; i < opxy::block_frames; ++i) take.out.push_back(output[i] / 2147483648.0);
        take.blocks.push_back(voice);
    }
    mach::destroy(voice, shared, 1);
    return take;
}

// The exact retarded emission time for listener time t (straight sound):
// t = sigma + sqrt((M sigma)^2 + d^2), on the forward (+1) or reversed (-1) branch.
double exact_root(double m, double d, double t, int branch) {
    const double a = 1 - m * m, disc = m * m * t * t + d * d * a;
    if (disc < 0) return NAN;
    if (std::fabs(a) < 1e-9) return (t * t - d * d) / (2 * t);
    const double fwd = (t * t - d * d) / (t + std::sqrt(disc));
    if (branch > 0) return fwd;
    return (t + std::sqrt(disc)) / a;   // m > 1: the earlier emission
}

double peak(const std::vector<double>& x, size_t from, size_t to) {
    double p = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) p = std::fmax(p, std::fabs(x[i]));
    return p;
}

// The strength of a frequency in a window (a single DFT bin, Hann window).
double strength(const std::vector<double>& x, size_t from, size_t n, double hz) {
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
        const double ph = 2 * M_PI * hz * i / rate;
        re += w * x[from + i] * std::cos(ph);
        im += w * x[from + i] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im) / n;
}

// A click is broadband: it puts energy above 21 kHz, where the engine never
// sounds a partial (each fades out by 0.45 of the sample rate: 19.8 kHz).
// The most energy there in any 1024-sample Hann window from..to, and (with
// high false) the energy of the whole band.
double band_energy(const std::vector<double>& x, size_t at, bool high) {
    const size_t n = 1024;
    std::vector<double> w(n);
    double total = 0;
    for (size_t i = 0; i < n; ++i) {
        w[i] = x[at + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / n));
        total += w[i] * w[i];
    }
    if (!high) return total;
    double e = 0;
    for (size_t k = static_cast<size_t>(21000.0 / rate * n); k <= n / 2; ++k) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            re += w[i] * std::cos(2 * M_PI * k * i / n);
            im += w[i] * std::sin(2 * M_PI * k * i / n);
        }
        e += (re * re + im * im) * (k == n / 2 ? 1 : 2) / n;
    }
    return e;
}
// The worst share of a window's energy above 21 kHz, over windows from..to,
// a quiet window's measured against `least` (where a click would stand out).
double click_share(const std::vector<double>& x, size_t from, size_t to, double least) {
    double worst = 0;
    for (size_t at = from; at + 1024 <= to && at + 1024 <= x.size(); at += 128) {
        const double total = band_energy(x, at, false);
        worst = std::fmax(worst, band_energy(x, at, true) / std::fmax(total, least));
    }
    return worst;
}
}

int main() {
    const double declared = 1073741824.0 / 2147483648.0;

    // 1. Subsonic: one root, a single glide at f / (1 - M cos theta).
    {
        Flight f;
        f.mach = 0.5; f.pass = 20; f.dur = 3; f.hz = 220; f.source = mach::ORGAN;
        const Take take = play(f, 3.5);
        const mach::Voice& first = take.blocks.front();
        const double m = first.mach / 268435456.0, d = first.image[0].d / q26;
        const double hz = f.hz * first.pitch / 65536.0;   // the source's own pitch, tune's factor on the key
        bool single = true, exact = true;
        double worst = 0;
        for (const mach::Voice& v : take.blocks) {
            single = single && v.root[1].state == mach::DONE && v.root[2].state == mach::DONE && v.root[3].state == mach::DONE;
            if (v.root[0].state == mach::TRACKING) {
                const double t = v.t / q26, want = exact_root(m, d, t, 1);
                const double err = std::fabs(v.root[0].sigma / q26 - want);
                worst = std::fmax(worst, err);
                exact = exact && err < 2e-7;
            }
        }
        check(single, "subsonic: one root only");
        std::printf("      worst root error %.1f ns\n", worst * 1e9);
        check(exact, "subsonic: the root matches the exact retarded time within 200 ns");
        // The rate the source's time is heard at, against 1 / (1 - M cos theta)
        // for the emission heard, at five moments of the pass (approach, near,
        // closest, past, away). Then the sound itself: mixed down against the
        // exact phase 2 pi f sigma(t) of the physics, its fundamental must hold
        // still in phase across 46 ms, so the heard pitch is f / (1 - M cos theta)
        // throughout the window, however fast it glides there.
        const double t0 = first.t_start / q26;
        bool glide = true, falling = true, rate_ok = true;
        double last = 1e9;
        for (double at : {0.35, 0.75, 1.0, 1.25, 1.9}) {
            const size_t block = static_cast<size_t>(at * rate / 64);
            const mach::Voice& v = take.blocks[block];
            const double sigma = v.root[0].sigma / q26, x = m * sigma, r = std::sqrt(x * x + d * d);
            const double cos_theta = -x / r;   // between the flight and the line to the listener
            const double want = hz / (1 - m * cos_theta);
            // The engine's rate over its last sub-step, at the middle of that sub-step.
            const double mid = v.root[0].sigma / q26 - v.root[0].step / q26 / 2, xm = m * mid;
            const double rate_want = 1 / (1 - m * -xm / std::sqrt(xm * xm + d * d));
            const double rate_got = v.root[0].step / double(mach::substep_time);
            rate_ok = rate_ok && std::fabs(rate_got / rate_want - 1) < 1e-3;
            // Two halves of a 2048-sample window, each mixed against the exact phase.
            const size_t from = (block + 1) * 64 - 1024, half = 1024;
            double re[2] = {0, 0}, im[2] = {0, 0};
            for (int hh = 0; hh < 2; ++hh)
                for (size_t i = 0; i < half; ++i) {
                    const size_t n = from + hh * half + i;
                    const double t = t0 + n / rate;
                    const double ph = 2 * M_PI * hz * exact_root(m, d, t, 1);
                    const double w = (0.5 - 0.5 * std::cos(2 * M_PI * i / half)) * take.out[n];
                    re[hh] += w * std::cos(ph);
                    im[hh] -= w * std::sin(ph);
                }
            const double drift = std::atan2(im[1] * re[0] - re[1] * im[0], re[1] * re[0] + im[1] * im[0]);
            const double off = drift / (2 * M_PI) / (half / rate);   // Hz from the exact pitch, over the window
            std::printf("      t %.2f s: f/(1 - M cos theta) %.2f Hz; heard pitch %+.3f Hz from the exact pass\n", at, want, off);
            glide = glide && std::fabs(off) < 0.002 * want;
            falling = falling && want < last;
            last = want;
        }
        check(rate_ok, "subsonic: dtau/dt within 0.1% of 1 / (1 - M cos theta)");
        check(glide, "subsonic: heard pitch follows f / (1 - M cos theta) within 0.2%");
        check(falling, "subsonic: a single glide, only falling");
        check(peak(take.out, 0, take.out.size()) <= declared, "subsonic: within the declared peak");
    }

    // 1b. Tune: at -1 the source is pitched so the flight's start sounds the
    //     key exactly (above Mach 1, where the backward image ends), at 1 so
    //     its end does, each at most 0.5 s or four closest distances' flight
    //     from the pass; between them it moves evenly in pitch (at 0, half way).
    for (double m : {0.5, 0.8, 2.0, 3.0})
        for (double tune : {-1.0, 0.0, 1.0}) {
            Flight f;
            f.mach = m; f.pass = 30; f.dur = 3; f.tune = tune;
            const Take take = play(f, 0.05);
            const mach::Voice& v = take.blocks.front();
            const double mm = v.mach / 268435456.0, d = v.image[0].d / q26;
            const double most = std::fmax(0.5, 4 * d / mm);
            auto octaves = [&](double edge) {   // log2 of dtau/dt heard there, held within 2.5 octaves
                const double x = mm * edge, r = std::sqrt(x * x + d * d);
                return std::fmin(std::fmax(std::log2(r / std::fabs(r + mm * x)), -2.5), 2.5);
            };
            const double first = octaves(std::fmax(v.sigma_start / q26, -most)), last = octaves(std::fmin(v.sigma_end / q26, most));
            const double want = std::pow(2.0, -(first + (last - first) * (tune + 1) / 2));
            const double got = v.pitch / 65536.0;
            const double cents = 1200 * std::log2(got / want);
            char what[128];
            std::snprintf(what, sizeof what, "tune %+.0f at Mach %.1f: the source at %.4f of the key, %.2f cents from exact", tune, m, got, cents);
            check(std::fabs(cents) < 1.0, what);
        }

    // 2. Supersonic: silence, then the boom at the cone, then two roots, one
    //    running backwards through the source's time.
    {
        Flight f;
        f.mach = 2; f.pass = 20; f.dur = 2; f.hz = 220; f.source = mach::ORGAN; f.boom = 0.8;
        const Take take = play(f, 3);
        const mach::Voice& first = take.blocks.front();
        const int boom = first.image[0].boom_at;
        check(first.image[0].cone == 1 && boom > 0, "supersonic: the cone leaves inside the flight");
        check(peak(take.out, 0, boom) == 0, "supersonic: silent before the cone arrives");
        // The boom is the attack: the images swell in over the 40 ms after it.
        const int length = first.image[0].boom_length;
        const double bang = peak(take.out, boom, boom + length);
        const double after = peak(take.out, boom + length + 220, boom + length + 441);
        std::printf("      boom peak %.3f, the tone 5..10 ms after it %.3f\n", bang, after);
        check(bang > 0.1 && bang > 2 * after, "supersonic: a boom as the cone arrives, ahead of the tone");
        const double m = first.mach / 268435456.0, d = first.image[0].d / q26;
        bool both = false, backwards = true, forwards = true, exact = true;
        double worst = 0;
        for (const mach::Voice& v : take.blocks) {
            const mach::Root& fwd = v.root[0];
            const mach::Root& rev = v.root[1];
            if (fwd.state == mach::TRACKING && rev.state == mach::TRACKING) {
                both = true;
                backwards = backwards && rev.step < 0;
                forwards = forwards && fwd.step > 0;
                const double t = v.t / q26;
                const double e0 = std::fabs(fwd.sigma / q26 - exact_root(m, d, t, 1));
                const double e1 = std::fabs(rev.sigma / q26 - exact_root(m, d, t, -1));
                worst = std::fmax(worst, std::fmax(e0, e1));
                exact = exact && e0 < 2e-7 && e1 < 2e-7;
            }
        }
        check(both, "supersonic: two roots sound together after the cone");
        check(forwards && backwards, "supersonic: one root runs forwards, the other backwards in the source's time");
        std::printf("      worst root error %.1f ns\n", worst * 1e9);
        check(exact, "supersonic: both roots match the exact retarded times within 200 ns");
        // In the audio, 0.4 s after the cone: both components, at their own pitches.
        const size_t block = static_cast<size_t>((boom + 0.4 * rate) / 64);
        const mach::Voice& v = take.blocks[block];
        const double rf = v.root[0].step / double(mach::substep_time), rr = v.root[1].step / double(mach::substep_time);
        const size_t from = block * 64 - 2048;
        const double hz = f.hz * first.pitch / 65536.0;
        const double sf = strength(take.out, from, 4096, hz * rf), sr = strength(take.out, from, 4096, hz * -rr);
        const double floor = strength(take.out, from, 4096, hz * (rf - rr) / 2 * 1.13);
        std::printf("      forward at %.1f Hz: %.4f, backward at %.1f Hz: %.4f, between: %.4f\n",
                    hz * rf, sf, -hz * rr, sr, floor);
        check(sf > 4 * floor && sr > 4 * floor, "supersonic: both components are heard, each at its own pitch");
        check(peak(take.out, 0, take.out.size()) <= declared, "supersonic: within the declared peak");
    }

    // 2a. Across the supersonic range, both roots leave the fold and track the
    //     exact retarded times, the backward one a long way back through the
    //     flight.
    for (double m : {1.2, 2.5, 3.0}) {
        Flight f;
        f.mach = m; f.pass = 20; f.dur = 3; f.hz = 220; f.boom = 0; f.tune = -1;
        const Take take = play(f, 3);
        const double mm = take.blocks.front().mach / 268435456.0, d = take.blocks.front().image[0].d / q26;
        double worst = 0, back = 0;
        bool both = false;
        for (const mach::Voice& v : take.blocks) {
            const mach::Root& fwd = v.root[0];
            const mach::Root& rev = v.root[1];
            const bool live = (fwd.state == mach::FOLD || fwd.state == mach::TRACKING) && (rev.state == mach::FOLD || rev.state == mach::TRACKING);
            if (!live || fwd.step == 0 || rev.step == 0) continue;
            both = true;
            const double t = v.t / q26;
            worst = std::fmax(worst, std::fmax(std::fabs(fwd.sigma / q26 - exact_root(mm, d, t, 1)),
                                               std::fabs(rev.sigma / q26 - exact_root(mm, d, t, -1))));
            back = std::fmax(back, (v.image[0].sigma_star - rev.sigma) / q26);
        }
        char what[160];
        std::snprintf(what, sizeof what, "Mach %.1f: both roots track (worst %.1f ns), the backward one %.2f s back", m, worst * 1e9, back);
        check(both && worst < 2e-7 && back > 0.5, what);
    }

    // 2c. Below Mach 1 there is no shock: the boom is the bow wave's pressure
    //     pulse, heard as the pass's sound arrives, ahead of the tone there.
    {
        Flight f;
        f.mach = 0.6; f.pass = 12; f.dur = 2; f.hz = 220; f.boom = 0.8; f.tune = 1;
        const Take take = play(f, 2);
        Flight quiet = f;
        quiet.boom = 0;
        const Take tone = play(quiet, 2);
        const mach::Voice& first = take.blocks.front();
        const int at = first.image[0].boom_at;
        const double pass_at = (first.image[0].d - first.t_start) / q26 * rate;
        double pulse = 0;
        for (size_t i = 0; i < take.out.size(); ++i) pulse = std::fmax(pulse, std::fabs(take.out[i] - tone.out[i]));
        std::printf("      pulse at %d (the pass heard at %.0f), %.3f over the tone\n", at, pass_at, pulse);
        check(first.image[0].cone == 0 && std::abs(at - pass_at) < 64 && pulse > 0.05, "subsonic boom: a pulse as the pass is heard");
    }

    // 2d. A cone that leaves right at the flight's start (Mach 1.67 at 190 m,
    //     tune at the end): the flight sounds, and settles on the key.
    {
        Flight f;
        f.mach = 1.6667; f.pass = 189; f.dur = 2.5; f.hz = 220; f.boom = 0.3; f.tune = 1; f.source = mach::BELL;
        const Take take = play(f, 6);
        const mach::Voice& last = take.blocks[static_cast<size_t>(4.0 * rate / opxy::block_frames)];
        const double heard = last.root[0].step / double(mach::substep_time) * last.pitch / 65536.0;
        std::printf("      peak %.3f; at 4 s the forward image at %.3f of the key\n", peak(take.out, 0, take.out.size()), heard);
        check(peak(take.out, 0, take.out.size()) > 0.05 && std::fabs(std::log2(heard)) < 0.05, "a cone at the flight's start: it sounds, settling on the key");
    }

    // 2b. Heard backwards: with the bell (struck as the flight starts and at
    //     its closest pass, a second apart in its own time),
    //     the backward image's prime partial swells through a strike period and
    //     the forward image's dies away. Each is picked out of the sound by
    //     mixing against its own exact phase, 2 pi f sigma(t).
    {
        Flight f;
        f.mach = 2; f.pass = 20; f.dur = 2; f.hz = 220; f.boom = 0; f.source = mach::BELL;
        const Take take = play(f, 3);
        const mach::Voice& first = take.blocks.front();
        const double m = first.mach / 268435456.0, d = first.image[0].d / q26, t0 = first.t_start / q26;
        const double hz = f.hz * first.pitch / 65536.0;   // the source's own pitch
        // The partial's strength around sample n along one root.
        auto partial = [&](size_t n, int branch) {
            double re = 0, im = 0;
            const size_t w = 1024;
            for (size_t i = 0; i < w; ++i) {
                const double t = t0 + (n - w / 2 + i) / rate;
                const double ph = 2 * M_PI * hz * exact_root(m, d, t, branch);
                const double x = (0.5 - 0.5 * std::cos(2 * M_PI * i / w)) * take.out[n - w / 2 + i];
                re += x * std::cos(ph);
                im += x * std::sin(ph);
            }
            return std::sqrt(re * re + im * im);
        };
        // When each root is a quarter and three quarters through the strike at
        // sigma = -1 .. 0 (backward) and 0 .. 1 (forward).
        auto when = [&](double sigma) { return static_cast<size_t>((sigma + std::sqrt(m * m * sigma * sigma + d * d) - t0) * rate); };
        // The backward ring over the root's gain less its ring (it swells
        // against the distance, which grows); the forward one as heard.
        auto ring = [&](double sigma) {
            const size_t n = when(sigma);
            const mach::Root& root = take.blocks[n / 64].root[1];
            return partial(n, -1) / (root.gain / 1073741824.0) * (mach::tables::bell_low[root.clock >> 22] / 32768.0);
        };
        const double back_early = ring(-0.25), back_late = ring(-0.75);
        const double fwd_early = partial(when(0.25), 1), fwd_late = partial(when(0.75), 1);
        std::printf("      backward ring over its gain: %.1f then %.1f; forward image: %.3f then %.3f\n", back_early, back_late, fwd_early, fwd_late);
        check(back_late > 1.5 * back_early, "supersonic bell: the backward image's strikes swell (its ring heard in reverse)");
        check(fwd_late < 0.5 * fwd_early, "supersonic bell: the forward image's strikes die away");
    }

    // 3. The fold at the cone does not click (boom off), at several speeds. The
    //    bell is the probe: its strikes are moved 16 s apart, far from the cone, so near
    //    the cone it is four pure partials. (A
    //    strike is a deliberate kink, forwards or backwards.)
    for (double m : {1.1, 1.5, 2.0, 3.0}) {
        Flight f;
        f.mach = m; f.pass = 20; f.dur = 5; f.hz = 220; f.boom = 0; f.source = mach::BELL; f.strike = 16;
        const Take take = play(f, 1.5);
        const int cone = take.blocks.front().image[0].boom_at;
        // The share of energy above 21 kHz around the cone, against the same
        // share 0.1 .. 0.2 s on, and against the cone with a step of 10% of its
        // level added (the detector works).
        const double steady = band_energy(take.out, cone + 4410, false);
        const double share = click_share(take.out, cone - 1024, cone + 1024, steady / 10);
        const double floor = click_share(take.out, cone + 4410, cone + 8820, steady / 10);
        const double level = peak(take.out, cone, cone + 1764);
        std::vector<double> stepped_out(take.out);
        for (size_t i = cone + 600; i < stepped_out.size(); ++i) stepped_out[i] += 0.1 * level;
        const double stepped = click_share(stepped_out, cone - 1024, cone + 1024, steady / 10);
        char what[200];
        std::snprintf(what, sizeof what, "fold at Mach %.1f: no click (above 21 kHz: %.1e of the energy, %.1e later, %.1e with a 10%% step)",
                      m, share, floor, stepped);
        check(share <= 2 * floor && stepped > 4 * share && level > 0.01, what);
    }
    // ... and with the bell, whose reversal is clearest, and with an echo.
    {
        Flight f;
        f.mach = 2; f.pass = 20; f.dur = 5; f.hz = 440; f.boom = 0; f.source = mach::BELL; f.echo = 0.6; f.strike = 16;
        const Take take = play(f, 3);
        const mach::Voice& first = take.blocks.front();
        double worst = 0, floor = 0, stepped = 1;
        for (int m = 0; m < 2; ++m) {
            const int cone = first.image[m].boom_at;
            const double steady = band_energy(take.out, cone + 4410, false);
            worst = std::fmax(worst, click_share(take.out, cone - 1024, cone + 1024, steady / 10));
            floor = std::fmax(floor, click_share(take.out, cone + 4410, cone + 8820, steady / 10));
            std::vector<double> stepped_out(take.out);
            const double level = peak(take.out, cone, cone + 1764);
            for (size_t i = cone + 600; i < stepped_out.size(); ++i) stepped_out[i] += 0.1 * level;
            stepped = std::fmin(stepped, click_share(stepped_out, cone - 1024, cone + 1024, steady / 10));
        }
        char what[192];
        std::snprintf(what, sizeof what, "fold with an echo: no click at either cone (above 21 kHz: %.1e of the energy, %.1e later, %.1e with a 10%% step)",
                      worst, floor, stepped);
        check(first.image[1].cone && (worst <= 2 * floor || 4 * worst < stepped), what);
        check(peak(take.out, 0, take.out.size()) <= declared, "bell and echo: within the declared peak");
    }

    // 4. Every source sounds and ends: the voice falls silent after its flight.
    for (int s = 0; s < mach::SOURCES; ++s) {
        Flight f;
        f.mach = 0.8; f.pass = 30; f.dur = 1; f.source = s;
        const Take take = play(f, 4);
        const mach::Voice& last = take.blocks.back();
        char what[96];
        std::snprintf(what, sizeof what, "source %d sounds, stays within the peak and ends", s);
        check(peak(take.out, 0, take.out.size()) > 0.02 && peak(take.out, 0, take.out.size()) <= declared && last.done, what);
    }

    if (failures) std::printf("%d checks failed\n", failures);
    return failures ? 1 : 0;
}
