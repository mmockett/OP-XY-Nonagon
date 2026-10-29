// Cocoa's controls, measured: each at 0, 25, 50, 75 and 100 with the others at
// the default (Mug Keys), on an A3, as README.md's table gives them. Run by
// sdk.py test; it checks each control moves its measure the way it should.
//   powder  the first tap's pitch, semitones from the key (the engine's own)
//   taps    taps a second while a note is held (counted)
//   fizz    when the note is within a cent of the key, ms
//   cup     how long a tap rings, to 40 dB down, ms (audio)
//   fill    the column's second mode over its first (the engine's own)
//   spoon   a tap's upper modes and clink against its fundamental, first 0.2 s, dB (audio)
//   pops    the pops' own level against the rest, dB, in a long climb (audio)
//   stir    a second note's scoop 0.1 s after the first, semitones
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const double rate = 44100.0, pi = 3.14159265358979323846;
const double defaults[8] = {0.32, 0.0, 0.7, 0.1667, 0.65, 0.6, 0.15, 1.0};
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
    case 0: {   // powder: the first tap's pitch
        Mug m(c); m.note(0); m.play();
        return m.semitones(0);
    }
    case 1: {   // taps: taps a second over 4 s
        Mug m(c); m.note(0);
        for (int b = 0; b < 4 * 689; ++b) m.play();
        return (m.voice[0].taps - 1) / (4 * 689 * 64 / rate) + (c[1] < 0.02 ? 0 : 0);
    }
    case 2: {   // fizz: within a cent
        Mug m(c); m.note(0);
        for (int b = 0; b < 60 * 689; ++b) {
            m.play();
            if (std::fabs(m.semitones(0)) < 0.01) return b * 64 / rate * 1000;
        }
        return -1;
    }
    case 3: {   // cup: ring time to -40 dB, by 10 ms windows
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
    case 4: {   // fill: the second mode's ratio, once clear
        Mug m(c); m.note(0);
        for (int b = 0; b < 3 * 689; ++b) m.play();
        return m.voice[0].liquid[1].inc / static_cast<double>(m.voice[0].liquid[0].inc);
    }
    case 5: {   // spoon: energy from 1.5 to 12 times the key against the fundamental's
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
    case 6: {   // pops: in Hot Chocolate's climb, the energy with pops over without
        const double climb[8] = {0.87, 0.55, 0.2, 0.1667, 0.5, 0.6, value, 1.0};
        const double still[8] = {0.87, 0.55, 0.2, 0.1667, 0.5, 0.6, 0.0, 1.0};
        double e[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            Mug m(k ? climb : still); m.note(0);
            for (int b = 0; b < 3 * 689; ++b) { m.play(); for (int i = 0; i < 64; ++i) e[k] += m.out[i] * m.out[i]; }
        }
        return e[1] > e[0] ? 10 * std::log10((e[1] - e[0]) / e[0]) : -99;
    }
    default: {  // stir: a second note 0.1 s after the first
        Mug m(c); m.note(0);
        for (int b = 0; b < 69; ++b) m.play();
        m.note(1); m.play();
        return m.semitones(1);
    }
    }
}
}

int main() {
    const char* names[8] = {"powder", "taps", "fizz", "cup", "fill", "spoon", "pops", "stir"};
    const char* units[8] = {"st", "/s", "ms", "ms", "x", "dB", "dB", "st"};
    double table[8][5];
    for (int k = 0; k < 8; ++k) {
        std::printf("%-7s", names[k]);
        for (int j = 0; j < 5; ++j) {
            table[k][j] = measure(k, positions[j]);
            std::printf(" %9.2f", table[k][j]);
        }
        std::printf("  %s\n", units[k]);
    }
    auto check = [](bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } };
    check(table[0][0] == 0 && table[0][4] < -40, "powder: no scoop at 0, four octaves at 100");
    check(table[1][0] == 0 && table[1][4] > 10, "taps: once at 0, twelve a second at 100");
    check(table[2][0] > table[2][2] && table[2][2] > table[2][4], "fizz: larger bubbles land sooner");
    check(table[3][2] > table[3][0], "cup: glass rings longer than china");
    check(table[4][4] > table[4][0], "fill: deeper drinks spread the modes");
    check(table[5][4] > table[5][0] + 6, "spoon: a hard spoon is brighter by 6 dB");
    check(table[6][0] == -99 && table[6][4] > table[6][2] && table[6][4] > -16, "pops: louder with pops, within 16 dB of the rest at 100");
    check(table[7][0] > table[7][4], "stir: stir 0 continues the drink, 100 scoops afresh");
    return failures ? 1 : 0;
}
