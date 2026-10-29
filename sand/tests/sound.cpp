// Sand's sound checks, compiled for the host and run by
//   python sdk.py test --machine <folder>
// The defining behaviour first: the rattle is loud while the sand is on
// moving ground and dies as it settles on the nodal lines; a new figure or a
// new note makes it rise again; with no sand there is no rattle, and Shake
// keeps it going. Then the plate: bounded at every extreme, sustaining while
// bowed, tracking pitch, and every material and figure sounding.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace {
using sand::Voice;
using sand::Shared;
using opxy::i32;
using opxy::u32;

int failures = 0;
void check(const char* what, bool ok) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}

const i32 kPeak = 1073741824;
const unsigned kSecond = 689;  // blocks
enum { kFigure, kBow, kSand, kGrain, kPlate, kSettle, kTilt, kShake };

i32 word(double normalized) { return static_cast<i32>(std::lround(normalized * 2147483647.0)); }
// The middle of figure f's (or another enum's) range.
double pick(unsigned f, unsigned count) { return (f + 0.5) / count; }

void defaults(i32* p) {
    const double d[8] = {pick(2, 12), 0.6, 0.6, 0.3, 0, 0.5, 0, 0};
    for (unsigned i = 0; i < 8; ++i) p[i] = word(d[i]);
}

// A voice playing one note, as the device runs it.
struct Note {
    Voice voice;
    Shared shared;
    u32 pitch[opxy::block_frames];
    i32 out[opxy::block_frames];
    bool bounded = true;
    explicit Note(double hz, u32 owner = 1) {
        unsigned char* bytes = reinterpret_cast<unsigned char*>(&voice);
        for (unsigned i = 0; i < sizeof(Voice); ++i) bytes[i] = 0;
        unsigned char* common = reinterpret_cast<unsigned char*>(&shared);  // the SDK zeroes Shared too
        for (unsigned i = 0; i < sizeof(Shared); ++i) common[i] = 0;
        sand::initialize(voice, shared, owner);
        sand::start(voice, shared, owner);
        tune(hz);
    }
    void tune(double hz) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = static_cast<u32>(hz / 44100.0 * 4294967296.0);
    }
    void block(const i32* p) {
        sand::render(voice, shared, 1, out, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) bounded = bounded && out[i] >= -kPeak && out[i] <= kPeak;
    }
};

