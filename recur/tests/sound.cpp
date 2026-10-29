// Recur's own checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../sample.cpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
using opxy::i32;
using opxy::u32;
const u32 c4 = 25480119u, frames = 12 * 44100, slice = 5512, pass = 88200;   // a 1/16 at 120 BPM is 5512.5 samples
// Where slot n of the window starts, in the recording and in the output: 1/16s at 120 BPM.
u32 slot_start(u32 n) { return n * 11025 / 2; }
opxy::i16 voice_like[frames], square[frames], dc[frames], sine[frames];
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}
// A control at 0..1.
i32 at(double value) { return i32(value * 2147483647.0); }
// The middle of choice n of an enum with count choices.
i32 choice_of(u32 n, u32 count) { return at((n + 0.5) / count); }
// step at iteration k of a period: step runs from iteration 0 to the last.
i32 step_at(u32 k, u32 period) { return at((k + 0.005) / (period - 1)); }
// Enum choices by name.
enum { grid2, grid4, grid8 };
enum { hiss, s64, s32, s16, s8, s4 };
enum { off, bars4, bars2, bar, half_note, quarter, eighth, sixteenth };

struct Rig {
    i32 out[opxy::block_frames];
    i32 parameters[8];
    u32 pitch[opxy::block_frames];
    opxy::SampleBlock block;
    recur::Voice voice;
    std::vector<i32> heard;
    Rig(const opxy::i16* data, u32 count) {
        // definition.json's defaults: 4 x 4 slices of 1/16, the cat, an iteration a bar.
        const double defaults[8] = {0, 0.5, 0.583333, 0.083333, 0, 0.4375, 0.7, 0.071429};
        for (u32 i = 0; i < 8; ++i) parameters[i] = at(defaults[i]);
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = c4;
        block = opxy::SampleBlock();
        block.output = out; block.parameters = parameters; block.pitch = pitch;
        block.region.begin = 0; block.region.end = count; block.region.level = opxy::sample_unity_level;
        block.transport.bpm_x10 = 1200;
        block.output_channels = 1;
        const opxy::SampleView view = {data, count, 44100, 1};
        recur::initialize(voice, view);
        recur::start(voice);
    }
    void set(u32 control, u32 n, u32 count) { parameters[control] = choice_of(n, count); }
    // Renders blocks, keeping what it heard; returns the peak magnitude.
    i32 run(u32 blocks) {
        i32 peak = 0;
        for (u32 b = 0; b < blocks; ++b) {
            recur::render_sample(voice, block);
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                heard.push_back(out[i]);
                const i32 m = out[i] < 0 ? -out[i] : out[i];
                if (m > peak) peak = m;
            }
        }
        return peak;
    }
    // The largest change from one sample to the next over what it heard from first.
    i32 largest_jump(u32 first) const {
        i32 most = 0;
        for (u32 i = first + 1; i < heard.size(); ++i) {
            const i32 d = heard[i] > heard[i - 1] ? heard[i] - heard[i - 1] : heard[i - 1] - heard[i];
            if (d > most) most = d;
        }
        return most;
    }
};

// What a source sample sounds as from one reader at full level, on each path.
i32 played(opxy::i16 v) { return ((v * 32767) >> 1) * 2; }
i32 hissed(opxy::i16 v) { return ((v * 32766) >> 1) * 2; }

// Spectral flatness (geometric over arithmetic mean of power) of n samples,
// Welch-averaged over 1024-sample Hann segments: near 1 for white noise.
double flatness(const i32* x, u32 n) {
    const u32 len = 1024, half = len / 2;
    std::vector<double> power(half, 0.0);
    for (u32 start = 0; start + len <= n; start += len / 2)
        for (u32 k = 1; k < half; ++k) {
            double re = 0, im = 0;
            for (u32 i = 0; i < len; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / len), v = x[start + i] * w;
                re += v * std::cos(2 * M_PI * k * i / len);
                im -= v * std::sin(2 * M_PI * k * i / len);
            }
            power[k] += re * re + im * im;
        }
    double logs = 0, sum = 0;
    for (u32 k = 1; k < half; ++k) { logs += std::log(power[k] + 1e-9); sum += power[k]; }
    return std::exp(logs / (half - 1)) / (sum / (half - 1));
}
}

