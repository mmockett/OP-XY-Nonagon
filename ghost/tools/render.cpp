// Renders a short phrase on every preset as stereo WAVs, for headphones:
//   clang++ -std=c++11 -O2 -I<sdk>/sdk/include tools/render.cpp -o render && ./render <folder>
// Notes sound together through Ghost as on the device (tests/analysis.h's
// play: each a voice, sharing the line pool), under a simple envelope
// standing in for the OP-XY's, at the engine's own level.
#include "../tests/analysis.h"
#include <cstdio>
#include <string>

namespace {
using namespace measure;

void write_wav(const std::string& path, const std::vector<double>& left, const std::vector<double>& right) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); return; }
    const u32 frames = u32(left.size()), bytes = frames * 4;
    auto u32le = [&](u32 v) { std::fputc(v & 255, f); std::fputc((v >> 8) & 255, f); std::fputc((v >> 16) & 255, f); std::fputc(v >> 24, f); };
    auto u16le = [&](u32 v) { std::fputc(v & 255, f); std::fputc((v >> 8) & 255, f); };
    std::fwrite("RIFF", 1, 4, f); u32le(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32le(16); u16le(1); u16le(2); u32le(44100); u32le(44100 * 4); u16le(4); u16le(16);
    std::fwrite("data", 1, 4, f); u32le(bytes);
    for (u32 i = 0; i < frames; ++i) {
        u16le(u32(i32(std::lround(std::fmax(-1.0, std::fmin(1.0, left[i])) * 32767)) & 0xffff));
        u16le(u32(i32(std::lround(std::fmax(-1.0, std::fmin(1.0, right[i])) * 32767)) & 0xffff));
    }
    std::fclose(f);
    const std::vector<double> m = fold(left, right);
    std::printf("%-28s %5.1f s  peak %6.1f dBFS  RMS %6.1f dBFS  mono %+5.1f dB\n", path.substr(path.rfind('/') + 1).c_str(),
                frames / 44100.0, db(std::fmax(peak(left), peak(right))),
                10 * std::log10((rms(left) * rms(left) + rms(right) * rms(right)) / 2),
                db(rms(m)) - 10 * std::log10((rms(left) * rms(left) + rms(right) * rms(right)) / 2));
}

// Chords of (start beat, beats, notes...), at a tempo.
std::vector<Note> chords(const std::vector<std::vector<double>>& rows, double beat) {
    std::vector<Note> notes;
    for (const auto& row : rows)
        for (size_t k = 2; k < row.size(); ++k) notes.push_back({0.2 + row[0] * beat, row[1] * beat, row[k]});
    return notes;
}
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<double> left, right;
    struct Scene { const char* file; u32 preset; std::vector<Note> notes; double seconds, attack, release; };
    const double b = 0.6;   // a beat
    const std::vector<Scene> scenes = {
        // Seance: a slow minor progression, a pad.
        {"1-seance-pad.wav", 0, chords({{0, 4, 57, 60, 64}, {4, 4, 53, 57, 60}, {8, 4, 48, 55, 60, 64}, {12, 4, 55, 59, 62}}, b), 11.5, 0.4, 1.2},
        // Whisper: a slow line, close at the ears.
        {"2-whisper-line.wav", 1, chords({{0, 2, 62}, {2, 2, 65}, {4, 3, 64}, {7, 1, 60}, {8, 4, 62}}, b), 9, 0.05, 0.6},
        // Choir: open voicings, the voices spread round the head and circling.
        {"3-choir-chords.wav", 2, chords({{0, 4, 50, 57, 62, 65}, {4, 4, 46, 53, 62, 65}, {8, 4, 41, 53, 57, 60}, {12, 4, 48, 55, 60, 64}}, b), 11.5, 0.5, 1.5},
        // Revenant: a drone, beating between the ears.
        {"4-revenant-drone.wav", 3, chords({{0, 14, 38, 45}}, b), 11, 1.0, 2.0},
        // Wraith: long notes that circle the head quickly.
        {"5-wraith-circling.wav", 4, chords({{0, 6, 57}, {6, 6, 55}, {12, 4, 52}}, b), 11.5, 0.2, 1.0},
        // Skull: a melody whose fundamental exists only between the ears.
        {"6-skull-melody.wav", 5, chords({{0, 1, 60}, {1, 1, 62}, {2, 1, 63}, {3, 2, 67}, {5, 1, 65}, {6, 1, 63}, {7, 2, 62}, {9, 1, 63}, {10, 1, 58}, {11, 3, 60}}, b), 9.5, 0.03, 0.5},
        // Glass: an arpeggio, ringing.
        {"7-glass-arpeggio.wav", 6, chords({{0, 6, 64}, {1, 5, 67}, {2, 4, 71}, {3, 3, 76}, {6, 6, 62}, {7, 5, 69}, {8, 4, 72}, {9, 3, 78}}, b), 9.5, 0.01, 2.0},
        // Haunting: chords rung from a recorded voice (the bed).
        {"8-haunting-from-a-recording.wav", 7, chords({{0, 5, 45, 52, 57, 60}, {5, 5, 43, 50, 55, 59}, {10, 6, 41, 48, 57, 60}}, b), 11.5, 0.3, 1.2},
    };
    for (const Scene& s : scenes) {
        play(s.notes, presets[s.preset].values, s.seconds, left, right, presets[s.preset].values[7] > 0, s.attack, s.release);
        write_wav(dir + "/" + s.file, left, right);
    }
    // The recording itself, for comparison with the Haunting scene.
    const std::vector<i16>& rec = voice_recording();
    std::vector<double> l(rec.size()), r(rec.size());
    for (size_t i = 0; i < rec.size(); ++i) l[i] = r[i] = rec[i] / 32768.0;
    write_wav(dir + "/9-the-recording-itself.wav", l, r);
    return 0;
}
