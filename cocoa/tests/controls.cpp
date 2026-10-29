// Cocoa's controls, measured: each at 0, 25, 50, 75 and 100 with the others at
// the default (Mug Keys), on an A3, as README.md's table gives them. Run by
// sdk.py test; it checks each control moves its measure the way it should,
// and on every step of its travel, not only in one corner.
//   powder  the first tap's pitch, semitones from the key (the engine's own)
//   taps    taps a second while a note is held (counted)
//   fizz    when the drink is within a cent of clear, s (the engine's own drop)
//   cup     how long a tap rings, to 40 dB down, ms (audio)
//   fill    the same, ms (audio)
//   spoon   a tap's upper modes and clink against its fundamental, first 0.2 s, dB (audio)
//   pops    the pops' level against the rest of four notes, dB (audio)
//   stir    a second note 0.25 s after the first: its scoop, semitones; and
//           the first note's sway, the pitch's swing around its drop, cents
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const double rate = 44100.0, pi = 3.14159265358979323846;
const double defaults[8] = {0.32, 0.0, 0.7, 0.1667, 0.65, 0.6, 0.15, 0.6};
const double positions[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
int failures = 0;

opxy::u32 increment(double hz) { return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5); }
opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }

struct Mug {
    cocoa::Voice voice[2];
    cocoa::Shared shared;
    opxy::i32 parameters[8];
    opxy::u32 pitch[64];
    double out[64];
    bool on[2];
    explicit Mug(const double* c) {
        std::memset(static_cast<void*>(&shared), 0, sizeof shared);
        for (int k = 0; k < 8; ++k) parameters[k] = q31(c[k]);
        for (int i = 0; i < 64; ++i) pitch[i] = increment(220.0);
        for (unsigned v = 0; v < 2; ++v) { cocoa::initialize(voice[v], shared, v + 1); on[v] = false; }
    }
    void note(unsigned v) { cocoa::start(voice[v], shared, v + 1); on[v] = true; }
    void play() {
        for (int i = 0; i < 64; ++i) out[i] = 0;
        for (unsigned v = 0; v < 2; ++v) {
            if (!on[v]) continue;
            opxy::i32 o[64];
            cocoa::render(voice[v], shared, v + 1, o, parameters, pitch);
            for (int i = 0; i < 64; ++i) out[i] += o[i] / 2147483648.0;
        }
    }
    double semitones(unsigned v) const { return 12 * std::log2(voice[v].liquid[0].inc / static_cast<double>(pitch[0])); }
};

double measure(int control, double value) {
    double c[8];
    for (int k = 0; k < 8; ++k) c[k] = defaults[k];
    c[control] = control == 3 ? (value < 0.34 ? 0.1667 : value < 0.67 ? 0.5 : 0.8333) : value;
    switch (control) {
    case 0: {   // powder: the first tap's pitch, from the drink's drop
        Mug m(c); m.note(0); m.play();
        return -12.0 * m.voice[0].drop / 65536.0;
    }
    case 1: {   // taps: taps a second over 4 s
        Mug m(c); m.note(0);
        for (int b = 0; b < 4 * 689; ++b) m.play();
        return (m.voice[0].taps - 1) / (4 * 689 * 64 / rate) + (c[1] < 0.02 ? 0 : 0);
    }
    case 2: {   // fizz: the drink within a cent of clear
        Mug m(c); m.note(0);
        for (int b = 0; b < 60 * 689; ++b) {
            m.play();
            if (b > 0 && m.voice[0].drop < 55) return b * 64 / rate;
        }
        return -1;
    }
    case 3:
    case 4: {   // cup and fill: ring time to -40 dB, by 10 ms windows
        Mug m(c); m.note(0);
        double first = 0;
        for (int b = 0; b < 6 * 689; b += 7) {
            double e = 0;
            for (int k = 0; k < 7; ++k) { m.play(); for (int i = 0; i < 64; ++i) e += m.out[i] * m.out[i]; }
            if (b == 7) first = e;
            if (b > 7 && e < first * 1e-4) return b * 64 / rate * 1000;
        }
        return -1;
    }
    case 5: {   // spoon: energy from 1.5 to 12 times the key against the fundamental's, first 0.2 s
        Mug m(c); m.note(0);
        static double x[8820];
        for (int b = 0; b < 137; ++b) { m.play(); for (int i = 0; i < 64; ++i) x[b * 64 + i] = m.out[i]; }
        double low = 0, high = 0;
        for (double f = 100; f < 10000; f *= 1.02) {
            double re = 0, im = 0;
            for (int i = 0; i < 8768; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2 * pi * i / 8768);
                re += w * x[i] * std::cos(2 * pi * f * i / rate);
                im += w * x[i] * std::sin(2 * pi * f * i / rate);
            }
            (f < 330 ? low : high) += (re * re + im * im) * f;
        }
        return 10 * std::log10(high / low);
    }
    case 6: {   // pops: four notes of 2 s, with pops against the same without: the difference is the pops
        double e[2] = {0, 0}, d = 0;
        static double x[2][4 * 1378 * 64];
        for (int k = 0; k < 2; ++k) {
            double cc[8];
            for (int q = 0; q < 8; ++q) cc[q] = c[q];
            if (!k) cc[6] = 0;
            Mug m(cc);
            for (int n = 0; n < 4; ++n) {
                cocoa::initialize(m.voice[0], m.shared, 1);
                m.note(0);
                for (int b = 0; b < 1378; ++b) { m.play(); for (int i = 0; i < 64; ++i) x[k][(n * 1378 + b) * 64 + i] = m.out[i]; }
                cocoa::destroy(m.voice[0], m.shared, 1);
            }
        }
        for (int i = 0; i < 4 * 1378 * 64; ++i) { e[0] += x[0][i] * x[0][i]; d += (x[1][i] - x[0][i]) * (x[1][i] - x[0][i]); }
        return d > 0 ? 10 * std::log10(d / e[0]) : -99;
    }
    default: {  // stir: a second note 0.25 s after the first
        Mug m(c); m.note(0);
        for (int b = 0; b < 172; ++b) m.play();
        m.note(1); m.play();
        return m.semitones(1);
    }
    }
}
// stir's other measure: the first note's sway around its drop, 0.1 to 1 s, cents.
double sway(double value) {
    double c[8];
    for (int k = 0; k < 8; ++k) c[k] = defaults[k];
    c[7] = value;
    Mug m(c); m.note(0);
    double most = 0;
    for (int b = 0; b < 689; ++b) {
        m.play();
        if (b < 69) continue;
        const double cents = 100 * m.semitones(0) + 1200 * m.voice[0].drop / 65536.0;
        most = std::fabs(cents) > most ? std::fabs(cents) : most;
    }
    return most;
}
}

