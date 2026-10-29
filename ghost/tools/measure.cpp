// Measures every preset as ears would hear it: a held A3 (level, crest,
// spectral centroid, harmonic-to-noise, interaural coherence and phase by
// band, the mono fold-down) and an eight-note chord's peak.
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/measure.cpp -o measure && ./measure
#include "../tests/analysis.h"
#include <cstdio>

int main() {
    using namespace measure;
    std::printf("%-9s %7s %6s %6s %7s %6s %7s %7s %6s | coherence (phase) by octave band: 110 220 440 880 1760 3520\n",
                "preset", "RMS dB", "crest", "HNR", "centre", "mono", "dip", "chord", "dB");
    std::printf("  (RMS: the ears' mean power, held A3; crest: peak over RMS; HNR to 5 kHz; mono: the fold-down's level against the ears; dip: its worst third-octave; chord: an 8-note chord's peak and RMS; now: short-window coherence)\n");
    for (const Preset& p : presets) {
        std::vector<double> left, right;
        const double f0 = 220;
        play({{0, 6, 57}}, p.values, 6.5, left, right, p.values[7] > 0, 0.05, 0.3);
        const size_t from = size_t(2.0 * fs);                       // after ring's swell
        std::vector<double> l(left.begin() + from, left.begin() + size_t(6 * fs)), r(right.begin() + from, right.begin() + size_t(6 * fs));
        Spectra s(l, r, 8192);
        const std::vector<double> m = fold(l, r);
        const double level = 10 * std::log10((rms(l) * rms(l) + rms(r) * rms(r)) / 2);   // the ears' mean power
        const double crest = db(std::fmax(peak(l), peak(r))) - level;
        // Each ear against its own fundamental (the voice's few cents, the right's beat).
        Spectra fine(l, r, 32768);
        // The fundamental may be gone (split), so each ear's pitch comes from its
        // stronger of the second and third harmonics.
        auto pitch_of = [&](const std::vector<double>& ch) {
            return fine.harmonic(ch, f0, 2) > fine.harmonic(ch, f0, 3) ? fine.fundamental(ch, 2 * f0) / 2 : fine.fundamental(ch, 3 * f0) / 3;
        };
        const double hnr = (fine.hnr(fine.left, pitch_of(fine.left), 5000) + fine.hnr(fine.right, pitch_of(fine.right), 5000)) / 2;
        const double centre = (s.centroid(s.left) + s.centroid(s.right)) / 2;
        const double mono = db(rms(m)) - level;
        // An eight-note chord: C3 E3 G3 B3 D4 F#4 A4 C5, all at once.
        std::vector<double> cl, cr;
        std::vector<Note> chord;
        const int notes[8] = {48, 52, 55, 59, 62, 66, 69, 72};
        for (int n : notes) chord.push_back({0, 3, double(n)});
        play(chord, p.values, 3.5, cl, cr, p.values[7] > 0, 0, 0.3);
        const double chord_peak = std::fmax(peak(cl), peak(cr));
        std::printf("%-9s %7.1f %6.1f %6.1f %6.0fHz %+6.1f %+7.1f %7.2f %6.1f |", p.name, level, crest, hnr, centre, mono,
                    worst_fold_dip(s), chord_peak, 10 * std::log10((rms(cl) * rms(cl) + rms(cr) * rms(cr)) / 2));
        // And over short windows (0.37 s, seven frames each), 220-880 Hz: how
        // alike the ears are at any moment, before beat, trade and orbit turn them.
        double moment = 0;
        u32 windows = 0;
        for (size_t at = 0; at + 16384 <= l.size(); at += 16384, ++windows) {
            std::vector<double> a(l.begin() + at, l.begin() + at + 16384), b(r.begin() + at, r.begin() + at + 16384);
            moment += Spectra(a, b, 4096).coherence(220, 880);
        }
        std::printf(" now %.2f |", moment / windows);
        for (double band = 110; band < 4000; band *= 2)
            std::printf(" %.2f(%+.0f)", s.coherence(band / 1.41, band * 1.41), s.phase(band / 1.41, band * 1.41));
        std::printf("\n");
    }
    return 0;
}
