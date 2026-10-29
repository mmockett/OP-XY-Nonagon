// Coin's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
//   - Let go, the contact rate rises monotonically along (1 - t/T)^(-n), then
//     stops abruptly when it reaches the ring.
//   - Kept spinning (spin at hold), the rate stays steady.
//   - Air changes the sweep's exponent from -1/3 to -1/6.
//   - The disk's ring tracks the key; many's disks stop at different moments.
//   - The output stays within the declared peak and falls silent.
// COIN_REPORT=1 also prints each control's measured effect at 0, 25, 50, 75
// and 100 (README.md's table).
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const double rate = 44100.0, block = 64.0;
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

opxy::u32 increment(double hz) { return static_cast<opxy::u32>(hz / rate * 4294967296.0 + 0.5); }
double hz_of(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
opxy::i32 q31(double v) { return static_cast<opxy::i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }

// One voice playing one note.
struct Player {
    coin::Voice voice;
    coin::Shared shared;
    opxy::i32 parameters[8];
    opxy::u32 pitch[64];
    opxy::i32 out[64];
    Player(const double (&c)[8], double hz) {
        for (int k = 0; k < 8; ++k) parameters[k] = q31(c[k]);
        for (int i = 0; i < 64; ++i) pitch[i] = increment(hz);
        coin::initialize(voice, shared, 1);
        coin::start(voice, shared, 1);
    }
    void play() { coin::render(voice, shared, 1, out, parameters, pitch); }
    // Disk k's contact cycles so far: whole strikes plus the phase.
    double cycles(int k) const {
        const coin::Disk& d = voice.disk[k];
        return d.strikes + (static_cast<double>(d.phase - coin::spin::offset[k]) / 4294967296.0);
    }
    double energy() const {
        double e = 0;
        for (int i = 0; i < 64; ++i) e += static_cast<double>(out[i]) * out[i];
        return e;
    }
};

// The settle time a spin control gives, from the engine's own step.
double settle_seconds(double spin) {
    return static_cast<double>(coin::spin::one) / coin::spin::life_step(q31(spin)) * block / rate;
}

// Plays a let-go spin and returns the contact rate block by block (Hz) until
// the disk stops; `stop` gets the block it stopped in.
int sweep(double air, double spin, double* rates, int most, int* stop, Player** keep = nullptr) {
    const double c[8] = {spin, 0.5, 0, 0, 0, air, 0, 0};
    static Player* p;
    p = new Player(c, 440.0);
    int b = 0;
    *stop = -1;
    double last = p->cycles(0);
    for (; b < most; ++b) {
        p->play();
        const double now = p->cycles(0);
        rates[b] = (now - last) * rate / block;
        last = now;
        if (!p->voice.disk[0].rolling) { *stop = b; ++b; break; }
    }
    if (keep) *keep = p; else delete p;
    return b;
}

void settles_along_the_law() {
    static double rates[40000];
    int stop = 0;
    Player* p = nullptr;
    const double spin = 0.5, T = settle_seconds(spin), held = 440.0 / 8;
    const int n = sweep(0, spin, rates, 40000, &stop, &p);
    check(stop > 0, "the disk never stopped");
    std::printf("settle: T %.3f s, stopped at %.3f s, held %.2f Hz, last %.1f Hz\n",
                T, (stop + 1) * block / rate, rates[1], rates[stop - 1]);
    double worst = 0;
    bool monotonic = true;
    for (int b = 1; b < stop; ++b) {
        const double t = (b + 0.5) * block / rate;
        if (t < 0.97 * T) {
            const double law = held * std::pow(1 - t / T, -1.0 / 3);
            worst = std::fabs(rates[b] / law - 1) > worst ? std::fabs(rates[b] / law - 1) : worst;
        }
        if (rates[b] < rates[b - 1] * (1 - 1e-4)) monotonic = false;
    }
    std::printf("settle: worst departure from (1 - t/T)^(-1/3) before 0.97 T: %.3f%%\n", worst * 100);
    check(worst < 0.005, "the rate departs from the power law by 0.5% or more");
    check(monotonic, "the rate fell while the disk settled");
    check(std::fabs((stop + 1) * block / rate - T) < 0.01, "the disk did not stop at its settle time");
    check(rates[stop - 1] > 440.0 * 0.6, "the sweep did not reach the ring");

    // Abrupt: no strikes after the stop, and 0.1 to 0.2 s after it the output
    // is 30 dB under the 0.1 s before it.
    double before = 0;
    {
        int stop2 = 0;
        const double c[8] = {spin, 0.5, 0, 0, 0, 0, 0, 0};
        Player q(c, 440.0);
        static double energy[40000];
        for (int b = 0; b < n + 400; ++b) {
            q.play();
            energy[b] = q.energy();
            if (stop2 == 0 && !q.voice.disk[0].rolling) stop2 = b;
        }
        const int tenth = static_cast<int>(0.1 * rate / block);
        double after = 0;
        for (int b = stop2 - tenth; b < stop2; ++b) before += energy[b];
        for (int b = stop2 + tenth; b < stop2 + 2 * tenth; ++b) after += energy[b];
        const double drop = 10 * std::log10(after / before + 1e-30);
        std::printf("settle: 0.1-0.2 s after the stop, %.1f dB under the 0.1 s before\n", drop);
        check(drop < -30, "the whirr does not stop abruptly");
        const double cycles = q.cycles(0);
        for (int b = 0; b < 100; ++b) q.play();
        check(q.cycles(0) == cycles, "the disk struck after it stopped");
        for (int b = 0; b < 2000; ++b) q.play();
        check(q.voice.quiet == 1, "the voice did not fall silent");
    }
    delete p;
}

void held_is_steady() {
    const double c[8] = {1.0, 0.5, 0, 0, 0.5, 0, 0, 0};
    Player p(c, 440.0);
    const double held = 440.0 / 8;
    const int window = 138;   // 0.2 s
    double first = 0, last = 0, low = 1e9, high = 0, all = 0;
    double from = p.cycles(0);
    const int windows = 21;   // 4.2 s: three of the hand's 0.7 Hz wobbles
    for (int w = 0; w < windows; ++w) {
        for (int b = 0; b < window; ++b) p.play();
        const double now = p.cycles(0), r = (now - from) * rate / (window * block);
        from = now;
        if (w == 0) continue;   // the throw
        all += r;
        if (w <= 5) first += r / 5;
        if (w > windows - 6) last += r / 5;
        low = r < low ? r : low;
        high = r > high ? r : high;
    }
    all /= windows - 1;
    std::printf("held: mean %.3f Hz (%.2f%% off %.2f), windows %.2f to %.2f Hz, first second %.3f, last %.3f\n",
                all, (all / held - 1) * 100, held, low, high, first, last);
    check(std::fabs(all / held - 1) < 0.003, "the held rate's mean is off the held rate");
    check(low > held * 0.98 && high < held * 1.02, "the held rate wobbles by 2% or more");
    check(std::fabs(last / first - 1) < 0.006, "the held rate drifts");
    check(p.voice.disk[0].rolling == 1, "a held disk stopped");
}

double fitted_exponent(double air) {
    static double rates[40000];
    int stop = 0;
    const double spin = 0.5, T = settle_seconds(spin);
    sweep(air, spin, rates, 40000, &stop);
    // Least squares of log rate against log (1 - t/T), from 0.1 T to 0.95 T.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int b = 1; b < stop; ++b) {
        const double t = (b + 0.5) * block / rate;
        if (t < 0.1 * T || t > 0.95 * T) continue;
        const double x = std::log(1 - t / T), y = std::log(rates[b]);
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

void air_sets_the_exponent() {
    const double friction = fitted_exponent(0), middle = fitted_exponent(0.5), viscous = fitted_exponent(1);
    std::printf("air: exponent %.4f at 0 (rolling friction), %.4f at 0.5, %.4f at 1 (viscous air)\n",
                friction, middle, viscous);
    check(std::fabs(friction + 1.0 / 3) < 0.005, "air 0 does not sweep as (t0 - t)^(-1/3)");
    check(std::fabs(middle + 1.0 / 4) < 0.005, "air 0.5 does not sweep as (t0 - t)^(-1/4)");
    check(std::fabs(viscous + 1.0 / 6) < 0.005, "air 1 does not sweep as (t0 - t)^(-1/6)");
}

// The magnitude of x's spectrum at f (a Hann-windowed DTFT).
double magnitude(const float* x, int n, double f) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
        re += w * x[i] * std::cos(2 * M_PI * f * i / rate);
        im += w * x[i] * std::sin(2 * M_PI * f * i / rate);
    }
    return std::sqrt(re * re + im * im);
}

