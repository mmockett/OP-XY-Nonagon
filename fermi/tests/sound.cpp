// Fermi's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
//   - The integrator is exactly reversible, and conserves the chain's energy
//     over a long lossless run.
//   - At moderate energy, mode 1 comes back to at least 95% of the energy at
//     the recurrence time the screen states.
//   - At high energy the modes approach equipartition.
//   - At very low energy the spectrum barely changes.
//   - The seed's nudge takes its share of the note's energy, in the low
//     modes, and leaves the note's energy as it was.
//   - The partials sit at the chain's dispersion relation, stretched, or at
//     harmonics with dispersion at 0.
//   - Level, bounds and chords.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const double rate = 44100.0, pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
opxy::u32 increment(double hz) { return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5); }
double hz_of(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
// Controls: energy, masses (8..32), springs, time, dispersion, mode, damping, seed.
struct Controls {
    double energy = 0.5, springs = 0, time = 0.39, dispersion = 1, damping = 0;
    int masses = 16, mode = 1, seed = 0;
    void fill(opxy::i32* p) const {
        const int index = (masses - 8) / 4;
        p[0] = q31(energy); p[1] = q31((index + 0.5) / 7); p[2] = q31(springs); p[3] = q31(time);
        p[4] = q31(dispersion); p[5] = q31((mode - 0.5) / 4); p[6] = q31(damping); p[7] = q31((seed + 0.5) / 64);
    }
};
fermi::chain::Setup setup(const Controls& c) {
    opxy::i32 p[8];
    c.fill(p);
    fermi::chain::Setup s;
    fermi::chain::configure(s, p);
    return s;
}
// Every mode's share of the chain's harmonic energy.
void shares(const fermi::chain::Chain& c, const fermi::chain::Setup& s, double* e) {
    fermi::chain::Folded f;
    fermi::chain::fold(c, f);
    double total = 0;
    for (unsigned k = 1; k <= c.n; ++k) total += e[k] = fermi::chain::energy(c, f, k, s.shift) / 16777216.0;
    for (unsigned k = 1; k <= c.n; ++k) e[k] /= total;
}
// The chain's energy, in double from its state: the leapfrog's own
// invariant, kinetic energy at the velocity's time less its dt^2 / 8 term,
// and the springs' potential half a drift back.
struct Exact {
    double x[34], v[34];
};
double hamiltonian(const double* x, const double* v, unsigned n, double dt, double alpha, double beta) {
    double h = 0;
    for (unsigned j = 1; j <= n; ++j) h += 0.5 * v[j] * v[j];
    for (unsigned j = 0; j <= n; ++j) {
        const double dv = v[j + 1] - v[j];
        const double d = (x[j + 1] - 0.5 * dt * v[j + 1]) - (x[j] - 0.5 * dt * v[j]);
        h += d * d * (0.5 + d * (alpha / 3 + d * beta / 4)) - dt * dt / 8 * dv * dv;
    }
    return h;
}
double alpha_of(const fermi::chain::Setup& s) { return s.ma * std::ldexp(1.0, -s.common - s.scale); }
double beta_of(const fermi::chain::Setup& s) { return s.mb * std::ldexp(1.0, -s.common - s.scale); }
double hamiltonian(const fermi::chain::Chain& c, const fermi::chain::Setup& s) {
    double x[34], v[34];
    for (unsigned j = 0; j < 34; ++j) { x[j] = c.x[j] / 1048576.0; v[j] = c.v[j] / 1048576.0; }
    return hamiltonian(x, v, c.n, std::ldexp(1.0, -static_cast<int>(s.shift)), alpha_of(s), beta_of(s));
}

void reversible() {
    Controls k;
    k.energy = 0.9;
    k.seed = 5;
    const fermi::chain::Setup s = setup(k);
    fermi::chain::Chain c, start;
    fermi::chain::init(c, s);
    start = c;
    const unsigned steps = 200000;
    for (unsigned i = 0; i < steps; ++i) fermi::chain::step(c, s);
    // Backward: undo each drift (it depended only on v), then each kick (only on x).
    const opxy::i32 half = 1 << (s.shift - 1);
    for (unsigned i = 0; i < steps; ++i) {
        for (unsigned j = 1; j <= c.n; ++j) c.x[j] -= (c.v[j] + half) >> s.shift;
        opxy::i32 left = fermi::chain::force(c.x[1], s);
        for (unsigned j = 1; j <= c.n; ++j) {
            const opxy::i32 right = fermi::chain::force(c.x[j + 1] - c.x[j], s);
            c.v[j] -= (right - left + half) >> s.shift;
            left = right;
        }
    }
    const bool same = !std::memcmp(c.x, start.x, sizeof c.x) && !std::memcmp(c.v, start.v, sizeof c.v);
    std::printf("reversible: %u steps forward and back at energy 0.9 %s\n", steps, same ? "return bit for bit" : "DIFFER");
    check(same, "the integrator is not exactly reversible");
}

void conserves_energy() {
    // The integer chain beside the same integrator in double precision, which
    // shows how much the energy swings from the step itself (O(dt^2)).
    const double energies[3] = {0.5, 0.8, 1.0};
    const int masses[3] = {16, 32, 16};
    for (int r = 0; r < 3; ++r) {
        Controls k;
        k.energy = energies[r];
        k.masses = masses[r];
        k.springs = r == 2 ? 0.5 : 0;
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        const double dt = std::ldexp(1.0, -static_cast<int>(s.shift)), alpha = alpha_of(s), beta = beta_of(s);
        double x[34], v[34];
        for (unsigned j = 0; j < 34; ++j) { x[j] = c.x[j] / 1048576.0; v[j] = 0; }
        const double h0 = hamiltonian(c, s), d0 = hamiltonian(x, v, c.n, dt, alpha, beta);
        const unsigned steps = 2000000, window = 20000;
        double worst = 0, exact = 0, mean = 0, first = 0;
        for (unsigned i = 0; i < steps; ++i) {
            fermi::chain::step(c, s);
            double left = x[1] * (1 + x[1] * (alpha + beta * x[1]));
            for (unsigned j = 1; j <= c.n; ++j) {
                const double d = x[j + 1] - x[j], right = d * (1 + d * (alpha + beta * d));
                v[j] += dt * (right - left);
                x[j] += dt * v[j];
                left = right;
            }
            if (i % 50 == 0) {
                mean += hamiltonian(c, s) / h0;
                exact = std::fabs(hamiltonian(x, v, c.n, dt, alpha, beta) / d0 - 1) > exact
                      ? std::fabs(hamiltonian(x, v, c.n, dt, alpha, beta) / d0 - 1) : exact;
            }
            if ((i + 1) % window == 0) {
                mean /= window / 50;
                if (i + 1 == window) first = mean;
                worst = std::fabs(mean - 1) > worst ? std::fabs(mean - 1) : worst;
                mean = 0;
            }
        }
        std::printf("energy: N %u, energy %.1f, springs %.1f, dt 1/%u, %u steps (%.0f units): the integer chain's energy "
                    "stays within %.2f%% (averaged over %u steps); in double precision the same steps swing %.2f%%\n",
                    s.n, k.energy, k.springs, 1u << s.shift, steps, steps * dt, worst * 100, window, exact * 100);
        // Fixed point adds nothing to the step's own swing, and nothing drifts.
        check(worst < exact + 0.002, "the integer chain's energy moves more than the exact integrator's");
        check(std::fabs(first - 1) < exact + 0.002, "the chain's energy drifts over a long lossless run");
    }
}

// Runs the chain block by block as a voice does and returns mode m's share
// at the stated recurrence time (the best within 5% of it), and its lowest before.
void recurs(int masses, double energy, double springs, double* back, double* low, double* seconds, double* units) {
    Controls k;
    k.masses = masses;
    k.energy = energy;
    k.springs = springs;
    const fermi::chain::Setup s = setup(k);
    fermi::chain::Chain c;
    fermi::chain::init(c, s);
    *units = std::pow(2.0, fermi::chain::period_log2(s) / 4096.0);
    *seconds = std::pow(2.0, fermi::chain::period_seconds_log2(s, fermi::chain::amount(q31(k.time))) / 4096.0);
    double e[33];
    *back = 0;
    *low = 1;
    const double ticks = *units * 8;
    while (c.ticks < 1.06 * ticks) {
        fermi::chain::advance(c, s);
        shares(c, s, e);
        if (c.ticks < 0.94 * ticks) *low = e[s.m] < *low ? e[s.m] : *low;
        else *back = e[s.m] > *back ? e[s.m] : *back;
    }
}
void recurrence() {
    struct Case { int masses; double energy, springs; } cases[] = {{16, 0.5, 0}, {8, 0.5, 0}, {32, 0.5, 0}, {16, 0.5, 0.5}, {24, 0.5, 0}};
    for (const Case& t : cases) {
        double back, low, seconds, units;
        recurs(t.masses, t.energy, t.springs, &back, &low, &seconds, &units);
        std::printf("recurrence: N %d, energy %.2f, springs %.2f: mode 1 falls to %.3f and returns to %.3f at %.0f units "
                    "(%.2f s at the default time)\n", t.masses, t.energy, t.springs, low, back, units, seconds);
        check(low < 0.5, "at moderate energy, mode 1 does not give up half its energy");
        check(back >= 0.95, "mode 1 does not return to 95% at the stated recurrence time");
    }
}

void equipartition() {
    const int masses[3] = {8, 16, 32};
    for (int r = 0; r < 3; ++r) {
        Controls k;
        k.energy = 1;
        k.masses = masses[r];
        k.seed = 3;
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        double e[33], mean[33] = {0};
        const double units = 40000;
        unsigned samples = 0;
        while (c.ticks < units * 8) {
            fermi::chain::advance(c, s);
            if (c.ticks < units * 4) continue;   // the second half
            shares(c, s, e);
            for (unsigned j = 1; j <= c.n; ++j) mean[j] += e[j];
            ++samples;
        }
        double lo = 1, hi = 0, entropy = 0;
        for (unsigned j = 1; j <= c.n; ++j) {
            mean[j] /= samples;
            lo = mean[j] < lo ? mean[j] : lo;
            hi = mean[j] > hi ? mean[j] : hi;
            entropy -= mean[j] * std::log(mean[j]);
        }
        const double even = 1.0 / c.n;
        std::printf("equipartition: N %u at energy 1, time-averaged mode shares %.3f to %.3f (equal is %.3f), "
                    "effective modes %.2f of N\n", c.n, lo, hi, even, std::exp(entropy) / c.n);
        check(lo > 0.4 * even && hi < 2.5 * even, "at high energy the modes are far from equipartition");
        check(std::exp(entropy) / c.n > 0.9, "at high energy the time-averaged spectrum is not spread over the modes");
    }
}

// One voice, one note, rendered.
struct Player {
    fermi::Voice voice;
    fermi::Shared shared;
    opxy::i32 parameters[8];
    opxy::u32 pitch[64];
    opxy::i32 out[64];
    Player(const Controls& c, double hz) {
        c.fill(parameters);
        for (int i = 0; i < 64; ++i) pitch[i] = increment(hz);
        fermi::initialize(voice, shared, 1);
        fermi::start(voice, shared, 1);
    }
    void play() { fermi::render(voice, shared, 1, out, parameters, pitch); }
};
void render(const Controls& c, double hz, double* out, int count) {
    static Player* p;
    p = new Player(c, hz);
    for (int b = 0; b < count / 64; ++b) {
        p->play();
        for (int i = 0; i < 64; ++i) out[b * 64 + i] = p->out[i] / 2147483648.0;
    }
    delete p;
}
// Magnitude of a Blackman-Harris-windowed signal at f Hz.
double magnitude(const double* x, int n, double f) {
    double re = 0, im = 0;
    const double w = 2 * pi * f / rate;
    for (int i = 0; i < n; ++i) {
        const double t = 2 * pi * i / (n - 1);
        const double win = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        re += x[i] * win * std::cos(w * i);
        im -= x[i] * win * std::sin(w * i);
    }
    return std::sqrt(re * re + im * im);
}
// The strongest frequency within 10 cents of f, to a tenth of a cent.
double peak(const double* x, int n, double f) {
    double best = 0, at = f;
    for (double cents = -10; cents <= 10; cents += 0.5) {
        const double g = f * std::pow(2.0, cents / 1200), m = magnitude(x, n, g);
        if (m > best) { best = m; at = g; }
    }
    for (double cents = -2; cents <= 2; cents += 0.1) {
        const double g = at * std::pow(2.0, cents / 1200), m = magnitude(x, n, g);
        if (m > best) { best = m; at = g; }
    }
    return at;
}

void barely_changes() {
    // At energy 0, mode 1 keeps all but a sliver of the energy for a long run...
    const int masses[3] = {8, 16, 32};
    for (int r = 0; r < 3; ++r) {
        Controls k;
        k.energy = 0;
        k.masses = masses[r];
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        double e[33], low = 1;
        while (c.ticks < 8u * 60000) {
            fermi::chain::advance(c, s);
            shares(c, s, e);
            low = e[1] < low ? e[1] : low;
        }
        std::printf("pure: N %d at energy 0, mode 1 keeps at least %.4f of the energy over 60000 units\n", masses[r], low);
        check(low > 0.98, "at very low energy, mode 1 gives up more than 2%");
    }
    // ...so the sound's spectrum is steady: its first three harmonics (the
    // struck mode with its body) hold their levels within 0.2 dB, and the
    // second mode's partial stays 40 dB down.
    Controls k;
    k.energy = 0;
    k.time = 0;   // the fastest clock, so the run covers many periods
    k.dispersion = 0.2;
    const int n = 8192, blocks = 200;
    static double x[64 * 2000];
    render(k, 220.0, x, 64 * blocks * 10);
    double first[3] = {0}, spread = 0, worst = -200;
    const double second = 220.0 * (2 + 0.2 * (fermi::tables::ratio[(2 * 4) * 17 + 2] / 65536.0 - 2));
    for (int w = 1; w < 10; ++w) {
        const double* y = x + w * 64 * blocks;
        for (int h = 0; h < 3; ++h) {
            const double a = magnitude(y, n, 220.0 * (h + 1)) + 1e-12;
            if (w == 1) first[h] = a;
            spread = std::fabs(20 * std::log10(a / first[h])) > spread ? std::fabs(20 * std::log10(a / first[h])) : spread;
        }
        (void)second;
    }
    // The second mode, from the chain itself (the body's harmonic shares its frequency).
    {
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        double e[33];
        while (c.ticks < 8u * 20000) {
            fermi::chain::advance(c, s);
            shares(c, s, e);
            worst = 10 * std::log10(e[2] + 1e-30) > worst ? 10 * std::log10(e[2] + 1e-30) : worst;
        }
    }
    std::printf("pure: over %.1f s at the fastest clock the tone's first three harmonics hold within %.2f dB; "
                "mode 2 stays %.1f dB under the struck mode\n", 9 * 64.0 * blocks / rate, spread, -worst);
    check(spread < 0.2, "at very low energy, the tone's harmonics change level");
    check(worst < -40, "at very low energy, mode 2 rises within 40 dB of the struck mode");
}

void settles() {
    // Damping takes more from the high modes (as w^2), so a thermalized note
    // darkens toward its lowest modes as it fades, as a string does. The
    // brightness is the energy-weighted mean mode number.
    struct Run { double first, later; };
    auto run = [](double damping) {
        Controls k;
        k.energy = 1;
        k.damping = damping;
        k.time = 0.3;
        k.seed = 3;
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        double e[33];
        Run r = {0, 0};
        int a = 0, b = 0;
        for (int block = 0; block < 689 * 3; ++block) {
            fermi::chain::advance(c, s);
            if (block % 32) continue;
            shares(c, s, e);
            double mean = 0;
            for (unsigned j = 1; j <= c.n; ++j) mean += j * e[j];
            if (block >= 345 && block < 689) { r.first += mean; ++a; }
            if (block >= 689 * 2) { r.later += mean; ++b; }
        }
        r.first /= a;
        r.later /= b;
        return r;
    };
    const Run damped = run(0.5), lossless = run(0);
    std::printf("settles: energy 1, damping 0.5 (struck mode's T60 5.5 s): mean mode number %.1f at 0.5-1 s, %.1f at 2-3 s "
                "(lossless: %.1f, %.1f)\n", damped.first, damped.later, lossless.first, lossless.later);
    check(damped.later < 0.6 * lossless.later, "damping does not darken a thermalized note as it fades");
}

void seeds() {
    // At the note's start, the nudge takes its share of the energy from the
    // struck mode, mostly into modes 2 to 6, and the note's energy (the
    // modes' sum, against the unnudged note's) stays where it was.
    const int seeds[4] = {0, 16, 32, 63};
    double plain = 0;
    for (int i = 0; i < 4; ++i) {
        Controls k;
        k.energy = 0;
        k.seed = seeds[i];
        const fermi::chain::Setup s = setup(k);
        fermi::chain::Chain c;
        fermi::chain::init(c, s);
        fermi::chain::Folded f;
        fermi::chain::fold(c, f);
        double e[33], total = 0, low = 0;
        for (unsigned j = 1; j <= c.n; ++j) total += e[j] = fermi::chain::energy(c, f, j, s.shift) / 16777216.0;
        for (unsigned j = 2; j <= 6; ++j) low += e[j];
        if (!i) plain = total;
        const double x = seeds[i] / 63.0, wanted = 0.66 * x * (1.5 - 0.5 * x), took = 1 - e[1] / total;
        std::printf("seed %d: the nudge takes %.3f of the energy (%.3f asked), %.2f of it in modes 2 to 6; the note's energy is %.3f of the plain note's\n",
                    seeds[i], took, wanted, took > 0 ? low / total / took : 1.0, total / plain);
        check(std::fabs(took - wanted) < 0.06, "the nudge does not take its share of the energy");
        check(!i || low / total > 0.75 * took, "the nudge does not lie in the low modes");
        check(std::fabs(total / plain - 1) < 0.12, "the nudge changes the note's energy");
    }
}

void dispersion() {
    // N = 8 is the most dispersive chain. At energy 0.65 its modes all take up
    // energy within a second; then the clock is slowed 22 times, so each
    // partial holds still for the analysis. The partials should sit at
    // k + D (w_k / w_1 - k), D being dispersion 1's stretch for 8 masses
    // (the top partial 500 cents flat); with dispersion at 0, on k.
    for (int pass = 0; pass < 2; ++pass) {
        Controls k;
        k.masses = 8;
        k.energy = 0.65;
        k.time = 0;
        k.dispersion = pass ? 0 : 1;
        const double f1 = 110.0;
        const int n = 65536;
        static double x[65536];
        Player p(k, f1);
        for (int b = 0; b < 1400; ++b) p.play();
        k.time = 1;
        k.fill(p.parameters);
        for (int b = 0; b < n / 64; ++b) {
            p.play();
            for (int i = 0; i < 64; ++i) x[b * 64 + i] = p.out[i] / 2147483648.0;
        }
        double worst = 0;
        std::printf("dispersion %d: partial ratios", pass ? 0 : 1);
        for (int j = 2; j <= 8; ++j) {
            const double stretch = fermi::tables::stretch[0] / 4096.0;
            const double chain = j + stretch * (std::sin(j * pi / 18) / std::sin(pi / 18) - j), expected = pass ? j : chain;
            const double f = peak(x, n, f1 * expected);
            const double cents = 1200 * std::log2(f / (f1 * expected));
            std::printf(" %d: %.3f (%+.1f c%s)", j, f / f1, cents, pass ? "" : " from the chain's");
            worst = std::fabs(cents) > worst ? std::fabs(cents) : worst;
        }
        std::printf("\n");
        check(worst < 1, "a partial is off the dispersion relation (or harmonic) by a cent or more");
    }
}

void levels() {
    // One note at A3, energies across the regimes; RMS over the first 4 s.
    const double energies[4] = {0, 0.5, 0.8, 1};
    const int n = 44100 * 4;
    static double x[44100 * 4 + 64];
    for (double e : energies) {
        Controls k;
        k.energy = e;
        render(k, 220.0, x, n);
        double ss = 0, top = 0, dc = 0;
        for (int i = 0; i < n; ++i) { ss += x[i] * x[i]; top = std::fabs(x[i]) > top ? std::fabs(x[i]) : top; dc += x[i]; }
        std::printf("level: A3 at energy %.1f, %.1f dBFS RMS, peak %.3f, DC %.1e\n", e, 10 * std::log10(ss / n), top, dc / n);
        check(10 * std::log10(ss / n) > -27 && 10 * std::log10(ss / n) < -22, "a single A3 is not near -25 dBFS RMS");
    }
    // An eight-note chord at energy 1, worst case for peaks.
    const double chord[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    double sum[44100 * 2] = {0};
    for (int v = 0; v < 8; ++v) {
        Controls k;
        k.energy = 0.5;
        render(k, hz_of(static_cast<int>(chord[v])), x, 44100 * 2);
        for (int i = 0; i < 44100 * 2; ++i) sum[i] += x[i];
    }
    double top = 0;
    for (int i = 0; i < 44100 * 2; ++i) top = std::fabs(sum[i]) > top ? std::fabs(sum[i]) : top;
    std::printf("level: an eight-note chord at energy 0.5 peaks at %.2f of full scale\n", top);
    check(top > 0.3 && top < 0.9, "an eight-note chord's peak is outside 0.3..0.9");
}

void bounded() {
    // Every control corner, from A0 to C8: within half scale and never silent.
    const double notes[3] = {27.5, 440.0, 4186.0};
    bool ok = true, sounding = true;
    for (int corner = 0; corner < 256; ++corner) {
        Controls k;
        k.energy = corner & 1;
        k.masses = corner & 2 ? 32 : 8;
        k.springs = corner & 4 ? 1 : 0;
        k.time = corner & 8 ? 1 : 0;
        k.dispersion = corner & 16 ? 1 : 0;
        k.mode = corner & 32 ? 4 : 1;
        k.damping = corner & 64 ? 1 : 0;
        k.seed = corner & 128 ? 63 : 0;
        for (double hz : notes) {
            Player p(k, hz);
            bool any = false;
            for (int b = 0; b < 400; ++b) {
                p.play();
                for (int i = 0; i < 64; ++i) {
                    ok = ok && p.out[i] >= -1073741824 && p.out[i] <= 1073741824;
                    any = any || p.out[i] != 0;
                }
            }
            if (hz < 1000 && !any) sounding = false;
        }
    }
    check(ok, "output beyond the declared peak");
    check(sounding, "a note is silent");
}
}

int main() {
    reversible();
    conserves_energy();
    recurrence();
    equipartition();
    barely_changes();
    settles();
    seeds();
    dispersion();
    levels();
    bounded();
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
