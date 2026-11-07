#include "display.h"

// Fermi's screen: the chain as a laboratory apparatus (tools/make_panel.py).
// Bobs on coil springs between two retort stands on a plank, a regulator
// pendulum on the right stand, a felt damper on a lever from the left one,
// each part in the grey of the encoder that moves it.
//
// The screen cannot hear the voices. It runs its own copy of the latest
// note's chain with the voices' code (chain.h), a block of time at a time,
// so with the controls held still it is the same chain, step for step.
//
// The bobs swing far too fast to see, so the chain is drawn as a strobe
// would light it: once a period of the struck mode, at the peak of that
// mode's own swing; the struck mode then stands still and only the energy's
// flow moves the shape. While the chain breathes, the arch ripples into
// higher modes and comes back clean at each return; in chaos it shivers.
// The chain is four reshaping outlines, redrawn whole every frame.
//
// Every control acts on a part. Energy is how far the chain is drawn, masses
// the number of bobs, strung at a near-even spacing so that more of them
// make a longer chain and move the stands (and the plank's halves, the
// pendulum and the damper with them) apart, springs the coils (conical for
// the quadratic law, straight and tall for the cubic), time the regulator's
// bob (up the rod fast, down it slow; while a note runs the pendulum swings
// once a breath),
// dispersion the bobs themselves (beads on a near-continuous spring sound
// harmonic; lumped into heavy bobs, the lattice disperses), mode the arch's
// humps, damping the felt pad, lowered on its lever to just above the
// chain as it is drawn back, and the seed the starting shape
// itself: the arch drawn with its nudge (three times over), and a flagged pin
// on the bob it moves most, lifting off when the note lets the chain go.
//
// Where the OP-XY tells the screen its amp envelope (SDK 1.10, amp.h), the
// chain runs for as long as the sound lasts, as the voice's chain runs on
// until the envelope ends it, and the chain is drawn as it is heard: the
// arch and the regulator's swing scaled by the envelope, swelling
// with its attack, holding at its sustain while keys are held and fading
// over its release, in 32 steps. The felt and the chain's own damping still
// take the swing down sooner. Without it, the screen keeps its own timing:
// the chain runs while keys are held and two seconds after.
namespace fermi {
namespace screen {
using namespace opxy::draw;
const i32 linger = 2048;          // Q10 s the chain runs on after the last key is let go
const u32 catch_up = 172;         // blocks a frame at most (0.25 s)
const i32 steps = 32;             // the envelope's level as the chain is drawn

inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
// Places a node upright when it is not already so; returns 1 if it changed.
inline u32 put(Frame& frame, u32 n, i32 x, i32 y, i32 scale, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == unit && d.sin == 0 && d.shade == grey && d.paint == paint)
        return 0;
    place(frame, n, x, y, scale, grey, paint);
    return 1;
}
inline u32 put_turned(Frame& frame, u32 n, i32 x, i32 y, i32 scale, i32 c, i32 s, u32 grey, Paint paint) {
    const Node& d = frame.nodes[n];
    if (d.x == x && d.y == y && d.scale == scale && d.cos == c && d.sin == s && d.shade == grey && d.paint == paint)
        return 0;
    place(frame, n, x, y, scale, grey, paint);
    Turn t = {c, s};
    turn(frame, n, t);
    return 1;
}
inline u32 conceal(Frame& frame, u32 n) { return put(frame, n, hidden_x * unit, hidden_y * unit, unit, 0, keep); }

// Captions, in the controls' units.
inline void put_number(char* out, u32 hundredths, const char* unit_text) {
    // 2 or 3 significant figures: 0.31, 3.9, 64, 120.
    char t[12];
    u32 at = 0;
    if (hundredths < 100) {
        t[at++] = '0'; t[at++] = '.';
        t[at++] = static_cast<char>('0' + hundredths / 10); t[at++] = static_cast<char>('0' + hundredths % 10);
    } else if (hundredths < 1000) {
        const u32 tenths = (hundredths + 5) / 10;
        if (tenths >= 100) { t[at++] = static_cast<char>('0' + tenths / 100); t[at++] = static_cast<char>('0' + tenths / 10 % 10); }
        else { t[at++] = static_cast<char>('0' + tenths / 10); t[at++] = '.'; t[at++] = static_cast<char>('0' + tenths % 10); }
    } else {
        u32 whole = (hundredths + 50) / 100;
        if (whole > 9999) whole = 9999;
        char d[5];
        u32 k = 0;
        while (whole) { d[k++] = static_cast<char>('0' + whole % 10); whole /= 10; }
        while (k) t[at++] = d[--k];
    }
    for (u32 i = 0; unit_text[i] && at < 7; ++i) t[at++] = unit_text[i];
    t[at] = 0;
    copy(out, header_capacity, t);
}
// 2^(l / 4096) in hundredths.
inline u32 hundredths_of(i32 l) {
    if (l < 7 * 4096) return ((chain::exp2_q24(l) >> 8) * 100u + 32768) >> 16;   // under 128
    return ((chain::exp2_q24(l - 10 * 4096) >> 7) * 100u) >> 7;                  // the value x 2^14
}
inline void captions(char (&values)[8][header_capacity], const chain::Setup& s, const Input& input) {
    using chain::amount;
    // Energy: the chain's energy in the classic units of the 1955 runs, a
    // quadratic chain with alpha = 1/4 or a cubic one with beta = 1:
    // 4 (N+1) alpha^2 or (N+1) beta / 4, in strain-normalized coefficients.
    {
        // log2 of the energy, Q12, from the springs' coefficients.
        i32 l = 0;
        if (s.springs < 16384) {
            l = chain::log2_q12(4 * (s.n + 1)) + 2 * (chain::log2_q12(s.alpha ? s.alpha : 1) - 24 * 4096);
        } else {
            const u32 cubic = ((s.strength >> 10) * static_cast<u32>(s.springs)) >> 5;   // Q24
            l = chain::log2_q12(s.n + 1) - 2 * 4096 + chain::log2_q12(cubic ? cubic : 1) - 24 * 4096;
        }
        if (l >= -27213) put_number(values[chain::ENERGY], hundredths_of(l), "");   // 0.01 and up
        else {
            // Smaller: a digit and a power of ten, "2e-7".
            const i32 l10 = (l * 1233) >> 12;                 // log10, Q12 (floor)
            const i32 d = l10 >> 12;                          // floor
            const u32 mantissa = chain::exp2_q24(((l10 - (d << 12)) * 13607) >> 12);   // 10^frac, Q24
            u32 digit = (mantissa + (1u << 23)) >> 24;
            i32 power = -d;
            if (digit >= 10) { digit = 1; --power; }
            char t[8] = {static_cast<char>('0' + digit), 'e', '-', static_cast<char>('0' + (power > 9 ? 9 : power)), 0};
            copy(values[chain::ENERGY], header_capacity, t);
        }
    }
    {
        char t[4] = {static_cast<char>(s.n >= 10 ? '0' + s.n / 10 : '0' + s.n), static_cast<char>(s.n >= 10 ? '0' + s.n % 10 : 0), 0, 0};
        copy(values[chain::MASSES], header_capacity, t);
    }
    // Springs: the quadratic and cubic shares.
    if (s.springs == 0) copy(values[chain::SPRINGS], header_capacity, "quad");
    else if (s.springs >= 32768) copy(values[chain::SPRINGS], header_capacity, "cubic");
    else {
        const u32 c = (static_cast<u32>(s.springs) * 100 + 16384) >> 15, q = 100 - c;
        char t[8];
        u32 at = 0;
        if (q >= 10) t[at++] = static_cast<char>('0' + q / 10);
        t[at++] = static_cast<char>('0' + q % 10);
        t[at++] = ':';
        if (c >= 10) t[at++] = static_cast<char>('0' + c / 10);
        t[at++] = static_cast<char>('0' + c % 10);
        t[at] = 0;
        copy(values[chain::SPRINGS], header_capacity, t);
    }
    // Time: the breathing period, in seconds.
    put_number(values[chain::TIME], hundredths_of(chain::period_seconds_log2(s, amount(input.slots[chain::TIME]))), "s");
    // Dispersion: how far the highest voiced partial sits from harmonic, in cents.
    {
        const u32 count = s.n < chain::voiced ? s.n : chain::voiced;
        const i32 h = static_cast<i32>(tables::harmonic[(s.m - 1) * 17 + count]);
        const i32 r = static_cast<i32>(tables::ratio[(s.size * 4 + s.m - 1) * 17 + count]);
        const i32 blend = h + ((((r - h) >> 4) * s.stretch) >> 11);
        const i32 cents = ((chain::log2_q12(static_cast<u32>(blend)) - chain::log2_q12(static_cast<u32>(h))) * 1200) >> 12;
        char t[8];
        u32 at = 0;
        u32 c = static_cast<u32>(cents < 0 ? -cents : cents);
        if (c && cents < 0) t[at++] = '-';
        if (c >= 1000) c = 999;
        if (c >= 100) t[at++] = static_cast<char>('0' + c / 100);
        if (c >= 10) t[at++] = static_cast<char>('0' + c / 10 % 10);
        t[at++] = static_cast<char>('0' + c % 10);
        t[at++] = 'c';
        t[at] = 0;
        copy(values[chain::DISPERSION], header_capacity, t);
    }
    {
        char t[2] = {static_cast<char>('0' + s.m), 0};
        copy(values[chain::MODE], header_capacity, t);
    }
    // Damping: off, or the time to fall 60 dB.
    {
        const i32 d = amount(input.slots[chain::DAMPING]);
        if (!d) copy(values[chain::DAMPING], header_capacity, "off");
        else put_number(values[chain::DAMPING], hundredths_of(20098 - ((20098 * d) >> 15)), "s");
    }
    // Seed: the share of the note's energy its nudge takes.
    if (!s.seed) copy(values[chain::SEED], header_capacity, "off");
    else {
        const u32 percent = (chain::nudge_share(s) * 100 + 32768) >> 16;
        char t[5] = {static_cast<char>(percent >= 10 ? '0' + percent / 10 : '0' + percent),
                     static_cast<char>(percent >= 10 ? '0' + percent % 10 : '%'), static_cast<char>(percent >= 10 ? '%' : 0), 0, 0};
        copy(values[chain::SEED], header_capacity, t);
    }
}

inline i32 ease(i32 value, i32 target, u32 left) {   // left: Q16 share of the distance kept
    const i32 next = target + static_cast<i32>((static_cast<i32>(value - target) * static_cast<i32>(left >> 6)) >> 10);
    return magnitude(next - target) < 8 ? target : next;
}
inline i32 normal(i32 tenths) { return tenths < 0 ? tenths + 3600 : tenths; }
inline u32 put_angle(Frame& frame, u32 n, i32 x, i32 y, i32 scale, i32 tenths, u32 grey, Paint paint) {
    const Turn t = rotation(normal(tenths));
    return put_turned(frame, n, x, y, scale, t.cos, t.sin, grey, paint);
}

// A mass's strain-scaled displacement, Q12: x_j d0, so the struck mode's
// arch is 1 at its widest.
inline i32 strain(i32 x, i32 d0) { return ((x >> 10) * (d0 >> 5)) >> 8; }
// The row at a strobe.
inline void strobe(DisplayState& st, const chain::Setup& s) {
    const i32 d0 = static_cast<i32>(tables::strain[s.size * 4 + s.m - 1]);
    for (u32 j = 1; j <= st.chain.n; ++j) st.shape[j - 1] = strain(st.chain.x[j], d0);
}
// The struck mode's projection, of the displacement or the velocity: its
// sign is all the strobe needs, so a rough sum does.
inline i32 project(const i32* y, const chain::Chain& c) {
    const u32 half = c.n / 2;
    const i16* row = &tables::shape[tables::shape_at[c.size] + (c.m - 1) * half];
    const bool odd = c.m & 1;
    i32 sum = 0;
    for (u32 j = 1; j <= half; ++j) {
        const i32 a = y[j] >> 12, b = y[c.n + 1 - j] >> 12;
        sum += (odd ? a + b : a - b) * row[j - 1];
    }
    return sum;
}
inline void load(DisplayState& st, const chain::Setup& s) {
    chain::init(st.chain, s);
    st.owed = 0;
    st.strobe = 0;
    strobe(st, s);
}
// A block of time, exactly as a voice runs it, strobing the row at each peak
// of the struck mode's swing (its velocity turning from out to back while
// it is out): loading the springs shifts its period, so a clock would slip.
inline void block(DisplayState& st, const chain::Setup& s) {
    chain::Chain& c = st.chain;
    const u32 steps = chain::owed(c, s);
    for (u32 i = 0; i < steps; ++i) {
        chain::step(c, s);
        c.ticks += s.dt_ticks;
        const i32 v = project(c.v, c);
        if (static_cast<i32>(st.strobe) > 0 && v <= 0 && project(c.x, c) > 0) strobe(st, s);
        st.strobe = static_cast<u32>(v);
    }
    chain::damp(c, s);
    chain::guard(c, s);
}
// Where the chain hangs for its number of masses, Q10 px: the bobs strung at
// near-even spacing, the span between the hooks growing by a step a size.
struct Layout {
    i32 hook_left, hook_right, spacing;
};
inline Layout layout(u32 size, u32 n) {
    Layout l;
    const i32 span = (plate::span + plate::span_step * static_cast<i32>(size)) * unit;
    l.spacing = span / static_cast<i32>(n + 1);
    l.hook_left = plate::centre * unit - span / 2;
    l.hook_right = l.hook_left + l.spacing * static_cast<i32>(n + 1);
    return l;
}
// The chain, drawn whole every frame as four reshaping outlines, a quarter
// of its springs each: a thin ribbon out along the string, over the top of
// each bob and along each coil's turns, and back under them, so the stroke
// draws each bob as a hollow ring and each coil as a zigzag. A quarter ends
// at a bob's right rim (or a hook), where the next begins.
// The angle of (x, y), y up, tenths of a degree, -1800..1800: a CORDIC in
// 32-bit integers (|x| and |y| under about 2^28).
inline i32 angle_of(i32 x, i32 y) {
    static const i32 step[14] = {7200, 4250, 2246, 1140, 572, 286, 143, 72, 36, 18, 9, 4, 2, 1};   // atan 2^-i, 1/160 degree
    i32 a = 0;
    if (x < 0) { x = -x; y = -y; a = y > 0 ? -28800 : 28800; }
    for (u32 i = 0; i < 14; ++i) {
        const i32 nx = y > 0 ? x + (y >> i) : x - (y >> i);
        const i32 ny = y > 0 ? y - (x >> i) : y + (x >> i);
        a += y > 0 ? step[i] : -step[i];
        x = nx;
        y = ny;
    }
    a /= 16;
    return a > 1800 ? a - 3600 : a < -1800 ? a + 3600 : a;
}
const i32 ribbon = 307;            // half the ribbon's thickness, Q10 px (0.3)
const u32 side = 32;               // points a side
struct Strand { i32 x[side], y[side]; u32 count; };
inline void add(Strand& t, i32 x, i32 y) { if (t.count < side) { t.x[t.count] = x; t.y[t.count] = y; ++t.count; } }
// Fills a side out to its 32 points, splitting its longest segment in turn.
inline void pad(Strand& t) {
    while (t.count < side) {
        u32 at = 1;
        i32 longest = -1;
        for (u32 i = 1; i < t.count; ++i) {
            const i32 d = magnitude(t.x[i] - t.x[i - 1]) + magnitude(t.y[i] - t.y[i - 1]);
            if (d > longest) { longest = d; at = i; }
        }
        for (u32 i = t.count; i > at; --i) { t.x[i] = t.x[i - 1]; t.y[i] = t.y[i - 1]; }
        t.x[at] = (t.x[at - 1] + t.x[at + 1]) / 2;
        t.y[at] = (t.y[at - 1] + t.y[at + 1]) / 2;
        ++t.count;
    }
}
// Points a side for a quarter of `spacings` springs and `bobs` bobs, with
// `arc` points over each bob and `turns` on each coil.
inline u32 needed(u32 spacings, u32 bobs, bool hook, u32 arc, u32 turns) { return 1 + spacings * turns + bobs * arc + (hook ? 1 : 0); }
}