void ring_tracks_the_key() {
    // Each disk's first mode, from the engine's own coefficients.
    double worst = 0;
    const double cents[4] = {-3, 3, -9, 9};   // four disks' detune
    for (int midi = 24; midi <= 108; midi += 7) {
        const double c[8] = {1.0, 0.5, 0, 0, 0, 0, 0, 1.0};
        Player p(c, hz_of(midi));
        p.play();
        for (int k = 0; k < 4; ++k) {
            const double w = std::acos(p.voice.disk[k].cosine / 2147483648.0);
            const double f = w * rate / (2 * M_PI);
            const double off = 1200 * std::log2(f / hz_of(midi)) - cents[k];
            worst = std::fabs(off) > worst ? std::fabs(off) : worst;
        }
    }
    std::printf("ring: each disk's ring within %.3f cents of the key (and its size's detune), C1 to C8\n", worst);
    check(worst < 0.5, "a disk's ring is out of tune with the key");

    // And in the sound: the spectrum's peak within a semitone of the key is at it.
    static float x[44100];
    const int keys[3] = {45, 57, 69};
    for (int j = 0; j < 3; ++j) {
        const double c[8] = {1.0, 0.5, 0, 0, 0, 0, 0, 0}, f = hz_of(keys[j]);
        Player p(c, f);
        for (int b = 0; b < 300; ++b) p.play();
        for (int b = 0; b < 689; ++b) {
            p.play();
            for (int i = 0; i < 64; ++i) x[b * 64 + i] = static_cast<float>(p.out[i] / 2147483648.0);
        }
        double best = 0, at = 0;
        for (int step = -100; step <= 100; ++step) {
            const double g = f * std::pow(2.0, step / 1200.0);
            const double m = magnitude(x, 689 * 64, g);
            if (m > best) { best = m; at = step; }
        }
        std::printf("ring: MIDI %d sounds its strongest within a semitone at %+.0f cents\n", keys[j], at);
        check(std::fabs(at) <= 8, "the sound's ring is not at the key");
    }
}

