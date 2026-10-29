// Cocoa's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
//   - After a stir the pitch rises monotonically, block by block.
//   - Once the drink is clear it lands within 1 cent of the key.
//   - The drop at a given void fraction is Wood's equation's.
//   - The column's effective void fraction is the pressure-weighted integral.
//   - Successive taps ring progressively higher, measured in the audio.
//   - With powder at 0 every tap rings at the key.
//   - Stir 0 continues the drink the last note left; taps keep their rate.
//   - Stir sets the drink sloshing, and the slosh dies away.
//   - The output stays within the declared peak, and a single tap falls silent.
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>

namespace {
const double rate = 44100.0, block = 64.0, pi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
opxy::u32 increment(double hz) { return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5); }
double hz_of(opxy::u32 inc) { return inc * rate / 4294967296.0; }
opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }
double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// Wood's equation in double precision, with make_tables.py's constants.
double wood_ratio(double x) {
    const double rho_l = 1000, c_l = 1500, rho_g = 1.2, c_g = 343, k = rho_l * c_l * c_l / (rho_g * c_g * c_g);
    return 1 / std::sqrt((1 - x + x * rho_g / rho_l) * (1 - x + x * k));
}

// Controls: powder, taps, fizz, cup, fill, spoon, pops, stir.
struct Controls { double c[8]; };
// The long climb (Hot Chocolate, without pops): three octaves in about five
// seconds. At stir 0 the drink does not slosh, so the pitch is the drink's
// alone (a first note stirs afresh whatever stir is).
const Controls standard = {{0.87, 0.55, 0.2, 0.1667, 0.5, 0.6, 0.0, 0.0}};
// Mug Keys, the default: a scoop of about five semitones.
const Controls keys = {{0.32, 0.0, 0.7, 0.1667, 0.65, 0.6, 0.15, 0.6}};

// Voices on one shared drink.
struct Mug {
    cocoa::Voice voice[8];
    cocoa::Shared shared;
    opxy::i32 parameters[8];
    opxy::u32 pitch[8][64];
    opxy::i32 out[64];
    bool on[8];
    explicit Mug(const Controls& c) : shared() {
        set(c);
        for (unsigned v = 0; v < 8; ++v) { cocoa::initialize(voice[v], shared, v + 1); on[v] = false; }
    }
    void set(const Controls& c) { for (int k = 0; k < 8; ++k) parameters[k] = q31(c.c[k]); }
    void note(unsigned v, double hz) {
        for (int i = 0; i < 64; ++i) pitch[v][i] = increment(hz);
        cocoa::start(voice[v], shared, v + 1);
        on[v] = true;
    }
    // One block of every voice that has a note; `out` is voice v's.
    void play(unsigned v = 0) {
        for (unsigned u = 0; u < 8; ++u) {
            if (!on[u]) continue;
            opxy::i32 o[64];
            cocoa::render(voice[u], shared, u + 1, o, parameters, pitch[u]);
            if (u == v) for (int i = 0; i < 64; ++i) out[i] = o[i];
        }
    }
    double fundamental(unsigned v = 0) const { return hz_of(voice[v].liquid[0].inc); }
    // The drink's own pitch, without its slosh.
    double drink(unsigned v = 0) const { return hz_of(pitch[v][0]) * std::pow(2.0, -voice[v].drop / 65536.0); }
};

void rises_monotonically() {
    Mug mug(standard);
    mug.note(0, 220.0);
    double last = 0, first = 0, window = 0;
    bool monotonic = true, strictly = true;
    int cleared = -1;
    for (int b = 0; b < 60 * 689; ++b) {
        mug.play();
        const double f = mug.fundamental();
        if (b == 0) first = window = f;
        if (b > 0 && f < last) monotonic = false;
        const bool bubbly = cocoa::drink::effective(mug.voice[0].d, mug.voice[0].s) > 0;
        // Every 50 ms while bubbles are left, the pitch has risen.
        if (b > 0 && b % 34 == 0) {
            if (bubbly && !(f > window)) strictly = false;
            window = f;
        }
        if (cleared < 0 && !bubbly) cleared = b;
        last = f;
    }
    std::printf("rise: from %.2f Hz (%.2f octaves under) to %.4f Hz, clear after %.2f s\n", first,
                std::log2(220.0 / first), last, cleared * block / rate);
    check(monotonic, "the pitch fell after a stir");
    check(strictly, "the pitch stood still for 50 ms while bubbles were left");
    check(first < 220.0 / 7.5, "powder 0.87 did not drop the pitch about three octaves");
    check(cleared > 0, "the drink never cleared");
}

