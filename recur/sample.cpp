// Recur: a window of the sample cut into a square of slices and permuted by
// an area-preserving map, one iteration a step: re-sequenced, stuttered and
// glitched, then after the map's period, the recording again exactly. At
// slice's hiss end the cells are single samples and it shreds into noise.
#include "core.h"
#include "tables.h"

namespace recur {
namespace {
// Phase increment of middle C.
const u32 c4_increment = 25480119u;
// Samples per crossfade segment, and the most an iteration crossfade or a
// reader's level moves in one: 128 samples from end to end.
const u32 segment = 16, settle = 8192;
// A change of size or map fades the voice out over this many samples, and in again.
const u32 presence_full = 128;
// A region shorter than this is silent.
const u32 shortest = 256;

// Each chrd's readers, in cents from the note: off, dtun, oct, 5th, maj, min, harm.
const i32 chord_cents[chord_count][most_taps] = {
    {0, 0, 0}, {0, 9, -9}, {0, 1200, 0}, {0, 700, 0}, {0, 400, 700}, {0, 300, 700}, {0, 1200, 1900}};
const u32 chord_taps[chord_count] = {1, 3, 2, 2, 3, 3, 3};
// A reader's level, Q15, by how many sound: 1, 0.6, 0.45, so a chord of readers peaks near one alone.
const i32 tap_gain[most_taps + 1] = {0, 32767, 19661, 14746};

// a / b in 16-bit fixed point, by long division, without a 64-bit divide.
u32 ratio(u32 a, u32 b) {
    if (!b) return 0;
    u32 whole = a / b, remainder = a % b, fraction = 0;
    for (u32 bit = 0; bit < 16; ++bit) {
        const bool carry = remainder >= 0x80000000u;
        remainder <<= 1;
        fraction <<= 1;
        if (carry || remainder >= b) { remainder -= b; fraction |= 1; }
    }
    return whole << 16 | fraction;
}

// (a * b) >> 16 in 32-bit arithmetic: the device has no 64-bit multiply.
u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}

// 2^(cents/1200) in Q16: whole semitones from the table, the rest linearly.
u32 cents_ratio(i32 cents) {
    i32 octave = 0;
    while (cents < 0) { cents += 1200; --octave; }
    while (cents >= 1200) { cents -= 1200; ++octave; }
    const u32 semitones = u32(cents) / 100, rest = u32(cents) % 100;
    const u32 low = semitone_table[semitones], high = semitones < 11 ? semitone_table[semitones + 1] : 131072u;
    const u32 r = low + (high - low) * rest / 100;
    return octave >= 0 ? r << octave : r >> -octave;
}

// a * b >> 14 for 16-bit a and b: the Blackfin's single-cycle 16 by 16 multiply.
inline i32 mul16(i32 a, i32 b) { return (i32(opxy::i16(a)) * opxy::i16(b)) >> 14; }

u32 next_random(Tap& t) {
    u32 x = t.random ? t.random : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return t.random = x;
}

// sin(w * 90 degrees), Q15, for w Q16 from 0 to 65536: the equal-power crossfade.
i32 sine(u32 w) {
    const u32 at = w >> 10, part = w & 1023;
    if (at >= 64) return quarter_sine[64];
    return quarter_sine[at] + (((quarter_sine[at + 1] - quarter_sine[at]) * i32(part)) >> 10);
}

// The controls, decoded once a block, for the square and map playing.
struct Settings {
    u32 begin, end, span;   // the region
    u32 bits, mask, size;   // the square: 2^bits a side, size samples
    u32 map, period, cycle; // the map, its period, and the period Q20
    u32 tail_bits;          // the loop's crossfade is 2^tail_bits samples
    u32 home;               // pos: the window's start, frames into the region
    u32 room;               // the latest start that fits the window
    u32 step;               // step's start in the period, Q20
    u32 advance;            // rate, Q20 a segment
    u32 blend, blend_reciprocal;
    u32 spray;              // spry: frames either side of pos a pass may start
    u32 shuffle;            // spry past halfway: whole iterations either side a pass may play
    u32 taps;               // readers sounding
    i32 wet, dry;           // depth: the scramble's share and the plain window's, Q15 (32768 unity), equal power
};

// The readers' steps for the note's pitch, the sample's tuning and chrd,
// worked out again only when one of them changes.
void tune(Voice& voice, const opxy::SampleBlock& block, u32 chord) {
    if (voice.pitch == block.pitch[0] && voice.cents == block.region.pitch_cents && voice.chord == chord) return;
    voice.pitch = block.pitch[0];
    voice.cents = block.region.pitch_cents;
    voice.chord = chord;
    // The note's rate from middle C, the sample's own rate and its tuning.
    voice.root = times(ratio(voice.sample.rate, 44100), cents_ratio(block.region.pitch_cents));
    const u32 rate = times(ratio(block.pitch[0], c4_increment), voice.root);
    for (u32 i = 0; i < most_taps; ++i) {
        const u32 step = times(rate, cents_ratio(chord_cents[chord][i]));
        voice.steps[i] = step ? step : 1;
    }
}

// The hiss path's controls: a square of single samples (bits a side), the
// iteration moving at rate's pace in time, a quarter of each step crossfaded.
Settings decode(const Voice& voice, const opxy::SampleBlock& block, u32 bits, u32 map) {
    const i32* p = block.parameters;
    Settings s;
    s.begin = block.region.begin;
    s.end = block.region.end < voice.sample.frames ? block.region.end : voice.sample.frames;
    s.span = s.end > s.begin ? s.end - s.begin : 0;
    s.bits = bits;
    s.mask = (1u << bits) - 1;
    s.size = 1u << (2 * bits);
    s.map = map;
    s.period = period(map, bits);
    s.cycle = s.period << iteration_bits;
    s.tail_bits = 2 * bits - 3 < 10 ? 2 * bits - 3 : 10;
    s.room = s.span > s.size ? s.span - s.size : 0;
    s.home = times(level(p[kPos]), s.room + 1);
    s.step = step_position(p[kStep], s.period);
    const u32 bpm = block.transport.bpm_x10 ? block.transport.bpm_x10 : 1200;
    s.advance = (rate256(rate64[choice(p[kRate], rate_count)], bpm) * 97391u) >> 16;   // (it/s Q8) * 16 / 44100 * 2^20 / 256
    s.blend = 16384;
    s.blend_reciprocal = crossfade_reciprocal(s.blend);
    s.spray = s.shuffle = 0;
    const u32 depth = depth16(p[kDepth]);
    s.wet = depth >= 65536 ? 32768 : sine(depth);   // exact unity at the ends
    s.dry = depth == 0 ? 32768 : sine(65536 - depth);
    // In hiss a chord stacks two readers at most: a third would be noise on
    // noise, at a third more of the CPU.
    s.taps = chord_taps[choice(p[kChrd], chord_count)] < 2 ? chord_taps[choice(p[kChrd], chord_count)] : 2;
    return s;
}

// Where the window starts for a pass: pos, scattered by spry.
u32 place(Tap& t, const Settings& s) {
    if (!s.spray) return s.home;
    const i32 off = i32(times(next_random(t) >> 16, 2 * s.spray + 1)) - i32(s.spray);
    const i32 at = i32(s.home) + off;
    return at < 0 ? 0 : at > i32(s.room) ? s.room : u32(at);
}

// spry past halfway: a pass plays whole iterations off the others, Q20.
i32 scatter(Tap& t, const Settings& s) {
    if (!s.shuffle) return 0;
    const i32 n = i32(((next_random(t) >> 16) * (2 * s.shuffle + 1)) >> 16) - i32(s.shuffle);
    return n * i32(1u << iteration_bits);
}

void next_pass(Tap& t, const Settings& s) {
    t.offset = t.next;
    t.next = place(t, s);
    t.shift = scatter(t, s);
}

// The gather for iteration k: the map's inverse, applied k times, since the
// sample the map moved to cell q came from its inverse's image of q.
Mat gather(const Settings& s, u32 k) { return power(s.map, (s.period - k) % s.period, s.bits); }

// The iteration a reader aims for, Q20: step, plus rate's progress, plus spry's shift.
u32 aim(const Settings& s, u32 progress, i32 shift) {
    u32 at = s.step + progress + (shift < 0 ? s.cycle - u32(-shift) : u32(shift));
    while (at >= s.cycle) at -= s.cycle;
    return at;
}

template <bool Stereo> inline i32 frame_at(const opxy::i16* data, u32 frame) {
    return Stereo ? (i32(data[frame * 2]) + data[frame * 2 + 1]) >> 1 : data[frame];
}

// Two frames, interpolated by a 16-bit fraction.
template <bool Stereo> inline i32 between(const opxy::i16* data, u32 f0, u32 f1, u32 fraction) {
    const i32 a = frame_at<Stereo>(data, f0), b = frame_at<Stereo>(data, f1);
    return a + mul16((b - a) >> 1, fraction >> 1);   // half the step by half the fraction: one multiply
}

// The loop's seam: over the last 2^tail_bits samples of each pass the window
// crossfades into the recording just before the next pass's start, which
// leads into that pass's first sample, since every map leaves sample 0 in
// place. It takes the gains the first sample will have (both, if mixing).
template <bool Stereo>
inline i32 seam(const Tap& t, const Settings& s, const opxy::i16* data, u32 j, u32 fraction, i32 half, i32 gains) {
    const i32 q = i32(t.next + j) - i32(s.size);
    const u32 f0 = s.begin + u32(q < 0 ? q + i32(s.span) : q);
    const u32 f1 = s.begin + u32(q + 1 < 0 ? q + 1 + i32(s.span) : q + 1);
    const i32 r = between<Stereo>(data, f0, f1, fraction);
    const i32 g = i32((((j - (s.size - (1u << s.tail_bits))) << 16) | fraction) >> s.tail_bits) >> 1;   // Q15
    const i32 d = ((r * gains) >> 1) - half;   // gains at most 46341
    return half + (d >> 15) * g;
}

// n samples of one reader into out (half scale), under gathers ma and mb with
// gains (Q15 << 16) ramped by da and db a sample. Mixing: both gathers sound.
// A linear map keeps track of the cells it gathers from as the read moves: a
// sample along a row moves the cell by (a, c), and the next row adds (b, d),
// since a N and c N vanish mod N.
template <bool Stereo, bool Mixing>
void play_linear(Tap& t, const Settings& s, const opxy::i16* data, i32* out, u32 n,
                 const Mat& ma, const Mat& mb, i32 ga, i32 da, i32 gb, i32 db, i32 dry) {
    const u32 bits = s.bits, mask = s.mask, side = mask + 1, tail = s.size - (1u << s.tail_bits);
    u32 j = t.j, fraction = t.fraction, x = j & mask, y = j >> bits;
    u32 base = s.begin + t.offset;
    const u32 whole = t.step >> 16, part = t.step & 0xffff;
    u32 ax = (ma.a * x + ma.b * y) & mask, ay = (ma.c * x + ma.d * y) & mask;
    u32 bx = (mb.a * x + mb.b * y) & mask, by = (mb.c * x + mb.d * y) & mask;
    const u32 ax0 = whole * ma.a, ay0 = whole * ma.c, bx0 = whole * mb.a, by0 = whole * mb.c;
    for (; n; --n, ++out) {
        // j + 1's cell: one along the row, or the next row's first.
        const bool edge = x == mask;
        const u32 i0 = ay << bits | ax;
        const u32 i1 = ((ay + ma.c + (edge ? ma.d : 0)) & mask) << bits | ((ax + ma.a + (edge ? ma.b : 0)) & mask);
        i32 half = (between<Stereo>(data, base + i0, base + i1, fraction) * (ga >> 16)) >> 1;
        i32 gains = ga >> 16;
        if (Mixing) {
            const u32 k0 = by << bits | bx;
            const u32 k1 = ((by + mb.c + (edge ? mb.d : 0)) & mask) << bits | ((bx + mb.a + (edge ? mb.b : 0)) & mask);
            half += (between<Stereo>(data, base + k0, base + k1, fraction) * (gb >> 16)) >> 1;
            gains += gb >> 16;
            gb += db;
        }
        ga += da;
        if (dry) {   // depth under 1: the plain window beneath the scramble
            half += (between<Stereo>(data, base + j, base + ((j + 1) & (s.size - 1)), fraction) * dry) >> 1;
            gains += dry;
        }
        if (j >= tail) half = seam<Stereo>(t, s, data, j, fraction, half, gains);
        *out += half;
        fraction += part;
        const u32 carry = fraction >> 16;
        fraction &= 0xffff;
        j += whole + carry;
        x += whole + carry;
        ax += carry ? ax0 + ma.a : ax0;
        ay += carry ? ay0 + ma.c : ay0;
        if (Mixing) { bx += carry ? bx0 + mb.a : bx0; by += carry ? by0 + mb.c : by0; }
        if (x >= side) {
            x -= side;
            ax += ma.b; ay += ma.d;
            if (Mixing) { bx += mb.b; by += mb.d; }
        }
        ax &= mask; ay &= mask;
        if (Mixing) { bx &= mask; by &= mask; }
        if (j >= s.size) {
            j -= s.size;
            next_pass(t, s);
            base = s.begin + t.offset;
        }
    }
    t.j = j;
    t.fraction = fraction;
}

// The baker's map isn't linear: each sample's cell is worked out afresh. Its
// ring is y's bits backwards, then x's, turned by m.a.
inline u32 baker(const Mat& m, u32 j, u32 bits, u32 mask, u32 ring_mask) {
    const u32 shift = 8 - bits, total = 2 * bits, turn = m.a;
    const u32 ring = (u32(reversed8[j >> bits]) >> shift) << bits | (j & mask);
    const u32 turned = ((ring << turn) | (ring >> (total - turn))) & ring_mask;
    return (u32(reversed8[turned >> bits]) >> shift) << bits | (turned & mask);
}

template <bool Stereo, bool Mixing>
void play_baker(Tap& t, const Settings& s, const opxy::i16* data, i32* out, u32 n,
                const Mat& ma, const Mat& mb, i32 ga, i32 da, i32 gb, i32 db, i32 dry) {
    const u32 bits = s.bits, mask = s.mask, last = s.size - 1, tail = s.size - (1u << s.tail_bits);
    u32 j = t.j, fraction = t.fraction;
    u32 base = s.begin + t.offset;
    const u32 whole = t.step >> 16, part = t.step & 0xffff;
    for (; n; --n, ++out) {
        const u32 j1 = (j + 1) & last;
        i32 half = (between<Stereo>(data, base + baker(ma, j, bits, mask, last), base + baker(ma, j1, bits, mask, last),
                                    fraction) * (ga >> 16)) >> 1;
        i32 gains = ga >> 16;
        if (Mixing) {
            half += (between<Stereo>(data, base + baker(mb, j, bits, mask, last), base + baker(mb, j1, bits, mask, last),
                                     fraction) * (gb >> 16)) >> 1;
            gains += gb >> 16;
            gb += db;
        }
        ga += da;
        if (dry) {   // depth under 1: the plain window beneath the scramble
            half += (between<Stereo>(data, base + j, base + ((j + 1) & (s.size - 1)), fraction) * dry) >> 1;
            gains += dry;
        }
        if (j >= tail) half = seam<Stereo>(t, s, data, j, fraction, half, gains);
        *out += half;
        fraction += part;
        j += whole + (fraction >> 16);
        fraction &= 0xffff;
        if (j >= s.size) {
            j -= s.size;
            next_pass(t, s);
            base = s.begin + t.offset;
        }
    }
    t.j = j;
    t.fraction = fraction;
}

typedef void (*Player)(Tap&, const Settings&, const opxy::i16*, i32*, u32, const Mat&, const Mat&,
                       i32, i32, i32, i32, i32);
template <bool Stereo> Player player(bool baker_map, bool mixing) {
    if (baker_map) return mixing ? &play_baker<Stereo, true> : &play_baker<Stereo, false>;
    return mixing ? &play_linear<Stereo, true> : &play_linear<Stereo, false>;
}
Player player(const Settings& s, bool stereo, bool mixing) {
    return stereo ? player<true>(s.map == kBaker, mixing) : player<false>(s.map == kBaker, mixing);
}

void set_b(Tap& t, const Settings& s, u32 k) { if (t.kb != k) { t.kb = k; t.gb = gather(s, k); } }

// Moves a reader's crossfade toward iterations kt and kt1 = kt + 1, at kt1's
// share wt, by at most settle a segment; returns kb's share at the segment's
// end. Only whole iterations change places, and only while silent, so any
// change of iteration, step or rate is a crossfade.
u32 steer(Tap& t, const Settings& s, u32 kt, u32 kt1, u32 wt) {
    if (t.w >= 65536) { t.ka = t.kb; t.ga = t.gb; t.w = 0; }
    u32 goal;
    if (t.w == 0) {
        if (t.ka == kt) { set_b(t, s, kt1); goal = wt; }
        else if (t.ka == kt1) { set_b(t, s, kt); goal = 65536 - wt; }
        else { set_b(t, s, wt > 32768 ? kt1 : kt); goal = 65536; }
    } else if (t.ka == kt && t.kb == kt1) goal = wt;
    else if (t.ka == kt1 && t.kb == kt) goal = 65536 - wt;
    else if (t.kb == kt || t.kb == kt1) goal = 65536;
    else if (t.ka == kt || t.ka == kt1) goal = 0;
    else goal = t.w >= 32768 ? 65536 : 0;
    if (goal > t.w) return goal - t.w > settle ? t.w + settle : goal;
    return t.w - goal > settle ? t.w - settle : goal;
}

// The iteration pair and share a reader aims for now.
void target(const Settings& s, u32 progress, i32 shift, u32& kt, u32& kt1, u32& wt) {
    const u32 at = aim(s, progress, shift);
    kt = at >> iteration_bits;
    kt1 = kt + 1 == s.period ? 0 : kt + 1;
    wt = toward_next((at >> 4) & 0xffff, s.blend, s.blend_reciprocal);
}

// A reader starting afresh: at the window's start, hearing its target at once.
void begin(Tap& t, const Settings& s, u32 progress, u32 j, u32 fraction) {
    t.j = j & (s.size - 1);
    t.fraction = fraction;
    t.offset = place(t, s);
    t.next = place(t, s);
    t.shift = 0;
    u32 kt, kt1, wt;
    target(s, progress, 0, kt, kt1, wt);
    t.ka = kt; t.ga = gather(s, kt);
    t.kb = kt1; t.gb = gather(s, kt1);
    t.w = wt;
}

// The square and map playing change only through silence: the block before
// fades out on the old ones, the block after fades in on the new.
void reshape(Voice& voice, const Settings& s) {
    voice.progress %= s.cycle;
    for (u32 i = 0; i < most_taps; ++i) {
        Tap& t = voice.taps[i];
        begin(t, s, voice.progress, t.j, t.fraction);
    }
}

// --- The slice path ----------------------------------------------------------

// A slice path's controls, decoded once a block.
struct Slices {
    u32 begin, end, span;   // the region
    u32 bits, side, cells;  // the square of slices
    u32 map, period;
    u32 length;             // a slice, output samples, Q8: a 1/16 at 120 BPM is 5512.5
    u32 fade, reciprocal;   // its crossfade, samples, and 65536 / fade
    u32 source;             // a slice of the recording at the sample's root, frames, Q8
    u32 home;               // pos: the window's start, frames into the region
    u32 step;               // step's iteration
    u32 rate;               // 64ths an iteration, 0 to hold
    u32 sixty_fourths;      // 64ths a slice
    u32 taps;
};

Slices decode_slices(Voice& voice, const opxy::SampleBlock& block, u32 bits, u32 map) {
    const i32* p = block.parameters;
    Slices s;
    s.begin = block.region.begin;
    s.end = block.region.end < voice.sample.frames ? block.region.end : voice.sample.frames;
    s.span = s.end - s.begin;
    s.bits = bits;
    s.side = 1u << bits;
    s.cells = s.side * s.side;
    s.map = map;
    s.period = period(map, bits);
    const u32 bpm = block.transport.bpm_x10 >= 400 ? block.transport.bpm_x10 : 1200;
    // While fading out into the hiss path, slices of 1/64.
    s.sixty_fourths = slice64[choice(p[kSlice], slice_count)] ? slice64[choice(p[kSlice], slice_count)] : 1;
    s.length = s.sixty_fourths * ((1653750u << 8) / bpm);   // 44100 * 600 / 16 samples a 64th at bpm / 10
    s.fade = s.length >> 10 < 256 ? s.length >> 10 : 256;
    s.reciprocal = 65536 / s.fade;
    s.source = times(s.length, voice.root);
    s.home = times(level(p[kPos]), s.span);
    s.step = (level(p[kStep]) * s.period) >> 16;
    s.rate = rate64[choice(p[kRate], rate_count)];
    s.taps = chord_taps[choice(p[kChrd], chord_count)];
    const u32 key = map << 24 | bits << 20 | depth16(p[kDepth]);
    if (key != voice.moving_key) {
        moving_cells(map, bits, depth16(p[kDepth]), voice.moving);
        voice.moving_key = key;
    }
    return s;
}

// The recording's slice that slot plays at iteration k: where the map's
// inverse, applied k times, takes it, if its orbit moves at this depth.
u32 source_slot(Voice& voice, const Slices& s, u32 k, u32 slot) {
    if (!moves(voice.moving, slot)) return slot;
    const u32 key = s.map << 24 | s.bits << 20 | k;
    if (key != voice.gather_key) {
        voice.gather = power(s.map, (s.period - k) % s.period, s.bits);
        voice.gather_key = key;
    }
    u32 x = slot & (s.side - 1), y = slot >> s.bits;
    send(voice.gather, s.map, s.bits, x, y);
    return y * s.side + x;
}

// A new slice: every reader jumps to its start, crossfading from where it
// was, unless it is already there (a slice that follows on in the recording).
void begin_slice(Voice& voice, const Slices& s, bool fresh) {
    const u32 k = (s.step + voice.iteration) % s.period;
    const u32 from = (s.home + ((source_slot(voice, s, k, voice.slot) * s.source) >> 8)) % s.span;
    const u32 frame = s.begin + from;
    for (u32 i = 0; i < most_taps; ++i) {
        Reader& r = voice.readers[i];
        if (fresh || (r.frame == frame && r.fraction == 0)) { r.crossing = false; r.frame = frame; r.fraction = 0; continue; }
        r.tail_frame = r.frame;
        r.tail_fraction = r.fraction;
        r.frame = frame;
        r.fraction = 0;
        r.crossing = s.fade != 0;
    }
    // The slot's length: whole samples between its Q8 bounds in the pass, and
    // at a pass's start what the passes before it have left over.
    voice.length = ((voice.slot + 1) * s.length >> 8) - (voice.slot * s.length >> 8);
    if (!voice.slot) {
        if (!fresh) voice.carry += (s.cells * s.length) & 255;
        voice.length += voice.carry >> 8;
        voice.carry &= 255;
    }
    voice.into = 0;
}

// n samples of one reader into out (half scale) at a level (Q15 << 16)
// moving by slope a sample, fading from its last slice over the first fade
// samples of this one.
template <bool Stereo>
void read_slice(Reader& r, const Slices& s, const opxy::i16* data, i32* out, u32 n, u32 into, u32 step,
                i32 level, i32 slope) {
    const u32 whole = step >> 16, part = step & 0xffff, last = s.end - 1;
    for (; n; --n, ++out, ++into) {
        const u32 next = r.frame < last ? r.frame + 1 : s.begin;
        i32 v = between<Stereo>(data, r.frame, next, r.fraction);
        if (r.crossing) {
            if (into < s.fade) {
                // Equal power along a smoothstep: no kink where the fade begins or ends.
                const u32 x = (into * s.reciprocal) >> 4, w = (((x * x) >> 12) * (12288 - 2 * x)) >> 8;   // x Q12, w Q16
                const u32 tail_next = r.tail_frame < last ? r.tail_frame + 1 : s.begin;
                const i32 t = between<Stereo>(data, r.tail_frame, tail_next, r.tail_fraction);
                v = (v * sine(w) + t * sine(65536 - w)) >> 15;
                r.tail_fraction += part;
                r.tail_frame += whole + (r.tail_fraction >> 16);
                r.tail_fraction &= 0xffff;
                if (r.tail_frame >= s.end) r.tail_frame -= s.span;
            } else r.crossing = false;
        }
        *out += (v * (level >> 16)) >> 1;
        level += slope;
        r.fraction += part;
        r.frame += whole + (r.fraction >> 16);
        r.fraction &= 0xffff;
        if (r.frame >= s.end) r.frame -= s.span;
    }
}

// One block of the slice path into mix: slices of the window, each the
// recording's slice the map has put there, on the tempo's grid.
void render_slices(Voice& voice, const opxy::SampleBlock& block, i32* mix, u32 bits, u32 map) {
    const Slices s = decode_slices(voice, block, bits, map);
    const bool fresh = voice.fresh;
    if (fresh) {
        voice.slot = 0;
        voice.iteration = 0;
        voice.counted = 0;
        voice.carry = 0;
        voice.taps_playing = s.taps;
        begin_slice(voice, s, true);
    }
    voice.slot %= s.cells;
    voice.iteration %= s.period;
    const bool stereo = voice.sample.channels == 2;
    for (u32 at = 0; at < opxy::block_frames;) {
        if (voice.into >= voice.length) {
            // The slice is over: the next slot, and the iteration rate has reached.
            voice.slot = voice.slot + 1 == s.cells ? 0 : voice.slot + 1;
            if (s.rate) {
                voice.counted += s.sixty_fourths;
                while (voice.counted >= s.rate) {
                    voice.counted -= s.rate;
                    voice.iteration = voice.iteration + 1 == s.period ? 0 : voice.iteration + 1;
                }
            }
            begin_slice(voice, s, false);
        }
        u32 n = voice.length - voice.into;
        if (n > opxy::block_frames - at) n = opxy::block_frames - at;
        for (u32 i = 0; i < most_taps; ++i) {
            // A reader joining fades in from where the first one reads; one leaving fades out.
            const i32 aim_level = i < s.taps ? tap_gain[s.taps] : 0;
            const i32 from_level = fresh ? aim_level : i < voice.taps_playing ? tap_gain[voice.taps_playing] : 0;
            if (!from_level && !aim_level) continue;
            Reader& r = voice.readers[i];
            if (!from_level && at == 0) r = voice.readers[0];
            // Levels move across the block a sample at a time.
            const i32 slope = (aim_level - from_level) * 1024;   // a 64th of the change, Q16
            const i32 level = from_level * 65536 + slope * i32(at);
            if (stereo) read_slice<true>(r, s, voice.sample.data, mix + at, n, voice.into, voice.steps[i], level, slope);
            else read_slice<false>(r, s, voice.sample.data, mix + at, n, voice.into, voice.steps[i], level, slope);
        }
        voice.into += n;
        at += n;
    }
    voice.taps_playing = s.taps;
}

// One block of the hiss path into mix.
void render_fine(Voice& voice, const opxy::SampleBlock& block, i32* mix) {
    Settings s = decode(voice, block, voice.bits, voice.map);
    if (voice.fresh) {
        voice.progress = 0;
        for (u32 i = 0; i < most_taps; ++i) {
            begin(voice.taps[i], s, 0, 0, 0);
            voice.taps[i].step = voice.steps[i];
        }
        voice.taps_playing = s.taps;
    }
    const bool stereo = voice.sample.channels == 2;
    for (u32 at = 0; at < opxy::block_frames; at += segment) {
        voice.progress += s.advance;
        if (voice.progress >= s.cycle) voice.progress -= s.cycle;
        for (u32 i = 0; i < most_taps; ++i) {
            Tap& t = voice.taps[i];
            // A reader joining fades in from where the first one reads; one leaving fades out.
            const i32 aim_level = i < s.taps ? tap_gain[s.taps] : 0;
            const i32 from_level = voice.fresh ? aim_level : i < voice.taps_playing ? tap_gain[voice.taps_playing] : 0;
            if (!from_level && !aim_level) continue;
            if (!from_level && i < s.taps && at == 0) {
                const Tap& first = voice.taps[0];
                t.j = first.j; t.fraction = first.fraction;
                t.offset = first.offset; t.next = first.next; t.shift = first.shift;
                t.ka = first.ka; t.ga = first.ga; t.kb = first.kb; t.gb = first.gb; t.w = first.w;
            }
            // Levels move from the block's start to its end, a segment at a time.
            const i32 l0 = from_level + (aim_level - from_level) * i32(at) / i32(opxy::block_frames);
            const i32 l1 = from_level + (aim_level - from_level) * i32(at + segment) / i32(opxy::block_frames);
            t.step = voice.steps[i];
            u32 kt, kt1, wt;
            target(s, voice.progress, t.shift, kt, kt1, wt);
            if (t.w >= 65536) { t.ka = t.kb; t.ga = t.gb; t.w = 0; }
            const u32 w0 = t.w, w1 = steer(t, s, kt, kt1, wt);
            const bool mixing = w0 || w1;
            const i32 m0 = (l0 * s.wet) >> 15, m1 = (l1 * s.wet) >> 15;   // the scramble's part of the level
            const i32 a0 = (m0 * sine(65536 - w0)) >> 15, a1 = (m1 * sine(65536 - w1)) >> 15;
            const i32 b0 = (m0 * sine(w0)) >> 15, b1 = (m1 * sine(w1)) >> 15;
            player(s, stereo, mixing)(t, s, voice.sample.data, mix + at, segment, t.ga, t.gb,
                                      a0 << 16, (a1 - a0) * 4096, b0 << 16, (b1 - b0) * 4096, (l0 * s.dry) >> 15);
            t.w = w1;
        }
    }
    voice.taps_playing = s.taps;
}
}