void many_stop_apart() {
    const double c[8] = {0.4, 0.5, 0, 0, 0, 0, 0, 1.0};
    Player p(c, 220.0);
    int stopped[4] = {-1, -1, -1, -1};
    for (int b = 0; b < 4000; ++b) {
        p.play();
        for (int k = 0; k < 4; ++k)
            if (stopped[k] < 0 && !p.voice.disk[k].rolling) stopped[k] = b;
    }
    std::printf("many: four disks stop at %.2f, %.2f, %.2f and %.2f s\n", stopped[0] * block / rate,
                stopped[1] * block / rate, stopped[2] * block / rate, stopped[3] * block / rate);
    bool apart = true;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (stopped[i] < 0 || std::abs(stopped[i] - stopped[j]) < 20) apart = false;
    check(apart, "many's disks do not stop at different moments");
}

void bounded() {
    // Every control at its ends, low and high keys: never over half scale.
    bool ok = true, sounded = true;
    for (int corner = 0; corner < 256; ++corner) {
        double c[8];
        for (int k = 0; k < 8; ++k) c[k] = (corner >> k) & 1 ? 1.0 : 0.0;
        if (c[0] == 1.0 && corner & 2) c[0] = 0.9;   // a long let-go spin as well as hold
        const double keys[3] = {hz_of(21), hz_of(69), hz_of(120)};
        for (int j = 0; j < 3; ++j) {
            Player p(c, keys[j]);
            bool any = false;
            for (int b = 0; b < 400; ++b) {
                p.play();
                for (int i = 0; i < 64; ++i) {
                    if (p.out[i] > 1073741824 || p.out[i] < -1073741824) ok = false;
                    any = any || p.out[i] != 0;
                }
            }
            sounded = sounded && any;
        }
    }
    check(ok, "the output exceeds the declared peak");
    check(sounded, "a setting is silent");
}