void lands_in_tune() {
    double worst = 0;
    const double keys[] = {32.70, 110.0, 220.0, 440.0, 1046.5, 2093.0};
    const double fills[] = {0.0, 0.5, 1.0};
    for (double fill : fills)
        for (double key : keys) {
            Controls c = standard;
            c.c[4] = fill;
            c.c[2] = 1.0;   // quick bubbles, to be clear sooner
            Mug mug(c);
            mug.note(0, key);
            for (int b = 0; b < 30 * 689; ++b) mug.play();
            check(cocoa::drink::effective(mug.voice[0].d, mug.voice[0].s) == 0, "not clear after 30 s at fizz 1");
            const double e = std::fabs(cents(mug.fundamental(), key));
            worst = e > worst ? e : worst;
        }
    std::printf("in tune: once clear, at most %.4f cents from the key (six keys, three fills)\n", worst);
    check(worst < 1.0, "the clear drink is more than a cent from the key");
}

void follows_wood() {
    // The drop itself, against Wood's equation.
    double worst = 0;
    for (double x = 1e-6; x < 0.0165; x *= 1.07) {
        const double drop = cocoa::drink::wood_drop(q31(x)) / 65536.0;
        const double e = std::fabs(1200 * (drop + std::log2(wood_ratio(x))));
        worst = e > worst ? e : worst;
    }
    std::printf("wood: the drop departs from Wood's equation by at most %.4f cents (x 1e-6 to 1.6%%)\n", worst);
    check(worst < 0.1, "the drop departs from Wood's equation by 0.1 cent or more");
    // The effective void fraction, against the pressure-weighted integral.
    double worst_w = 0;
    for (double s = 0; s < 1; s += 0.0137) {
        opxy::i32 d[8] = {q31(0.01)}, sv[8];
        for (int i = 0; i < 8; ++i) sv[i] = cocoa::drink::clear;
        sv[0] = static_cast<opxy::i32>(s * (1 << 30));
        double integral = 0;
        const int n = 4000;
        for (int k = 0; k < n; ++k) {
            const double u = s + (1 - s) * (k + 0.5) / n;
            integral += 2 * std::cos(pi * u / 2) * std::cos(pi * u / 2) * (1 - s) / n;
        }
        const double x = cocoa::drink::effective(d, sv) / 2147483648.0;
        const double e = std::fabs(x - 0.01 * integral) / 0.01;
        worst_w = e > worst_w ? e : worst_w;
    }
    std::printf("wood: the effective void fraction departs from the weighted integral by %.2e of x\n", worst_w);
    check(worst_w < 1e-4, "the effective void fraction departs from the weighted integral");
    // And the engine's own pitch at its own effective void fraction.
    Mug mug(standard);
    mug.note(0, 440.0);
    double worst_e = 0;
    for (int b = 0; b < 10 * 689; ++b) {
        mug.play();
        const double x = cocoa::drink::effective(mug.voice[0].d, mug.voice[0].s) / 2147483648.0;
        const double e = std::fabs(cents(mug.fundamental(), 440.0 * wood_ratio(x)));
        worst_e = e > worst_e ? e : worst_e;
    }
    std::printf("wood: the voice's pitch departs from 440 Hz x c(x)/c_l by at most %.4f cents\n", worst_e);
    check(worst_e < 0.5, "the voice's pitch departs from Wood's equation");
}

// The strongest frequency from lo to hi Hz in n samples (Hann window), to 0.25 Hz.
double peak(const float* x, int n, double lo, double hi) {
    double best = 0, at = lo;
    for (double f = lo; f <= hi; f += 0.25) {
        double re = 0, im = 0;
        const double w = 2 * pi * f / rate;
        for (int i = 0; i < n; ++i) {
            const double h = 0.5 - 0.5 * std::cos(2 * pi * i / n);
            re += h * x[i] * std::cos(w * i);
            im += h * x[i] * std::sin(w * i);
        }
        if (re * re + im * im > best) { best = re * re + im * im; at = f; }
    }
    return at;
}

// Plays one note, and measures the pitch each tap rings at in the audio.
int taps_ring(const Controls& c, double key, double* heard, double* expected, int most) {
    Mug mug(c);
    mug.note(0, key);
    static float audio[44100 * 12];
    int tap_at[64], taps = 0;
    unsigned seen = 0;
    double reported[64];
    const int blocks = 12 * 689;
    for (int b = 0; b < blocks; ++b) {
        mug.play();
        for (int i = 0; i < 64; ++i) audio[b * 64 + i] = static_cast<float>(mug.out[i] / 2147483648.0);
        if (mug.voice[0].taps != seen && taps < most) {
            seen = mug.voice[0].taps;
            tap_at[taps] = b * 64;
            reported[taps++] = 0;
        }
        // The engine's own pitch 40 ms after each tap.
        for (int t = 0; t < taps; ++t)
            if (b * 64 == tap_at[t] + 1728) reported[t] = mug.fundamental();
    }
    const int window = 4096;
    int n = 0;
    for (int t = 0; t < taps && n < most; ++t) {
        if (tap_at[t] + window > blocks * 64) break;
        heard[n] = peak(audio + tap_at[t], window, 15.0, key * 1.08);
        expected[n++] = reported[t];
    }
    return n;
}