int main() {
    const char* names[8] = {"powder", "taps", "fizz", "cup", "fill", "spoon", "pops", "stir"};
    const char* units[8] = {"st", "/s", "s", "ms", "ms", "dB", "dB", "st"};
    double table[8][5];
    for (int k = 0; k < 8; ++k) {
        std::printf("%-7s", names[k]);
        for (int j = 0; j < 5; ++j) {
            table[k][j] = measure(k, positions[j]);
            std::printf(" %9.2f", table[k][j]);
        }
        std::printf("  %s\n", units[k]);
    }
    double swaying[5];
    std::printf("sway   ");
    for (int j = 0; j < 5; ++j) { swaying[j] = sway(positions[j]); std::printf(" %9.2f", swaying[j]); }
    std::printf("  cents\n");
    auto check = [](bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } };
    // Every step of every control's travel changes its measure (cup's three cups aside).
    auto steady = [&](int k, double least) {
        for (int j = 1; j < 5; ++j)
            if (k != 3 && std::fabs(table[k][j] - table[k][j - 1]) < least) { std::printf("FAIL %s flat from %d to %d\n", names[k], 25 * (j - 1), 25 * j); ++failures; }
    };
    check(table[0][0] == 0 && table[0][4] < -40, "powder: no scoop at 0, four octaves at 100");
    check(table[1][0] == 0 && table[1][4] > 10, "taps: once at 0, twelve a second at 100");
    check(table[2][0] > table[2][1] && table[2][1] > table[2][2] && table[2][2] > table[2][3] && table[2][3] > table[2][4], "fizz: larger bubbles land sooner");
    check(table[3][2] > table[3][0] && table[3][4] != table[3][2], "cup: each cup rings its own time");
    check(table[4][4] > 2 * table[4][0], "fill: deeper drinks ring longer");
    check(table[5][4] > table[5][0] + 20, "spoon: a hard spoon is brighter by 20 dB");
    check(table[6][0] == -99 && table[6][1] > -36 && table[6][4] > table[6][2] && table[6][2] > table[6][1], "pops: heard from 25, louder up to 100");
    check(table[7][0] > -1 && table[7][4] < -4, "stir: stir 0 continues the drink, 100 scoops afresh");
    check(swaying[0] < 0.01 && swaying[4] > 12 && swaying[2] > 4, "stir: the drink sways as hard as it is stirred");
    steady(0, 1.0); steady(1, 0.5); steady(2, 0.1); steady(4, 50); steady(5, 2.0); steady(6, 1.0); steady(7, 0.5);
    return failures ? 1 : 0;
}