void screen_follows() {
    // The screen's own copy of a disk's life ends with the voice's, from the
    // same step (spin.h), and the frame is valid throughout.
    static coin::DisplayState state;
    static coin::Frame frame;
    opxy::draw::Input input = {};
    const double c[8] = {0.5, 0.5, 0, 0, 0.3, 0, 0.3, 0.6};
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    input.held_notes = 0;
    coin::display_reset(state);
    coin::display(state, input, frame);
    check(opxy::draw::valid(frame), "the first frame is invalid");
    input.note_serial = 1;
    input.held_notes = 1;
    input.elapsed = 34;   // 30 frames a second
    bool valid = true, stopped = false;
    double at = 0;
    long changes = 0, peak = 0, frames = 0;
    static coin::Frame before;
    for (int f = 0; f < 30 * 6; ++f) {
        if (f == 30 * 4) input.slots[coin::SIZE] = q31(0.7);   // a turn of size as it lies still
        std::memcpy(&before, &frame, sizeof(frame));
        coin::display(state, input, frame);
        valid = valid && opxy::draw::valid(frame);
        if (!stopped && state.disk[0].life <= 0) { stopped = true; at = f / 30.0; }
        long n = 0;
        for (unsigned i = 0; i < coin::Frame::node_count; ++i)
            if ((i < coin::Frame::path_count && frame.paths[i].key != before.paths[i].key) ||
                std::memcmp(&frame.nodes[i], &before.nodes[i], sizeof(opxy::draw::Node))) ++n;
        changes += n;
        ++frames;
        peak = n > peak ? n : peak;
    }
    // Still between notes: nothing changes.
    long idle = 0;
    for (int f = 0; f < 30; ++f) {
        std::memcpy(&before, &frame, sizeof(frame));
        coin::display(state, input, frame);
        idle += std::memcmp(&frame, &before, sizeof(frame)) != 0;
    }
    std::printf("screen: its disk stops at %.2f s (the voice's settle time %.2f s); churn at 30 fps: mean %.1f, peak %ld "
                "nodes a frame; %ld changed frames in a still second\n",
                at, settle_seconds(0.5), static_cast<double>(changes) / frames, peak, idle);
    check(valid, "a frame is invalid");
    check(stopped && std::fabs(at - settle_seconds(0.5)) < 0.1, "the screen's disk does not stop with the voice's");
    check(changes <= 20 * frames && peak <= 24, "the screen changes too many nodes a frame");
    check(idle == 0, "the still screen moves");
}

// ---- The README's table: each control's effect, measured. ----
// Euler's defaults, in control order.
const double euler[8] = {0.3984, 0.3, 0.125, 0.1667, 0.25, 0.0, 0.5746, 0.125};