void taps_climb() {
    Controls c = standard;
    c.c[1] = 0.35;   // about 1.5 taps a second
    double heard[64], expected[64];
    const int n = taps_ring(c, 440.0, heard, expected, 16);
    bool climbing = true;
    double worst = 0;
    std::printf("taps:");
    for (int t = 0; t < n; ++t) {
        std::printf(" %.1f", heard[t]);
        if (t && heard[t] <= heard[t - 1] && expected[t] < 439.5) climbing = false;
        const double e = std::fabs(cents(heard[t], expected[t]));
        worst = e > worst ? e : worst;
    }
    std::printf(" Hz\ntaps: %d taps, each heard within %.1f cents of the engine's pitch 40 ms in\n", n, worst);
    check(n >= 10, "too few taps");
    check(climbing, "a tap rang no higher than the one before");
    check(heard[0] < 440.0 / 6, "the first tap did not ring well under the key");
    check(worst < 25, "a tap's ring is not at the engine's pitch");
}

void clear_drink_in_tune() {
    Controls c = standard;
    c.c[0] = 0;
    c.c[1] = 0.6;
    Mug mug(c);
    mug.note(0, 330.0);
    bool exact = true;
    for (int b = 0; b < 5 * 689; ++b) {
        mug.play();
        if (mug.voice[0].liquid[0].inc != increment(330.0)) exact = false;
    }
    check(exact, "with powder 0 the column is not exactly at the key");
    double heard[64], expected[64];
    const int n = taps_ring(c, 330.0, heard, expected, 12);
    double worst = 0;
    for (int t = 0; t < n; ++t) worst = std::fabs(cents(heard[t], 330.0)) > worst ? std::fabs(cents(heard[t], 330.0)) : worst;
    std::printf("powder 0: %d taps, every one heard within %.2f cents of the key\n", n, worst);
    check(n >= 8, "too few taps with powder 0");
    check(worst < 3, "with powder 0 a tap rings off the key");
}

void stir_continues() {
    // Stir 0: a second note, two seconds on, takes the drink as the first left it.
    Controls c = standard;
    c.c[7] = 0;
    Controls fresh = standard;
    fresh.c[7] = 1;
    Mug mug(fresh);
    mug.note(0, 220.0);
    for (int b = 0; b < 2 * 689; ++b) mug.play();
    mug.set(c);
    mug.note(1, 220.0);
    mug.play();
    const double a = mug.drink(0), b = mug.drink(1);
    std::printf("stir 0: a note 2 s after a stir starts at %.2f Hz, the first note is at %.2f Hz\n", b, a);
    check(std::fabs(cents(a, b)) < 1, "stir 0 does not continue the drink");
    check(a < 200, "the first note should still be bubbly at 2 s");
    // Stir 1 starts a fresh drink.
    Mug again(fresh);
    again.note(0, 220.0);
    for (int k = 0; k < 2 * 689; ++k) again.play();
    again.note(1, 220.0);
    again.play();
    check(again.fundamental(1) < again.fundamental(0) * 0.6, "stir 1 does not stir the drink afresh");
    // Taps keep their rate: 0.5 Hz x 24^c.
    Controls t = standard;
    t.c[1] = 0.5;
    Mug tapping(t);
    tapping.note(0, 220.0);
    for (int k = 0; k < 10 * 689; ++k) tapping.play();
    const double hz = 0.5 * std::pow(24.0, 0.5), seconds = 10 * 689 * block / rate;
    const double expect = 1 + std::floor(seconds * hz);
    std::printf("taps: %u in %.2f s at %.3f Hz, for %.0f\n", tapping.voice[0].taps, seconds, hz, expect);
    check(std::fabs(tapping.voice[0].taps - expect) <= 1, "the taps' rate is off");
}