inline void display_reset(DisplayState& state) {
    state.initialized = 0;
    state.header.reset();
    state.notes.reset();
    nonagon_amp::reset(state.amp);
}

inline void display(DisplayState& st, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    chain::Setup s;
    chain::configure(s, input.slots);
    st.notes.update(input);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const bool fresh = !st.initialized || st.track != input.track;
    const bool snap = fresh || input.reduced_motion;
    const i32 dt = clamp(input.elapsed, 0, 256);
    if (fresh) {
        for (u32 n = 0; n < Frame::node_count; ++n) conceal(frame, n);
        // The chain's outlines start flat (and hidden), each point distinct.
        for (u32 r = 0; r < 4; ++r) {
            for (u32 i = 0; i < 32; ++i) {
                frame.paths[r].points[i].x = frame.paths[r].points[63 - i].x = -27 * unit + static_cast<i32>(i) * 1783;
                frame.paths[r].points[i].y = -307;
                frame.paths[r].points[63 - i].y = 307;
            }
            st.keys[r] = 1;
            frame.paths[r].key = 1;
        }
        load(st, s);
        st.running = 0;
        st.linger = 0;
        st.serial = input.note_serial;
        st.initialized = 1;
        st.track = input.track;
        nonagon_amp::reset(st.amp);
    }
    // With the amp envelope known, the chain runs until the envelope ends
    // the sound and is drawn at the envelope's level; without it, it runs
    // while keys are held and lingers after.
    const bool follows = nonagon_amp::follow(st.amp, input, input.note_serial != st.serial);
    const i32 heard = follows ? nonagon_amp::level_steps(st.amp, steps) : steps;
    // A note releases the chain; a new number of masses or struck mode
    // restrings it, as it does each voice's.
    if (input.note_serial != st.serial) {
        st.serial = input.note_serial;
        load(st, s);
        st.running = 1;
        st.linger = linger;
    } else if (s.n != st.chain.n || s.m != st.chain.m) {
        load(st, s);
    }
    if (st.running) {
        if (held) st.linger = linger;
        else st.linger -= dt;
        if (follows ? !nonagon_amp::sounding(st.amp) || !heard : st.linger <= 0) st.running = 0;
    }
    if (st.running) {
        st.owed += static_cast<u32>(dt) * 44100u;   // Q16 blocks
        u32 blocks = 0;
        while (st.owed >= 65536 && blocks < catch_up) {
            block(st, s);
            st.owed -= 65536;
            ++blocks;
        }
        if (st.owed >= 65536) st.owed = 0;   // too far behind: let it go
        // The pendulum swings once a breath, out at each predicted return.
        const u32 period = chain::exp2_q24(chain::period_log2(s) + 3 * 4096 - 24 * 4096);   // ticks
        if (period) {
            u32 rem = st.chain.ticks % period, p = period;
            while (p > (1u << 20)) { p >>= 1; rem >>= 1; }
            st.phase = rem * 3600 / p;
        }
    }

    // The header.
    static const char* const titles[8] = {"energy", "masses", "springs", "time", "dispersion", "mode", "damping", "seed"};
    char values[8][header_capacity];
    captions(values, s, input);
    page_header(st.header, input, frame, titles, values, node::header);

    // How far the chain is drawn: 4 px a unit of strain at no energy, 34 at full.
    const u32 left = snap ? 0 : opxy::draw::page_header_detail::remaining(static_cast<u32>(dt));
    st.reach = ease(st.reach, 1024 + ((7680 * s.energy) >> 15), left);
    const u32 n = st.chain.n;
    // Between notes the chain is drawn back in the next note's starting
    // shape, the seed's nudge drawn three times its size so it reads; the pin
    // marks the bob the nudge moves most (0: none).
    i32 start[chain::most + 2];
    if (!st.running) {
        chain::shape(start, s);
        if (s.seed) {
            chain::Setup plain = s;
            plain.seed = 0;
            i32 struck[chain::most + 2];
            chain::shape(struck, plain);
            const i32 keep = static_cast<i32>(chain::root(65536 - chain::nudge_share(s)));   // Q8
            for (u32 j = 1; j <= n; ++j) start[j] += 2 * (start[j] - (struck[j] >> 8) * keep);
        }
    }
    u32 most = 0;
    if (s.seed) {
        i32 r[chain::most + 2], hardest = 0;
        chain::nudge(r, s);
        for (u32 j = 1; j <= n; ++j)
            if (magnitude(r[j]) > hardest) { hardest = magnitude(r[j]); most = j; }
    }
    const i32 d0 = static_cast<i32>(tables::strain[s.size * 4 + s.m - 1]);
    for (u32 j = 0; j < n; ++j) {
        const i32 target_strain = st.running ? st.shape[j] : strain(start[j + 1], d0);
        i32 target = (target_strain * st.reach) >> 12;   // px Q8
        target = clamp(target, -8704, 8704);
        if (follows && st.running) target = target * heard / steps;
        st.drawn[j] = ease(st.drawn[j], target, left);
        st.shown[j] = st.drawn[j];
    }

    // The parts the other controls turn, eased.
    st.slide = ease(st.slide, (plate::fast << 8) + (((plate::slow - plate::fast) * chain::amount(input.slots[chain::TIME])) >> 7), left);
    st.swing = ease(st.swing, st.running && !input.reduced_motion ? (30 << 8) * heard / steps : 0, left);
    {
        // The felt: its pad's foot lowered by damping from 34 px above the
        // top of the chain as energy draws it back to 4 px above it, its top
        // kept 14 px under the header, the lever turned to put it there.
        const i32 damping = chain::amount(input.slots[chain::DAMPING]);
        i32 top = plate::row_y * unit - (st.reach << 2) - 34 * unit + ((30 * unit * damping) >> 15) - plate::pad * unit;
        top = top < 52 * unit ? 52 * unit : top;
        const i32 sine_q14 = ((top - plate::pivot_y * unit) << 4) / plate::lever;
        const i32 cube = (((sine_q14 * sine_q14) >> 14) * sine_q14) >> 14;
        const i32 tenths16 = ((sine_q14 + cube / 6) * 573) >> 10;   // asin, sixteenths of a tenth of a degree
        st.damper = snap ? tenths16 : ease(st.damper, tenths16, left);
    }
    st.lump = ease(st.lump, s.dispersion, left);
    st.lift = ease(st.lift, st.running ? 1024 : 0, left);
    const Layout l = layout(st.chain.size, st.chain.n);
    u32 spent = 0;
    // The stands, the plank's halves, the pendulum and the damper, where the
    // chain's span puts them.
    const i32 left_stand = l.hook_left - plate::stand_out * unit, right_stand = l.hook_right + plate::stand_out * unit;
    spent += put(frame, node::plank_left, l.hook_left - plate::plank_out * unit, plate::plank_y * unit, unit, shade::plank, fill_and_stroke);
    spent += put(frame, node::plank_right, l.hook_right + plate::plank_out * unit, plate::plank_y * unit, unit, shade::plank, fill_and_stroke);
    spent += put(frame, node::stand_left, left_stand, plate::row_y * unit, unit, shade::stand, fill_and_stroke);
    spent += put(frame, node::stand_right, right_stand, plate::row_y * unit, unit, shade::stand, fill_and_stroke);
    {
        // The regulator: its rod on the knife edge, the bob slid along it.
        const i32 sway = ((st.swing >> 8) * rotation(st.phase).cos) >> 10;   // tenths
        const Turn t = rotation(normal(sway));
        const i32 along = st.slide << 2;   // Q10 px
        const i32 px = l.hook_right + plate::pendulum_out * unit, py = plate::pendulum_y * unit;
        spent += put_turned(frame, node::rod, px, py, unit, t.cos, t.sin, shade::rod, stroke);
        spent += put_turned(frame, node::regulator, px - ((t.sin * along) >> 10), py + ((t.cos * along) >> 10), unit, t.cos, t.sin,
                            shade::regulator, fill_and_stroke);
    }
    spent += put_angle(frame, node::damper, left_stand, plate::pivot_y * unit, unit, st.damper >> 4, shade::damper, fill_and_stroke);

    const i32 spacing = l.spacing;
    (void)spent;

    // The chain, whole, every frame: four outlines, a quarter of its springs
    // each (screen::Strand). A bob's radius is a share of the spacing that
    // grows with dispersion, from 0.085 (beads) to 0.3 (heavy bobs); it never
    // leaves its string under 6.4 px, and is never under 1.5 px.
    i32 radius = (spacing * (87 + ((220 * st.lump) >> 15))) >> 10;   // Q10 px
    const i32 roomiest = (spacing - 6554) / 2;
    radius = radius > roomiest ? roomiest : radius;
    radius = radius < 1536 ? 1536 : radius;
    const bool cubic = s.springs >= 16384;
    // Each quarter's springs: the first (n + 1) % 4 quarters take one more.
    u32 first[5];
    first[0] = 0;
    for (u32 q = 0; q < 4; ++q) first[q + 1] = first[q] + (n + 1) / 4 + (q < (n + 1) % 4 ? 1 : 0);
    // The most points over each bob and turns on each coil that every quarter can hold.
    u32 arc = 5, turns = 7;
    for (;;) {
        bool fits = true;
        for (u32 q = 0; q < 4 && fits; ++q) {
            const u32 springs = first[q + 1] - first[q];
            const bool hook = first[q + 1] == n + 1;
            fits = needed(springs, hook ? springs - 1 : springs, hook, arc, turns) <= side;
        }
        if (fits) break;
        if (turns > 2 || (arc == 3 && turns > 0)) --turns;
        else if (arc == 5) { arc = 3; turns = 7; }
        else break;
    }
    // Each bob's joins: the coils meet its rim on the line to their other
    // end, so the ribbon never folds back where it turns onto the rim; the
    // arc runs over the top between them going out, under the bottom coming back.
    auto bob_x = [&](u32 j) { return l.hook_left + static_cast<i32>(j + 1) * spacing; };
    auto bob_y = [&](u32 j) { return plate::row_y * unit - (st.shown[j] << 2); };
    const i32 edge = (ribbon * 573) / radius;   // where the ribbon's edge meets the rim, tenths of a degree off the join
    i32 toward_left[chain::most], toward_right[chain::most];   // tenths, counterclockwise from the right, y up
    for (u32 j = 0; j < n; ++j) {
        const i32 lx = j == 0 ? l.hook_left : bob_x(j - 1), ly = j == 0 ? plate::row_y * unit : bob_y(j - 1);
        const i32 rx = j + 1 == n ? l.hook_right : bob_x(j + 1), ry = j + 1 == n ? plate::row_y * unit : bob_y(j + 1);
        toward_left[j] = angle_of(lx - bob_x(j), bob_y(j) - ly);
        if (toward_left[j] < 0) toward_left[j] += 3600;
        toward_right[j] = angle_of(rx - bob_x(j), bob_y(j) - ry);
    }
    auto rim_x = [&](u32 j, i32 tenths) { return bob_x(j) + ((radius * sine(tenths + 900)) >> 10); };
    auto rim_y = [&](u32 j, i32 tenths) { return bob_y(j) - ((radius * sine(tenths)) >> 10); };
    for (u32 q = 0; q < 4; ++q) {
        const i32 left_x = first[q] == 0 ? l.hook_left : bob_x(first[q] - 1);
        const i32 right_x = first[q + 1] == n + 1 ? l.hook_right : bob_x(first[q + 1] - 1);
        const i32 cx = (left_x + right_x) / 2, cy = plate::row_y * unit;
        Strand out, back;   // relative to (cx, cy); back is built left to right, then reversed
        out.count = back.count = 0;
        if (first[q]) {   // the previous quarter's last bob, where its arc ended
            const u32 p = first[q] - 1;
            add(out, rim_x(p, toward_right[p] + edge) - cx, rim_y(p, toward_right[p] + edge) - cy);
            add(back, rim_x(p, toward_right[p] - edge) - cx, rim_y(p, toward_right[p] - edge) - cy);
        }
        for (u32 i = first[q]; i < first[q + 1]; ++i) {
            const bool hook = i == n;
            // The coil from the left hook or the last bob's join to this bob's join or the right hook.
            const i32 px = i == 0 ? l.hook_left : rim_x(i - 1, toward_right[i - 1]);
            const i32 py = i == 0 ? cy : rim_y(i - 1, toward_right[i - 1]);
            const i32 ex = hook ? l.hook_right : rim_x(i, toward_left[i]), ey = hook ? cy : rim_y(i, toward_left[i]);
            const i32 dx = ex - px, dy = ey - py;
            const i32 length = static_cast<i32>(chain::root(static_cast<u32>((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4))) << 4);
            const i32 safe = length < 64 ? 64 : length;
            const i32 nx = -((dy << 10) / safe), ny = (dx << 10) / safe;   // the unit normal, Q10
            // The ribbon's half-thickness across the coil, so its ends are square to it.
            const i32 ox = (nx * ribbon) >> 10, oy = (ny * ribbon) >> 10;
            if (i == 0) {
                add(out, px - cx - ox, py - cy - oy);
                add(back, px - cx + ox, py - cy + oy);
            }
            // A coil stretched steeper than 2 in 1 is drawn straight, pulled taut.
            const u32 coil = magnitude(dy) > 2 * magnitude(dx) ? 0 : turns;
            for (u32 k = 1; k <= coil; ++k) {
                const i32 t = static_cast<i32>((k << 10) / (turns + 1));   // Q10
                // Conical for the quadratic law (opening along it), straight for the cubic.
                const i32 wide = cubic ? 147 : 26 + ((141 * t) >> 10);   // Q10 of the coil's length
                // Tapered toward its ends (4 t (1 - t)), and never wider than a turn's step.
                i32 swing = ((((length >> 4) * wide >> 6) >> 2) * (((t * (1024 - t)) >> 10) << 2)) >> 8;   // Q10 px
                const i32 cap = length / static_cast<i32>(turns + 1);
                swing = swing > cap ? cap : swing;
                if (k & 1) swing = -swing;
                const i32 x = px + ((dx >> 4) * t >> 6) + ((nx * swing) >> 10) - cx;
                const i32 y = py + ((dy >> 4) * t >> 6) + ((ny * swing) >> 10) - cy;
                add(out, x - ox, y - oy);
                add(back, x + ox, y + oy);
            }
            if (hook) {
                add(out, ex - cx - ox, ey - cy - oy);
                add(back, ex - cx + ox, ey - cy + oy);
            } else {
                // Over the top going out, under the bottom coming back, each from join to join.
                // Where both coils come in from one side, that side's arc is
                // short: it takes fewer points (a point at its middle at the
                // least), each at least a fifth of a pixel from the next.
                const i32 top0 = toward_left[i] - edge, top1 = toward_right[i] + edge;
                const i32 bottom0 = toward_left[i] + edge, bottom1 = toward_right[i] - edge + 3600;
                const i32 apart = (205 * 573) / radius;   // tenths of a degree a fifth of a pixel spans
                for (u32 side_of = 0; side_of < 2; ++side_of) {
                    const i32 from = side_of ? bottom0 : top0, to = side_of ? bottom1 : top1;
                    const i32 span = side_of ? to - from : from - to;
                    u32 points = span <= apart ? 1 : static_cast<u32>(span / apart) + 1;
                    points = points > arc ? arc : points;
                    for (u32 a = 0; a < points; ++a) {
                        const i32 angle = points == 1 ? (from + to) / 2 : from + (to - from) * static_cast<i32>(a) / static_cast<i32>(points - 1);
                        add(side_of ? back : out, rim_x(i, angle) - cx, rim_y(i, angle) - cy);
                    }
                }
            }
        }
        pad(out);
        pad(back);
        Frame::path_type& path = frame.paths[q];
        bool moved = !path.key;
        for (u32 i = 0; i < side && !moved; ++i) {
            const Point& o = path.points[i];
            const Point& b = path.points[2 * side - 1 - i];
            moved = magnitude(o.x - clamp(out.x[i], -63 * unit, 63 * unit)) >= 128 || magnitude(o.y - clamp(out.y[i], -63 * unit, 63 * unit)) >= 128 ||
                    magnitude(b.x - clamp(back.x[i], -63 * unit, 63 * unit)) >= 128 || magnitude(b.y - clamp(back.y[i], -63 * unit, 63 * unit)) >= 128;
        }
        const Node& d = frame.nodes[node::chain + q];
        if (moved || fresh || d.x != cx) {
            for (u32 i = 0; i < side; ++i) {
                path.points[i].x = clamp(out.x[i], -63 * unit, 63 * unit);
                path.points[i].y = clamp(out.y[i], -63 * unit, 63 * unit);
                path.points[2 * side - 1 - i].x = clamp(back.x[i], -63 * unit, 63 * unit);
                path.points[2 * side - 1 - i].y = clamp(back.y[i], -63 * unit, 63 * unit);
            }
            st.keys[q] = st.keys[q] + 1 ? st.keys[q] + 1 : 1;
            path.key = st.keys[q];
        }
        put(frame, node::chain + q, cx, cy, unit, shade::chain, stroke);
    }

    // The seed's pin, its point on the bob's crown, lifted 4 px while the chain runs.
    if (!most) conceal(frame, node::pin);
    else put(frame, node::pin, bob_x(most - 1), bob_y(most - 1) - radius - (st.lift << 2), unit, shade::pin, stroke);
}
}