// Energy (Hann-windowed DFT) of x in [lo, hi) Hz, at `step` Hz.
double band_energy(const float* x, int n, double lo, double hi, double step = 10) {
    double total = 0;
    for (double f = lo; f < hi; f += step) {
        const double m = magnitude(x, n, f);
        total += m * m;
    }
    return total;
}
// T60 (s) by least squares of the level (`hop`-sample steps) against time,
// over [from, to) samples: beating and strikes average out.
double decay_time(const float* x, int from, int to, int hop) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int m = 0;
    for (int i = from; i + hop <= to; i += hop) {
        double e = 0;
        for (int j = 0; j < hop; ++j) e += x[i + j] * x[i + j];
        const double t = i / rate, db = 10 * std::log10(e / hop + 1e-30);
        sx += t; sy += db; sxx += t * t; sxy += t * db; ++m;
    }
    const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return -60 / slope;
}
// Plays A4 with control `k` at v (the rest at Euler's) into x, `blocks` long.
void play_into(int k, double v, float* x, int blocks, double spin = -1, double many = -1) {
    double c[8];
    for (int j = 0; j < 8; ++j) c[j] = euler[j];
    c[k] = v;
    if (spin >= 0) c[0] = spin;
    if (many >= 0) c[7] = many;
    Player p(c, 440.0);
    for (int b = 0; b < blocks; ++b) {
        p.play();
        for (int i = 0; i < 64; ++i) x[b * 64 + i] = static_cast<float>(p.out[i] / 2147483648.0);
    }
}
// Plays A4 with control k at v, recording the output and disk 0's contact rate
// a block (Hz) until it stops (or `blocks`); returns the block it stopped in.
int spin_into(int k, double v, float* x, double* rates, int blocks) {
    double c[8];
    for (int j = 0; j < 8; ++j) c[j] = euler[j];
    c[k] = v;
    Player p(c, 440.0);
    double last = p.cycles(0);
    for (int b = 0; b < blocks; ++b) {
        p.play();
        for (int i = 0; i < 64; ++i) x[b * 64 + i] = static_cast<float>(p.out[i] / 2147483648.0);
        const double now = p.cycles(0);
        rates[b] = (now - last) * rate / block;
        last = now;
        if (!p.voice.disk[0].rolling) return b;
    }
    return blocks;
}
// The contact rate's swing either side of its 0.2 s trend, cents rms, over
// the first 80% of a spin of `n` blocks.
double warble_of(const double* rates, int n) {
    const int k = 69, end = n * 8 / 10;
    double sum = 0;
    int m = 0;
    for (int b = k + 1; b < end - k; ++b) {
        double trend = 0;
        for (int j = -k; j <= k; ++j) trend += std::log2(rates[b + j]);
        trend /= 2 * k + 1;
        const double c = 1200 * (std::log2(rates[b]) - trend);
        sum += c * c;
        ++m;
    }
    return m ? std::sqrt(sum / m) : 0;
}
// An after-ring's T60 (s) from 20 ms after the stop to 60 dB under it.
double after_ring(const double (&c)[8]) {
    static float x[44100 * 8];
    Player p(c, 440.0);
    int stop = -1, n = 0;
    for (int b = 0; b < 689 * 8; ++b) {
        p.play();
        if (stop < 0 && b > 0 && !p.voice.disk[0].rolling) stop = (b + 1) * 64 + 441;
        for (int i = 0; i < 64; ++i) x[n++] = static_cast<float>(p.out[i] / 2147483648.0);
    }
    double e0 = 0;
    for (int j = 0; j < 441; ++j) e0 += x[stop + j] * x[stop + j];
    int end = stop + 882;
    for (; end + 441 < n && end < stop + 44100 * 5; end += 441) {
        double e = 0;
        for (int j = 0; j < 441; ++j) e += x[end + j] * x[end + j];
        if (e < e0 * 1e-6) break;
    }
    return decay_time(x, stop + 882, end, 441);
}