void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    for (u32 i = 0; i < most_taps; ++i) {
        voice.taps[i] = Tap();
        voice.taps[i].random = 0x2545f491u + 0x61c88647u * i;
        voice.readers[i] = Reader();
    }
    voice.progress = 0;
    voice.bits = 1;
    voice.map = kCat;
    voice.hiss = false;
    voice.taps_playing = 0;
    voice.pitch = voice.chord = ~0u;   // steps to be worked out
    voice.cents = 0;
    voice.root = 65536;
    voice.slot = voice.into = voice.length = voice.carry = voice.iteration = voice.counted = 0;
    voice.moving_key = voice.gather_key = ~0u;
    voice.fresh = true;
    voice.presence = presence_full;
}

// A note: the window from its start, the iteration from step.
void start(Voice& voice) {
    voice.fresh = true;
    voice.presence = presence_full;
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    const bool stereo_out = block.output_channels == 2;
    const u32 words = stereo_out ? 2 * opxy::block_frames : opxy::block_frames;
    for (u32 i = 0; i < words; ++i) block.output[i] = 0;
    if (!voice.sample.frames || !voice.sample.data) return;
    const i32* p = block.parameters;
    const u32 end = block.region.end < voice.sample.frames ? block.region.end : voice.sample.frames;
    const u32 span = end > block.region.begin ? end - block.region.begin : 0;
    const bool hiss = choice(p[kSlice], slice_count) == 0;
    const u32 bits = hiss ? fitting_bits(tile_bits(p[kGrid]) + fine_extra, span) : tile_bits(p[kGrid]);
    if (!bits || span < shortest) return;
    const u32 map = choice(p[kMap], map_count);
    if (voice.fresh) { voice.hiss = hiss; voice.bits = bits; voice.map = map; }
    tune(voice, block, choice(p[kChrd], chord_count));
    // The slice path takes a new square or map at its next slice; the hiss
    // path, and a change between the two, fade through silence.
    const bool changing = !voice.fresh && (hiss != voice.hiss || (hiss && (bits != voice.bits || map != voice.map)));
    i32 mix[opxy::block_frames] = {};   // half of each sample's output
    if (voice.hiss) render_fine(voice, block, mix);
    else render_slices(voice, block, mix, tile_bits(p[kGrid]), map);

    const i32 limit = 0x20000000;   // half scale, halved
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        i32 half = mix[i];
        // The sample's own volume (unsigned Q27, unity 1 << 27), as the sampler applies it.
        if (block.region.level != opxy::sample_unity_level) half = (half >> 11) * i32(block.region.level >> 16);
        half = half > limit ? limit : half < -limit ? -limit : half;
        if (changing) { if (voice.presence) --voice.presence; }
        else if (voice.presence < presence_full) ++voice.presence;
        if (voice.presence < presence_full) half = (half >> 7) * i32(voice.presence);
        const i32 out = half * 2;
        if (stereo_out) { block.output[2 * i] = out; block.output[2 * i + 1] = out; }
        else block.output[i] = out;
    }
    voice.fresh = false;
    if (changing && !voice.presence) {
        if (hiss != voice.hiss) voice.fresh = true;   // the other path starts afresh, fading in
        else if (hiss) reshape(voice, decode(voice, block, bits, map));
        voice.hiss = hiss;
        voice.bits = bits;
        voice.map = map;
    }
}
}