// The rattle: the output above 2.5 kHz (two second-order high-passes), where
// a low note's plate modes do not reach and dust's clicks do.
struct HighPass {
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    explicit HighPass(double hz) {
        const double w = 2 * M_PI * hz / 44100, c = std::cos(w), al = std::sin(w) / (2 * 0.7071), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double operator()(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// Renders blocks and returns the rattle's RMS and the whole output's RMS
// over them.
struct Meter {
    HighPass h1{2500}, h2{2500};
    double rattle(Note& n, const i32* p, unsigned blocks, double* all = 0) {
        double r = 0, a = 0;
        for (unsigned b = 0; b < blocks; ++b) {
            n.block(p);
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const double x = n.out[i] / 2147483648.0;
                const double y = h2(h1(x));
                r += y * y;
                a += x * x;
            }
        }
        if (all) *all = std::sqrt(a / (blocks * 64.0));
        return std::sqrt(r / (blocks * 64.0));
    }
};

// The defining behaviour, for one setting: rattle at the start (0.1 to 0.4 s),
// once settled (2.6 to 3 s), right after a figure change (3 to 3.3 s), once
// that has settled (6.6 to 7 s; a last grain can take three seconds) and
// after a new note on the settled plate (7 to 7.3 s).
struct Story { double start, settled, changed, resettled, legato, renewed; };
Story story(i32* p, double hz = 110) {
    Note n(hz);
    Meter m;
    Story s;
    m.rattle(n, p, kSecond / 10);
    s.start = m.rattle(n, p, kSecond * 3 / 10);
    m.rattle(n, p, kSecond * 22 / 10);
    s.settled = m.rattle(n, p, kSecond * 4 / 10);
    const i32 figure = p[kFigure];
    p[kFigure] = word(pick(7, 12));
    s.changed = m.rattle(n, p, kSecond * 3 / 10);
    m.rattle(n, p, kSecond * 33 / 10);
    s.resettled = m.rattle(n, p, kSecond * 4 / 10);
    // A note started while this one sounds takes over its settled sand.
    sand::start(n.voice, n.shared, 1);
    s.legato = m.rattle(n, p, kSecond * 3 / 10);
    m.rattle(n, p, kSecond * 2);
    // Once nothing sounds, a new note pours fresh sand.
    sand::destroy(n.voice, n.shared, 1);
    sand::start(n.voice, n.shared, 1);
    s.renewed = m.rattle(n, p, kSecond * 3 / 10);
    p[kFigure] = figure;
    check("bounded through the story", n.bounded);
    return s;
}
}

int main() {
    i32 p[8];

    // The rattle falls as the sand settles, and a figure change or a new
    // note makes it rise again.
    defaults(p);
    p[kGrain] = 0;  // dust: its clicks sit well above the 110 Hz note's modes
    const Story s = story(p);
    std::printf("rattle RMS: start %.5f, settled %.5f, new figure %.5f, re-settled %.5f, legato note %.5f, new note %.5f\n",
                s.start, s.settled, s.changed, s.resettled, s.legato, s.renewed);
    check("the rattle is clearly there at first (above -46 dBFS)", s.start > 0.005);
    check("the rattle dies as the sand settles (20 dB down)", s.settled < s.start * 0.1);
    check("a new figure kicks the sand up again (20 dB up)", s.changed > s.settled * 10);
    check("the new figure settles too", s.resettled < s.changed * 0.1);
    check("a legato note keeps the settled sand (10 dB below a fresh pour)", s.legato < s.renewed * 0.3);
    check("a new note with nothing sounding pours fresh sand", s.renewed > s.resettled * 10);

    // Every grain size rattles and settles (above 2.5 kHz even pebbles knock a little).
    for (unsigned g = 0; g <= 4; ++g) {
        defaults(p);
        p[kGrain] = word(g / 4.0);
        Note n(110);
        Meter m;
        m.rattle(n, p, kSecond / 10);
        const double start = m.rattle(n, p, kSecond * 3 / 10);
        m.rattle(n, p, kSecond * 22 / 10);
        const double settled = m.rattle(n, p, kSecond * 4 / 10);
        char what[96];
        std::snprintf(what, sizeof what, "grain %u/4 rattles, then settles (%.5f to %.5f)", g, start, settled);
        check(what, start > settled * 3 && start > 0.0005);
    }

    // No sand, no rattle: a pure bowed plate.
    defaults(p);
    p[kGrain] = 0;
    p[kSand] = 0;
    {
        Note n(110);
        Meter m;
        double all = 0;
        m.rattle(n, p, kSecond / 10);
        const double r = m.rattle(n, p, kSecond * 3 / 10, &all);
        check("no sand makes no rattle", r < s.start * 0.03);
        check("no sand still bows the plate", all > 0.02);
    }

    // Settle: slippery sand rattles longer than sticky sand.
    double lingering[2];
    for (unsigned k = 0; k < 2; ++k) {
        defaults(p);
        p[kGrain] = 0;
        p[kSettle] = word(k ? 1.0 : 0.0);
        Note n(110);
        Meter m;
        m.rattle(n, p, kSecond * 6 / 10);
        lingering[k] = m.rattle(n, p, kSecond * 6 / 10);
    }
    check("slippery sand still rattles after sticky sand has settled (0.6 to 1.2 s)", lingering[0] > lingering[1] * 3);

    // Shake keeps the sand dancing; tilt keeps some of it running downhill.
    {
        defaults(p);
        p[kGrain] = 0;
        p[kShake] = word(0.6);
        Note n(110);
        Meter m;
        m.rattle(n, p, kSecond * 2);
        const double shaken = m.rattle(n, p, kSecond / 2);
        check("shake keeps the sand rattling", shaken > s.settled * 10);
    }

    // Bounded within the declared peak at every extreme, at every pitch.
    {
        const double pitches[5] = {20, 110, 880, 4000, 16000};
        bool all_bounded = true, all_sound = true;
        for (unsigned corner = 0; corner < 256; corner += 5) {
            for (unsigned c = 0; c < 8; ++c) p[c] = (corner >> c & 1) ? 0x7fffffff : 0;
            p[kBow] = 0x7fffffff;
            for (double hz : pitches) {
                Note n(hz);
                double peak = 0;
                for (unsigned b = 0; b < kSecond / 2; ++b) {
                    n.block(p);
                    for (unsigned i = 0; i < opxy::block_frames; ++i) peak = std::fmax(peak, std::fabs(n.out[i] / 2147483648.0));
                }
                all_bounded = all_bounded && n.bounded;
                if (hz < 10000) all_sound = all_sound && peak > 0.01;
            }
        }
        check("bounded at every corner of the controls and pitch", all_bounded);
        check("every corner below 10 kHz sounds", all_sound);
    }

    // Sustains while bowed, at every figure and material.
    for (unsigned plate = 0; plate < 3; ++plate)
        for (unsigned f = 0; f < 12; ++f) {
            defaults(p);
            p[kFigure] = word(pick(f, 12));
            p[kPlate] = word(pick(plate, 3));
            Note n(220);
            Meter m;
            double early = 0, late = 0;
            m.rattle(n, p, kSecond / 2, &early);
            m.rattle(n, p, kSecond * 3, &late);
            char what[64];
            std::snprintf(what, sizeof what, "figure %u, plate %u sustains", f, plate);
            check(what, late > early * 0.7 && late > 0.02 && n.bounded);
        }

    // Struck (Bow 0): the plate rings like a bell and decays; even a legato
    // note's strike throws the settled sand.
    {
        defaults(p);
        p[kBow] = 0;
        p[kGrain] = 0;
        Note n(220);
        Meter m;
        double early = 0, late = 0;
        m.rattle(n, p, kSecond / 4, &early);
        m.rattle(n, p, kSecond * 2);
        const double quiet = m.rattle(n, p, kSecond / 2, &late);
        sand::start(n.voice, n.shared, 1);
        const double again = m.rattle(n, p, kSecond / 4);
        check("a struck plate rings, then decays", early > 0.02 && late < early * 0.5);
        check("a struck legato note throws the settled sand", again > quiet * 5);
    }

    // Pitch: the bowed mode sounds the note. The strongest component within
    // 3% of the note, by a fine sweep of single-bin DFTs over three seconds of
    // the settled, sand-free tone (long enough to part the bowed mode from its twin), lies within 3 cents of it.
    for (double hz : {65.41, 110.0, 220.0, 440.0, 1046.5}) {
        defaults(p);
        p[kSand] = 0;
        Note n(hz);
        for (unsigned b = 0; b < kSecond; ++b) n.block(p);
        std::vector<double> x;
        for (unsigned b = 0; b < kSecond * 3; ++b) {
            n.block(p);
            for (unsigned i = 0; i < opxy::block_frames; ++i) x.push_back(n.out[i] / 2147483648.0);
        }
        double best = 0, strongest = 0;
        for (int k = -60; k <= 60; ++k) {
            const double f = hz * (1 + k * 0.0002), w = 2 * M_PI * f / 44100;
            double re = 0, im = 0;
            for (size_t i = 0; i < x.size(); ++i) {
                const double win = 0.5 - 0.5 * std::cos(2 * M_PI * i / x.size());
                re += x[i] * win * std::cos(w * i);
                im -= x[i] * win * std::sin(w * i);
            }
            if (re * re + im * im > strongest) { strongest = re * re + im * im; best = f; }
        }
        const double cents = 1200 * std::log2(best / hz);
        char what[64];
        std::snprintf(what, sizeof what, "%.1f Hz plays %+.1f cents off", hz, cents);
        check(what, std::fabs(cents) < 3);
    }

    // Sand weighs on the plate: pebbles on moving ground flatten the bowed
    // mode, and it rises back toward the note as they settle on the nodes.
    {
        defaults(p);
        p[kGrain] = 0x7fffffff;
        p[kSand] = 0x7fffffff;
        Note n(220);
        for (unsigned b = 0; b < 8; ++b) n.block(p);
        const u32 base = sand::times(n.pitch[0], sand::kFigureModes[2][0].ratio);
        const i32 early = static_cast<i32>(sand::times(base, static_cast<u32>(std::min(1311, n.voice.load[0] * (96 + 288) >> 8 >> 11))));
        for (unsigned b = 0; b < kSecond * 3; ++b) n.block(p);
        const i32 late = static_cast<i32>(sand::times(base, static_cast<u32>(std::min(1311, n.voice.load[0] * (96 + 288) >> 8 >> 11))));
        std::printf("bowed mode flattened by %.1f cents at first, %.1f cents settled\n",
                    -1200 * std::log2(1 - early / static_cast<double>(base)), -1200 * std::log2(1 - late / static_cast<double>(base)));
        check("sand on moving ground flattens the bowed mode more than settled sand", early > late * 2 && early > 0);
    }


    // Level, matched to Hexagon's synths: a held middle C at the defaults sits
    // within 2 dB of Hollow's Choir (-26.5 dBFS RMS), and an eight-note chord
    // on every preset peaks between 0.4 and 0.8 of full scale.
    {
        defaults(p);
        Note n(261.63);
        Meter m;
        double all = 0;
        m.rattle(n, p, kSecond * 2);
        m.rattle(n, p, kSecond * 2, &all);
        const double level = 20 * std::log10(all);
        char what[80];
        std::snprintf(what, sizeof what, "a held middle C sits near Hexagon's level (%.1f dBFS)", level);
        check(what, std::fabs(level + 26.5) < 2);
    }
    struct Preset { const char* name; double values[8]; };
    const Preset presets[] = {
        {"Chladni", {0.2083, 0.6, 0.6, 0.3, 0.1667, 0.5, 0, 0}},
        {"Glass Pad", {0.125, 0.55, 0.3, 0.08, 0.5, 0.7, 0, 0}},
        {"Bowed Metal", {0.4583, 0.75, 0.45, 0.4, 0.1667, 0.55, 0, 0}},
        {"Bell Keys", {0.0417, 0.06, 0.35, 0.12, 0.1667, 0.6, 0, 0}},
        {"Sand Shaker", {0.5417, 0, 0.85, 0.45, 0.8333, 0.75, 0, 0}},
        {"Drone", {0.9583, 0.85, 0.5, 0.1, 0.1667, 0.25, 0, 0.18}},
        {"Hourglass", {0.7083, 0.6, 0.8, 0.15, 0.1667, 0.4, 0.6, 0}},
        {"Gravel", {0.2917, 0.7, 1, 0.85, 0.1667, 0.35, 0, 0}}
    };
    for (const Preset& preset : presets) {
        for (unsigned c = 0; c < 8; ++c) p[c] = word(preset.values[c]);
        const double chord[8] = {65.41, 98.0, 130.81, 164.81, 196.0, 246.94, 293.66, 329.63};
        std::vector<Note*> notes;
        for (unsigned v = 0; v < 8; ++v) notes.push_back(new Note(chord[v], v + 1));
        double peak = 0;
        for (unsigned b = 0; b < kSecond * 3; ++b) {
            double sum[opxy::block_frames] = {};
            for (Note* n : notes) {
                n->block(p);
                for (unsigned i = 0; i < opxy::block_frames; ++i) sum[i] += n->out[i] / 2147483648.0;
            }
            for (unsigned i = 0; i < opxy::block_frames; ++i) peak = std::fmax(peak, std::fabs(sum[i]));
        }
        for (Note* n : notes) delete n;
        char what[96];
        std::snprintf(what, sizeof what, "an eight-note chord of %s peaks between 0.4 and 0.8 (%.2f)", preset.name, peak);
        std::printf("%s chord peak %.2f\n", preset.name, peak);
        check(what, peak >= 0.4 && peak <= 0.8);
    }

    if (!failures) std::puts("Sand: all sound checks pass");
    return failures ? 1 : 0;
}