void report() {
    static float x[44100 * 8];
    const double at[5] = {0, 0.25, 0.5, 0.75, 1.0};
    std::printf("\nControl | 0 | 25 | 50 | 75 | 100\n");
    // spin: when the disk stops.
    std::printf("spin: settles in");
    for (double v : at) {
        double c[8];
        for (int j = 0; j < 8; ++j) c[j] = euler[j];
        c[0] = v;
        Player p(c, 440.0);
        p.play();   // the throw
        int b = 1;
        for (; b < 689 * 40 && p.voice.disk[0].rolling; ++b) p.play();
        if (p.voice.disk[0].rolling) std::printf(" | kept spinning");
        else std::printf(" | %.2f s", b * block / rate);
    }
    // size: the held contact rate.
    std::printf("\nsize: held contact rate");
    for (double v : at) {
        double c[8];
        for (int j = 0; j < 8; ++j) c[j] = euler[j];
        c[0] = 1.0;
        c[1] = v;
        Player p(c, 440.0);
        for (int b = 0; b < 345; ++b) p.play();
        const double from = p.cycles(0);
        for (int b = 0; b < 689; ++b) p.play();
        std::printf(" | %.1f Hz", (p.cycles(0) - from) * rate / (689 * block));
    }
    // disk and base, at each option: steel, brass, glass, wood; glass, wood, stone.
    const double disks[4] = {0.125, 0.375, 0.625, 0.875}, bases[3] = {0.1667, 0.5, 0.8333};
    // disk: its upper modes' level against its ring, in a held note.
    std::printf("\ndisk (steel, brass, glass, wood): upper modes against the ring");
    for (double v : disks) {
        play_into(2, v, x, 689 * 2, 1.0);
        const double ring = band_energy(x + 44100, 22050, 400, 480);
        const double upper = band_energy(x + 44100, 22050, 700, 1100);
        std::printf(" | %+.0f dB", 10 * std::log10(upper / ring));
    }
    // disk: its after-ring, a short spin, at Euler's ring.
    std::printf("\ndisk (steel, brass, glass, wood): after-ring T60");
    for (double v : disks) {
        double c[8];
        for (int j = 0; j < 8; ++j) c[j] = euler[j];
        c[0] = 0.15;
        c[2] = v;
        std::printf(" | %.2f s", after_ring(c));
    }
    // base: the spin's level, and its low (100-330 Hz) against its high
    // (0.9-1.5 kHz) over the spin's second half.
    static double rates[689 * 8];
    std::printf("\nbase (glass, wood, stone): the spin's level, low against high");
    for (double v : bases) {
        const int n = spin_into(3, v, x, rates, 689 * 4) * 64, a = n / 2;
        double e = 0;
        for (int i = 0; i < n; ++i) e += x[i] * x[i];
        std::printf(" | %.1f dB, %+.1f dB", 10 * std::log10(e / n),
                    10 * std::log10(band_energy(x + a, n - a, 100, 330) / band_energy(x + a, n - a, 900, 1500)));
    }
    // wobble: the whirr's warble, the contact rate's swing (the top half adds slip's jitter).
    std::printf("\nwobble: the whirr's warble");
    for (double v : at) {
        const int n = spin_into(4, v, x, rates, 689 * 4);
        std::printf(" | %.0f cents", warble_of(rates, n));
    }
    // air: the last octave onto the note, and the breath: the spin's last 40%
    // above 2 kHz against below.
    std::printf("\nair: the last octave's time, the breath");
    for (double v : at) {
        const int stop = spin_into(5, v, x, rates, 689 * 4);
        int b = stop;
        while (b > 0 && rates[b - 1] > 220) --b;
        const int n = stop * 64, a = n * 6 / 10;
        std::printf(" | %.0f ms, %.1f dB", (stop - b) * block / rate * 1000,
                    10 * std::log10(band_energy(x + a, n - a, 2000, 8000, 50) / band_energy(x + a, n - a, 100, 2000)));
    }
    // ring: the after-ring's T60 once the disk lies flat.
    std::printf("\nring: after-ring T60");
    for (double v : at) {
        double c[8];
        for (int j = 0; j < 8; ++j) c[j] = euler[j];
        c[0] = 0.15;
        c[6] = v;
        std::printf(" | %.2f s", after_ring(c));
    }
    // many: the disks and how far apart they stop.
    std::printf("\nmany: disks, and the spread of their stops");
    for (double v : at) {
        double c[8];
        for (int j = 0; j < 8; ++j) c[j] = euler[j];
        c[7] = v;
        Player p(c, 440.0);
        double first = 1e9, last = 0;
        for (int b = 0; b < 689 * 6; ++b) {
            bool before[4];
            for (int k = 0; k < 4; ++k) before[k] = p.voice.disk[k].rolling;
            p.play();
            for (int k = 0; k < 4; ++k)
                if (before[k] && !p.voice.disk[k].rolling) {
                    const double t = b * block / rate;
                    first = t < first ? t : first;
                    last = t > last ? t : last;
                }
        }
        std::printf(" | %u, %.2f s", p.voice.count, last - first);
    }
    std::printf("\n");
}
}

int main() {
    settles_along_the_law();
    held_is_steady();
    air_sets_the_exponent();
    ring_tracks_the_key();
    many_stop_apart();
    bounded();
    screen_follows();
    if (std::getenv("COIN_REPORT")) report();
    if (failures) std::printf("%d checks failed\n", failures);
    return failures ? 1 : 0;
}