void plays_as_keys() {
    // The default scoops a few semitones and lands within a cent well inside a second.
    Mug mug(keys);
    mug.note(0, 220.0);
    mug.play();
    const double first = mug.fundamental();
    int near = -1, landed = -1;
    for (int b = 1; b < 3 * 689; ++b) {
        mug.play();
        const double e = std::fabs(cents(mug.drink(), 220.0));
        if (near < 0 && e < 10) near = b;
        if (landed < 0 && e < 1) landed = b;
    }
    std::printf("keys: scoops %.1f semitones, within 10 cents at %.0f ms, within a cent at %.0f ms\n",
                -12 * std::log2(first / 220.0), near * block / rate * 1000, landed * block / rate * 1000);
    check(first < 220.0 * std::pow(2.0, -3.0 / 12) && first > 220.0 * std::pow(2.0, -8.0 / 12), "the default scoop is not 3 to 8 semitones");
    check(landed > 0 && landed * block / rate < 0.8, "the default does not land within a cent in 0.8 s");
    // A chord strums: four notes started together tap a block apart.
    Mug chord(keys);
    for (unsigned v = 0; v < 4; ++v) chord.note(v, 220.0 * std::pow(2.0, v * 4 / 12.0));
    int first_tap[4] = {-1, -1, -1, -1};
    for (int b = 0; b < 20; ++b) {
        chord.play();
        for (unsigned v = 0; v < 4; ++v) if (first_tap[v] < 0 && chord.voice[v].taps) first_tap[v] = b;
    }
    std::printf("strum: a four-note chord's first taps at blocks %d %d %d %d\n", first_tap[0], first_tap[1], first_tap[2], first_tap[3]);
    check(first_tap[0] == 0 && first_tap[1] == 1 && first_tap[2] == 2 && first_tap[3] == 3, "a chord does not strum a block apart");
    // A pause settles the drink: with stir 0, a note after every note has
    // ended is stirred afresh; one that overlaps carries the drink on.
    Controls still = standard;
    still.c[7] = 0;
    Mug pause(still);
    pause.note(0, 220.0);
    for (int b = 0; b < 3 * 689; ++b) pause.play();
    cocoa::destroy(pause.voice[0], pause.shared, 1);
    pause.on[0] = false;
    pause.note(1, 220.0);
    pause.play(1);
    std::printf("pause: after the drink has risen 3 s and the note ended, a stir-0 note starts at %.1f Hz\n", pause.fundamental(1));
    check(pause.fundamental(1) < 220.0 / 6, "a note after a pause does not stir afresh");
}

void sloshes() {
    // Stir sets the drink sloshing: the pitch swings around the drink's own,
    // at the mug's first sloshing mode (3.4 Hz at 59 mm), up to 20 cents at
    // stir 100, and dies away; at stir 0 it does not.
    const double stirs[2] = {1.0, 0.0};
    for (double stir : stirs) {
        Controls c = keys;
        c.c[7] = stir;
        Mug mug(c);
        mug.note(0, 220.0);
        double most = 0, late = 0, previous = 0;
        int crossings = 0;
        for (int b = 0; b < 3 * 689; ++b) {
            mug.play();
            const double swing = cents(mug.fundamental(), mug.drink());
            if (b < 689) most = std::fmax(most, std::fabs(swing));
            if (b >= 2 * 689) late = std::fmax(late, std::fabs(swing));
            if (b > 0 && b < 689 && (swing > 0) != (previous > 0) && std::fabs(swing) > 0.05) ++crossings;
            previous = swing;
        }
        std::printf("slosh: at stir %.0f the pitch swings %.1f cents, %d times across in the first second, %.2f cents after 2 s\n",
                    stir * 100, most, crossings, late);
        if (stir > 0) {
            check(most > 15 && most < 20.5, "stir 100 does not swing the pitch 15 to 20 cents");
            check(crossings >= 5 && crossings <= 8, "the slosh is not at the mug's sloshing mode");
            check(late < 2, "the slosh does not die away");
        } else {
            check(most < 0.01, "stir 0 sloshes");
        }
    }
}

void bounded_and_silent() {
    // The heaviest settings, many keys: within the declared peak.
    Controls c = {{1.0, 1.0, 1.0, 0.8333, 1.0, 1.0, 1.0, 1.0}};
    const double keys[] = {16.35, 65.4, 261.6, 1046.5, 4186.0, 8372.0};
    bool bounded = true;
    for (double key : keys) {
        Mug mug(c);
        mug.note(0, key);
        for (int b = 0; b < 4 * 689; ++b) {
            mug.play();
            for (int i = 0; i < 64; ++i) bounded = bounded && mug.out[i] >= -697932186 && mug.out[i] <= 697932186;
        }
    }
    check(bounded, "above the declared peak");
    // A single tap rings out and the voice falls silent.
    Controls once = standard;
    once.c[1] = 0;
    once.c[6] = 0.5;
    Mug mug(once);
    mug.note(0, 220.0);
    int quiet_at = -1;
    for (int b = 0; b < 40 * 689 && quiet_at < 0; ++b) {
        mug.play();
        if (mug.voice[0].quiet) quiet_at = b;
    }
    std::printf("once: a single tap falls silent after %.2f s\n", quiet_at * block / rate);
    check(quiet_at > 0, "a single tap never falls silent");
}
}

int main() {
    rises_monotonically();
    lands_in_tune();
    follows_wood();
    taps_climb();
    clear_drink_in_tune();
    stir_continues();
    sloshes();
    plays_as_keys();
    bounded_and_silent();
    return failures ? 1 : 0;
}
