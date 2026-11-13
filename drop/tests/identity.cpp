// Drop's output, sample for sample: a hash of every block over scenes that
// reach each path of the engine (floating and boiling, hops, every liquid,
// chords, a retuned and re-dropped voice, controls moving every block, low and
// high keys, a voice ringing out to silence). A change that should leave the
// sound alone must leave these hashes alone; a change to the sound updates
// them, and says so in its commit.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>

namespace {
using opxy::i32;
using opxy::u32;
const double rate = 44100;

i32 q31(double v) { return static_cast<i32>(v * 2147483647.0 + 0.5); }
u32 increment(double hz) { return static_cast<u32>(hz / rate * 4294967296.0 + 0.5); }
double key(double midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
u32 fnv(u32 h, i32 v) {
    for (int k = 0; k < 4; ++k) {
        h ^= (static_cast<u32>(v) >> (8 * k)) & 0xffu;
        h *= 16777619u;
    }
    return h;
}

enum Motion { still, vapour_sweep, glide, redrop, wander };
// heat, size, liquid, beat, groove, vapour, life, crackle
struct Scene {
    const char* name;
    double c[8];
    int midi;
    double seconds;
    int voices;
    Motion motion;
    u32 expected;
};
const Scene scenes[] = {
    {"defaults, C4", {0.5, 0.85, 0, 0.181, 0, 0.35, 0.55, 0.25}, 60, 8, 1, still, 0xa10a44fcu},
    {"floating fast beats, A4", {0.5, 1.0, 0, 1.0, 0, 0.4, 0.55, 0.3}, 69, 6, 1, still, 0xdd23ee1cu},
    {"boiling, C3", {0.2, 1.0, 0, 1.0, 0, 0.4, 0.55, 0.3}, 48, 2, 1, still, 0x86dcb744u},
    {"at the point, full crackle", {0.36, 0.6, 0, 0.5, 0, 0.6, 0.4, 1.0}, 55, 4, 1, still, 0xa071cf33u},
    {"ratchet hops, slow beats", {0.7, 1.0, 0, 0.3, 1.0, 0.3, 0.7, 0.6}, 52, 8, 1, still, 0x581834d0u},
    {"ethanol, thick vapour", {0.6, 0.7, 0.5, 0.6, 0.4, 1.0, 0.5, 0.5}, 72, 4, 1, still, 0x81a90b0du},
    {"nitrogen, high key", {0.9, 0.4, 1.0, 0.8, 0.2, 0.8, 0.4, 0.8}, 96, 3, 1, still, 0xd50cf281u},
    {"sphere, low key", {0.8, 0.0, 0, 0.5, 0, 0.1, 0.3, 0.2}, 24, 4, 1, still, 0x52861d06u},
    {"hot, no crackle, no vapour", {1.0, 0.5, 0, 0.7, 0, 0.0, 0.2, 0.0}, 64, 3, 1, still, 0xae89a6a2u},
    {"cold plate, bubbles", {0.0, 1.0, 0, 0.2, 0, 0.5, 1.0, 1.0}, 40, 2, 1, still, 0x0d6fb7abu},
    {"chord of five", {0.5, 0.85, 0, 0.4, 0.3, 0.4, 0.4, 0.4}, 50, 4, 5, still, 0x0cdbb182u},
    {"vapour swept each block", {0.55, 0.9, 0.2, 0.5, 0, 0.5, 0.6, 0.3}, 57, 4, 1, vapour_sweep, 0x2a027c62u},
    {"key gliding", {0.5, 0.85, 0, 0.6, 0, 0.35, 0.6, 0.25}, 45, 4, 2, glide, 0x923315b9u},
    {"re-dropped while ringing", {0.5, 0.9, 0, 0.7, 0.5, 0.45, 0.25, 0.5}, 62, 5, 2, redrop, 0xab66b75fu},
    {"every control wandering", {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5}, 67, 6, 3, wander, 0x7766af38u},
};
}

int main() {
    int failures = 0;
    for (const Scene& s : scenes) {
        static drop::Voice voices[8];
        static drop::Shared shared;
        shared.sounding = 0;
        double c[8];
        for (int k = 0; k < 8; ++k) c[k] = s.c[k];
        for (int v = 0; v < s.voices; ++v) {
            drop::initialize(voices[v], shared, static_cast<u32>(v + 1));
            drop::start(voices[v], shared, static_cast<u32>(v + 1));
        }
        u32 walk = 12345u;
        u32 h = 2166136261u;
        const unsigned blocks = static_cast<unsigned>(s.seconds * rate / 64);
        for (unsigned b = 0; b < blocks; ++b) {
            if (s.motion == vapour_sweep) c[drop::VAPOUR] = 0.5 + 0.5 * std::sin(b * 0.013);
            if (s.motion == wander) {
                for (int k = 0; k < 8; ++k) {
                    walk = walk * 1664525u + 1013904223u;
                    c[k] += ((walk >> 16) / 65536.0 - 0.5) * 0.04;
                    c[k] = c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k];
                }
            }
            if (s.motion == redrop && b % 300 == 299)
                for (int v = 0; v < s.voices; ++v) drop::start(voices[v], shared, static_cast<u32>(v + 1));
            i32 parameters[8];
            for (int k = 0; k < 8; ++k) parameters[k] = q31(c[k]);
            for (int v = 0; v < s.voices; ++v) {
                double midi = s.midi + 4 * v;
                if (s.motion == glide) midi += 12 * std::sin(b * 0.004);
                u32 pitch[64];
                for (int i = 0; i < 64; ++i) pitch[i] = increment(key(midi));
                i32 o[64];
                drop::render(voices[v], shared, static_cast<u32>(v + 1), o, parameters, pitch);
                for (int i = 0; i < 64; ++i) h = fnv(h, o[i]);
                h = fnv(h, static_cast<i32>(voices[v].quiet));
            }
        }
        const bool ok = h == s.expected;
        std::printf("%s %-30s 0x%08xu (expected 0x%08xu)\n", ok ? "ok  " : "FAIL", s.name, h, s.expected);
        if (!ok) ++failures;
    }
    return failures ? 1 : 0;
}