int main() {
    // A voice-like source: a 140 Hz pulse train's harmonics under two formants,
    // with a slow vibrato. Its spectrum is all peaks.
    double phase = 0;
    for (u32 i = 0; i < frames; ++i) {
        const double t = i / 44100.0, f0 = 140 * (1 + 0.02 * std::sin(2 * M_PI * 5 * t));
        phase += 2 * M_PI * f0 / 44100;
        double v = 0;
        for (u32 h = 1; h <= 30; ++h) {
            const double fh = h * f0;
            const double formants = std::exp(-std::pow((fh - 700) / 250, 2)) + 0.5 * std::exp(-std::pow((fh - 1200) / 300, 2)) + 0.02;
            v += formants * std::sin(h * phase);
        }
        voice_like[i] = opxy::i16(3500 * v);   // v stays within +-9
        square[i] = (i / 50) & 1 ? 32767 : -32768;
        dc[i] = 12000;
        sine[i] = opxy::i16(20000 * std::sin(2 * M_PI * 220.0 * i / 44100));
    }

    // --- The maps ---------------------------------------------------------------
    {   // The periods are the maps' own: the first iteration that returns every cell.
        for (u32 map = 0; map < recur::map_count; ++map)
            for (u32 bits = 1; bits <= 7; ++bits) {
                const u32 n = 1u << bits;
                const recur::Mat one = recur::power(map, 1, bits);
                std::vector<u32> xs(n * n), ys(n * n);
                for (u32 c = 0; c < n * n; ++c) { xs[c] = c % n; ys[c] = c / n; }
                u32 k = 0;
                bool home = false;
                while (!home && k < 1000) {
                    ++k;
                    home = true;
                    for (u32 c = 0; c < n * n; ++c) {
                        recur::send(one, map, bits, xs[c], ys[c]);
                        home = home && xs[c] == c % n && ys[c] == c / n;
                    }
                }
                check(k == recur::period(map, bits), "each map's period is the first full return");
            }
    }
    {   // power(k) is the map applied k times, and every map is a permutation.
        for (u32 map = 0; map < recur::map_count; ++map)
            for (u32 bits : {3u, 6u}) {
                const u32 n = 1u << bits;
                const recur::Mat one = recur::power(map, 1, bits);
                for (u32 k = 0; k < recur::period(map, bits); k += 3) {
                    const recur::Mat m = recur::power(map, k, bits);
                    std::vector<bool> seen(n * n, false);
                    bool same = true, onto = true;
                    for (u32 c = 0; c < n * n; ++c) {
                        u32 x = c % n, y = c / n, px = x, py = y;
                        for (u32 i = 0; i < k; ++i) recur::send(one, map, bits, px, py);
                        recur::send(m, map, bits, x, y);
                        same = same && x == px && y == py;
                        onto = onto && !seen[y * n + x];
                        seen[y * n + x] = true;
                    }
                    check(same, "power(k) is k applications of the map");
                    check(onto, "every iteration is a permutation of the square");
                }
            }
    }
    {   // depth: none move at 0, every cell at 1, and between about depth's
        // share of the cells; whole orbits stay, so each iteration is still a
        // permutation. step's fraction puts a share of the moving orbits an
        // iteration ahead, still a permutation.
        bool still = true, every = true, permutes = true, shares = true, none_ahead = true;
        for (u32 map = 0; map < recur::map_count; ++map)
            for (u32 bits = 1; bits <= 3; ++bits) {
                const u32 n = 1u << bits, cells = n * n, period = recur::period(map, bits);
                recur::Orbits none, all, some;
                recur::rank_orbits(map, bits, 0, 0, none);
                recur::rank_orbits(map, bits, 65536, 0, all);
                recur::rank_orbits(map, bits, 32768, 32768, some);
                for (u32 c = 0; c < cells; ++c) {
                    still = still && !recur::moves(none.moving, c);
                    every = every && recur::moves(all.moving, c);
                    none_ahead = none_ahead && !recur::moves(all.ahead, c);
                }
                // At 8 a side the orbits are small enough for the share to land within a sixth.
                if (bits == 3) shares = shares && some.moving_cells * 6 >= cells * 2 && some.moving_cells * 6 <= cells * 4;
                for (u32 k = 0; k < period; ++k) {
                    const recur::Mat m = recur::power(map, k, bits), m1 = recur::power(map, (k + 1) % period, bits);
                    std::vector<bool> seen(cells, false);
                    for (u32 c = 0; c < cells; ++c) {
                        u32 x = c % n, y = c / n;
                        if (recur::moves(some.moving, c)) recur::send(recur::moves(some.ahead, c) ? m1 : m, map, bits, x, y);
                        permutes = permutes && !seen[y * n + x];
                        seen[y * n + x] = true;
                    }
                }
            }
        check(still && every, "depth 0 holds every cell and depth 1 moves them all");
        check(shares, "depth at half moves about half the cells");
        check(none_ahead, "a whole step puts no orbit ahead");
        check(permutes, "at any depth and step each iteration is a permutation");
    }

    // --- The slice path ---------------------------------------------------------
    {   // Unscrambled at the root key, the recording plays exactly: each slice
        // follows on from the last with no crossfade, up to the window's wrap.
        Rig r(voice_like, frames);
        r.set(recur::kRate, off, recur::rate_count);
        r.run(pass / opxy::block_frames + 2);   // 4 x 4 slices of 1/16 at 120 BPM
        bool exact = true;
        for (u32 i = 0; i < pass; ++i) exact = exact && r.heard[i] == played(voice_like[i]);
        check(exact, "the unscrambled window is the recording, bit for bit");
    }
    {   // Scrambled, each slot plays a slice of the recording exactly, after its
        // crossfade in: the slice the map put there.
        for (u32 map = 0; map < recur::map_count; ++map) {
            Rig r(voice_like, frames);
            r.set(recur::kRate, off, recur::rate_count);
            r.set(recur::kMap, map, recur::map_count);
            r.parameters[recur::kDepth] = at(1);
            r.parameters[recur::kStep] = step_at(1, recur::period(map, 2));   // iteration 1
            r.run(pass / opxy::block_frames + 1);
            const recur::Mat g = recur::power(map, recur::period(map, 2) - 1, 2);
            bool exact = true, moved = false;
            for (u32 slot = 1; slot < 16; ++slot) {
                u32 x = slot % 4, y = slot / 4;
                recur::send(g, map, 2, x, y);
                const u32 source = y * 4 + x;
                moved = moved || source != slot;
                for (u32 i = 256; i < slice; ++i)
                    exact = exact && r.heard[slot_start(slot) + i] == played(voice_like[slot_start(source) + i]);
            }
            check(exact, "each slot plays the slice the map put there, exactly");
            check(moved, "and some slices have moved");
        }
    }
    {   // It comes back: a new iteration each bar (each pass of 4 x 4 slices of
        // 1/16), and the cat's period on 4 x 4 is 3, so the fourth pass is the
        // first again, bit for bit after its first crossfade.
        Rig r(voice_like, frames);
        r.parameters[recur::kDepth] = at(1);
        r.run(4 * pass / opxy::block_frames + 2);
        bool same = true, differs = false;
        for (u32 i = 256; i < pass; ++i) same = same && r.heard[3 * pass + i] == r.heard[i];
        for (u32 i = 256; i < pass; ++i) differs = differs || r.heard[pass + i] != r.heard[i];
        check(same, "after the period the pass is the original, bit for bit");
        check(differs, "between, it is re-sequenced");
    }
    {   // rate off holds the iteration step chooses: every pass plays the same.
        Rig r(voice_like, frames);
        r.set(recur::kRate, off, recur::rate_count);
        r.parameters[recur::kStep] = at(0.5);
        r.run(3 * pass / opxy::block_frames + 2);
        bool same = true;
        for (u32 i = 256; i < pass; ++i) same = same && r.heard[i + pass] == r.heard[i + 2 * pass];
        check(same && r.voice.iteration == 0, "rate off holds the iteration");
    }
    {   // The slices keep time: at 60 BPM a 1/16 slice is twice as long.
        Rig fast(voice_like, frames), slow(voice_like, frames);
        slow.block.transport.bpm_x10 = 600;
        fast.run(600);
        slow.run(600);
        check(fast.voice.slot == 6 && slow.voice.slot == 3, "slices follow the project's tempo");
    }
    {   // ...and don't drift: ten bars on, the eleventh starts on sample 882000.
        Rig r(voice_like, frames);
        r.run(10 * pass / opxy::block_frames + 1);   // 882048 samples
        check(r.voice.slot == 0 && r.voice.into == 48, "slices stay on the tempo's grid");
    }
    {   // Changes don't click. On a constant the map changes nothing, so any
        // step in the output is the crossfades' own: they stay smooth through
        // iterations, steps, chords, grids, maps, slices and the hiss path.
        Rig r(dc, frames);
        r.set(recur::kRate, sixteenth, recur::rate_count);
        r.run(300);
        const u32 from = 64 * 4;
        r.parameters[recur::kStep] = at(0.6); r.run(40);
        r.set(recur::kChrd, 4, recur::chord_count); r.run(40);
        r.set(recur::kMap, recur::kBaker, recur::map_count); r.run(40);
        r.set(recur::kGrid, grid8, recur::grid_count); r.run(40);
        r.set(recur::kSlice, s64, recur::slice_count); r.run(200);
        r.set(recur::kSlice, hiss, recur::slice_count); r.run(200);
        r.set(recur::kGrid, grid2, recur::grid_count); r.run(200);
        r.set(recur::kSlice, s8, recur::slice_count); r.run(200);
        const i32 level = 12000 << 15;
        const i32 jump = r.largest_jump(from);
        std::printf("largest step on a constant: %.2f%% of its level\n", 100.0 * jump / level);
        check(jump < level / 40, "every change ramps smoothly");
    }
    {   // The seams between slices are crossfades: a 220 Hz sine, scrambled
        // into 1/64 slices, never moves faster than twice its own steepest.
        Rig r(sine, frames);
        r.set(recur::kSlice, s64, recur::slice_count);
        r.set(recur::kGrid, grid8, recur::grid_count);
        r.set(recur::kRate, sixteenth, recur::rate_count);
        r.parameters[recur::kDepth] = at(1);
        for (u32 map = 0; map < recur::map_count; ++map) {
            r.set(recur::kMap, map, recur::map_count);
            r.run(400);
        }
        const double steepest = 20000.0 * (1 << 15) * 2 * M_PI * 220 / 44100;
        std::printf("steepest seam: %.2f of the sine's own\n", r.largest_jump(64) / steepest);
        check(r.largest_jump(64) < steepest * 2, "slice seams don't click, on any map");
    }

    // --- The hiss path ------------------------------------------------------------
    {   // Each map's player reads, frozen at an iteration, what the map put in
        // each cell (up to the loop's seam at the end of the pass).
        for (u32 map = 0; map < recur::map_count; ++map) {
            Rig r(voice_like, frames);
            const u32 bits = 6, n = 64, period = recur::period(map, bits), k = period / 3 + 1;
            r.set(recur::kSlice, hiss, recur::slice_count);
            r.set(recur::kRate, off, recur::rate_count);
            r.set(recur::kMap, map, recur::map_count);
            r.parameters[recur::kDepth] = at(1);
            r.parameters[recur::kStep] = step_at(k, period);
            r.run(4096 / opxy::block_frames);
            const recur::Mat g = recur::power(map, (period - k) % period, bits);
            bool same = true;
            for (u32 j = 0; j < 4096 - 512; ++j) {
                u32 x = j % n, y = j / n;
                recur::send(g, map, bits, x, y);
                same = same && r.heard[j] == hissed(voice_like[y * n + x]);
            }
            check(same, "hiss: each map's player gathers what the map put in each cell");
        }
    }
    {   // hiss comes back too: frozen at iteration 0, and moved once round the
        // period (24 on 32 x 32) at 8 iterations a second, it plays the same.
        Rig still(voice_like, frames), moving(voice_like, frames);
        for (Rig* r : {&still, &moving}) {
            r->set(recur::kSlice, hiss, recur::slice_count);
            r->set(recur::kGrid, grid2, recur::grid_count);
            r->parameters[recur::kDepth] = at(1);
        }
        still.set(recur::kRate, off, recur::rate_count);
        moving.set(recur::kRate, sixteenth, recur::rate_count);   // 8 iterations a second at 120 BPM
        const u32 blocks = (24 * 44100 / 8 + 2048) / opxy::block_frames;
        still.run(blocks + 20);   // within iteration 0, before its crossfade into 1
        moving.run(blocks + 20);
        check(moving.voice.taps[0].ka == 0 && moving.voice.taps[0].w == 0, "hiss: rate brings the voice round to iteration 0");
        bool same = true, differed = false;
        for (u32 i = blocks * opxy::block_frames; i < still.heard.size(); ++i) same = same && still.heard[i] == moving.heard[i];
        for (u32 i = 12 * 44100 / 8; i < 12 * 44100 / 8 + 2048; ++i) differed = differed || still.heard[i] != moving.heard[i];
        check(same, "hiss: after the period it plays the original bit for bit");
        check(differed, "hiss: between, it plays something else");
    }
    {   // hiss mid-period is close to noise: far flatter than the original. Not
        // white: a linear map moves the square's 2-D Fourier modes rather than
        // destroying them. (At half the period the cat map on 2^m cells
        // multiplies each by N/2 + 1: half the window plays in order.)
        const u32 period = 48, size = 4096;
        auto heard_at = [&](u32 k) {
            Rig r(voice_like, frames);
            r.set(recur::kSlice, hiss, recur::slice_count);
            r.set(recur::kRate, off, recur::rate_count);
            r.parameters[recur::kDepth] = at(1);
            r.parameters[recur::kStep] = step_at(k, period);
            r.run(2 * size / opxy::block_frames);
            return std::vector<i32>(r.heard.begin() + size, r.heard.begin() + 2 * size);
        };
        const std::vector<i32> original = heard_at(0);
        const double flat0 = flatness(original.data(), size);
        double least = 1;
        for (u32 k : {6u, 12u, 17u, 30u, 40u}) {
            const std::vector<i32> mid = heard_at(k);
            const double f = flatness(mid.data(), size);
            least = f < least ? f : least;
        }
        std::printf("hiss flatness: original %.5f, mid-period at least %.3f\n", flat0, least);
        check(flat0 < 0.01 && least > 0.05 && least > 50 * flat0, "hiss: mid-period the spectrum is far flatter");
    }

    {   // hiss's depth lays the plain window under the scramble: at 0 the
        // window plays as it is, whatever the iteration.
        Rig r(voice_like, frames);
        r.set(recur::kSlice, hiss, recur::slice_count);
        r.set(recur::kRate, off, recur::rate_count);
        r.parameters[recur::kDepth] = 0;
        r.parameters[recur::kStep] = at(0.4);
        r.run(64);
        bool same = true;
        for (u32 i = 0; i < 4096 - 512; ++i) same = same && r.heard[i] == played(voice_like[i]);
        check(same, "hiss at depth 0 plays the window unscrambled");
    }

    // --- Both ---------------------------------------------------------------------
    {   // No sample, or one too short: silence.
        Rig r(voice_like, 0);
        check(r.run(20) == 0, "silence without a sample");
        Rig tiny(voice_like, 40);
        check(tiny.run(20) == 0, "silence from a region too short to play");
    }
    {   // A sample shorter than the window: slices wrap through it; hiss plays
        // the largest square that fits.
        Rig r(voice_like, 3000);
        check(r.run(200) > 0x01000000, "a short sample sounds in slices");
        Rig h(voice_like, 3000);
        h.set(recur::kSlice, hiss, recur::slice_count);
        h.set(recur::kGrid, grid8, recur::grid_count);
        check(h.run(200) > 0x01000000 && h.voice.bits == 5, "hiss plays the largest square that fits");
    }
    {   // Every map, grid, slice and chord on a full-scale square: within half scale.
        for (u32 map = 0; map < recur::map_count; ++map)
            for (u32 slice = 0; slice < recur::slice_count; slice += 2)
                for (u32 chord = 0; chord < recur::chord_count; chord += 3) {
                    Rig r(square, frames);
                    r.set(recur::kMap, map, recur::map_count);
                    r.set(recur::kSlice, slice, recur::slice_count);
                    r.set(recur::kChrd, chord, recur::chord_count);
                    r.set(recur::kGrid, grid8, recur::grid_count);
                    r.set(recur::kRate, sixteenth, recur::rate_count);
                    r.parameters[recur::kDepth] = at(1);
                    const i32 peak = r.run(300);
                    check(peak > 0 && peak <= 0x40000000, "every map, slice and chord stays within half scale");
                }
    }
    {   // The note's pitch and chrd: an octave up doubles the reader's step,
        // and maj stacks readers a third and a fifth up.
        Rig r(voice_like, frames);
        for (u32 i = 0; i < opxy::block_frames; ++i) r.pitch[i] = 2 * c4;
        r.set(recur::kChrd, 4, recur::chord_count);
        r.run(2);
        check(r.voice.steps[0] == 131072, "an octave up doubles the step");
        const double third = 131072 * std::pow(2.0, 4 / 12.0), fifth = 131072 * std::pow(2.0, 7 / 12.0);
        check(std::fabs(r.voice.steps[1] - third) < third * 0.001, "maj's second reader is a major third up");
        check(std::fabs(r.voice.steps[2] - fifth) < fifth * 0.001, "maj's third reader is a fifth up");
    }
    {   // The sample's volume: half its level halves the output.
        Rig loud(voice_like, frames), quiet(voice_like, frames);
        quiet.block.region.level = opxy::sample_unity_level / 2;
        const i32 a = loud.run(100), b = quiet.run(100);
        check(b > a / 2 - a / 50 && b < a / 2 + a / 50, "the sample's volume scales the output");
    }
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
