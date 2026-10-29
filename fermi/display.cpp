#include "display.h"

// Fermi's screen: the chain as a laboratory apparatus (tools/make_panel.py).
// Brass bobs on coil springs between two stands, a stroboscope on the
// plank clocked by a regulator pendulum, a felt damper on a lever.
//
// The screen cannot hear the voices. It runs its own copy of the latest
// note's chain with the voices' code (chain.h), a block of time at a time,
// so with the controls held still it is the same chain, step for step.
//
// The bobs swing far too fast to see, so the stroboscope lights them once
// a period of the struck mode, at the peak of that mode's own swing; the
// struck mode then stands still and only the energy's flow moves the shape.
// While the chain breathes, the arch ripples into higher modes and comes back
// clean at each return, when the strobe flashes; in chaos it
// shivers. A faint ghost holds the swing's recent extremes, a long exposure.
//
// Every control acts on a part. Energy is how far the chain is drawn, masses
// the number of bobs, springs the coils (conical for the quadratic law,
// straight and tall for the cubic), time the regulator's bob (up the rod
// fast, down it slow; while a note runs the pendulum swings once a breath),
// dispersion the bobs themselves (beads on a near-continuous spring sound
// harmonic; lumped into heavy bobs, the lattice disperses), mode the arch's
// humps, damping the felt pad's long lever, and the seed the starting shape
// itself: the arch drawn with its nudge (three times over), and a flagged pin
// on the bob it moves most, lifting off when the note lets the chain go.
namespace fermi {
namespace screen {
using namespace opxy::draw;
const u32 budget = 20;            // nodes changed a frame, at most
const i32 linger = 2048;          // Q10 s the strobe runs on after the last key is let go
const u32 catch_up = 172;         // blocks a frame at most (0.25 s)

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
    st.breaths = 0;
    st.glow = 0;
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
// The ghost: each quarter of the chain's swing, out along its highs and back
// along its lows, as one outline about the quarter's middle.
inline u32 ghost(DisplayState& st, Frame& frame, i32 spacing, bool shown) {
    if (!shown) {
        u32 changed = 0;
        for (u32 q = 0; q < 4; ++q) changed += conceal(frame, node::ghost + q);
        st.ghost_shown = 0;
        return changed;
    }
    const u32 n = st.chain.n;
    const u32 inverse = (1u << 30) / static_cast<u32>(spacing);   // once a frame
    const i32 quarter = ((plate::hook_right - plate::hook_left) * unit) / 4;
    u32 changed = 0;
    for (u32 q = 0; q < 4; ++q) {
        const i32 centre = plate::hook_left * unit + quarter * static_cast<i32>(q) + quarter / 2;
        Point pts[64];
        for (u32 i = 0; i < 32; ++i) {
            const i32 off = -44 * unit + static_cast<i32>(i) * 2907;
            const u32 rel = static_cast<u32>(centre + off - plate::hook_left * unit);
            const u32 p = ((rel >> 4) * inverse) >> 16;   // Q10 masses along the chain
            const u32 at = p >> 10, frac = p & 1023;
            const i32 h0 = at == 0 || at > n ? 0 : st.high[at - 1], h1 = at + 1 > n ? 0 : st.high[at];
            const i32 l0 = at == 0 || at > n ? 0 : st.low[at - 1], l1 = at + 1 > n ? 0 : st.low[at];
            const i32 h = h0 + (((h1 - h0) * static_cast<i32>(frac)) >> 10);
            const i32 l = l0 + (((l1 - l0) * static_cast<i32>(frac)) >> 10);
            pts[i].x = off;
            pts[i].y = -(h << 2) - 307;
            pts[63 - i].x = off;
            pts[63 - i].y = -(l << 2) + 307;
        }
        Frame::path_type& path = frame.paths[q];
        bool moved = !st.ghost_shown || !path.key;
        for (u32 i = 0; i < 64 && !moved; ++i)
            moved = magnitude(pts[i].y - path.points[i].y) >= 384;
        if (moved) {
            for (u32 i = 0; i < 64; ++i) path.points[i] = pts[i];
            st.keys[q] = st.keys[q] + 1 ? st.keys[q] + 1 : 1;
            path.key = st.keys[q];
            ++changed;
        }
        changed += put(frame, node::ghost + q, centre, plate::row_y * unit, unit, shade::ghost, stroke);
    }
    st.ghost_shown = 1;
    return changed;
}
}

inline void display_reset(DisplayState& state) {
    state.initialized = 0;
    state.header.reset();
    state.notes.reset();
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
        // The ghost's outlines start flat (and hidden), each point distinct.
        for (u32 r = 0; r < 4; ++r) {
            for (u32 i = 0; i < 32; ++i) {
                frame.paths[r].points[i].x = frame.paths[r].points[63 - i].x = -44 * unit + static_cast<i32>(i) * 2907;
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
        st.rotor = 0;
        st.ghost_shown = 0;
        st.initialized = 1;
        st.track = input.track;
    }
    // A note releases the chain; a new number of masses or struck mode
    // restrings it, as it does each voice's.
    if (input.note_serial != st.serial) {
        st.serial = input.note_serial;
        load(st, s);
        st.running = 1;
        st.linger = linger;
        for (u32 j = 0; j < chain::most; ++j) st.high[j] = st.low[j] = st.drawn[j];
    } else if (s.n != st.chain.n || s.m != st.chain.m) {
        load(st, s);
    }
    if (st.running) {
        if (held) st.linger = linger;
        else st.linger -= dt;
        if (st.linger <= 0) st.running = 0;
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
        // The stroboscope flashes at each predicted return.
        const u32 period = chain::exp2_q24(chain::period_log2(s) + 3 * 4096 - 24 * 4096);   // ticks
        if (period && st.chain.ticks >= (st.breaths + 1) * period) {
            st.breaths = st.chain.ticks / period;
            st.glow = 0;
        }
        // The pendulum swings once a breath, out at each flash.
        if (period) {
            u32 rem = st.chain.ticks % period, p = period;
            while (p > (1u << 20)) { p >>= 1; rem >>= 1; }
            st.phase = rem * 3600 / p;
        }
    }
    st.glow = st.glow > 4096 ? 4096 : st.glow + dt;

    // The header.
    static const char* const titles[8] = {"energy", "masses", "springs", "time", "dispersion", "mode", "damping", "seed"};
    char values[8][header_capacity];
    captions(values, s, input);
    page_header(st.header, input, frame, titles, values, node::header);

    // How far the chain is drawn: 4 px a unit of strain at no energy, 34 at full.
    const u32 left = snap ? 0 : opxy::draw::page_header_detail::remaining(static_cast<u32>(dt));
    const u32 slow = snap ? 0 : opxy::draw::page_header_detail::remaining(static_cast<u32>(dt) / 8);
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
        st.drawn[j] = ease(st.drawn[j], target, left);
        if (st.running) {
            st.high[j] = target > st.high[j] ? target : ease(st.high[j], target, slow);
            st.low[j] = target < st.low[j] ? target : ease(st.low[j], target, slow);
        }
    }

    // The parts the other controls turn, eased.
    st.slide = ease(st.slide, (plate::fast << 8) + (((plate::slow - plate::fast) * chain::amount(input.slots[chain::TIME])) >> 7), left);
    st.swing = ease(st.swing, st.running && !input.reduced_motion ? 30 << 8 : 0, left);
    st.damper = ease(st.damper, 70 - ((plate::lowered * chain::amount(input.slots[chain::DAMPING])) >> 15), left);
    st.lump = ease(st.lump, s.dispersion, left);
    st.lift = ease(st.lift, st.running ? 1024 : 0, left);
    u32 spent = 0;
    {
        // The regulator: its rod on the knife edge, the bob slid along it.
        const i32 sway = ((st.swing >> 8) * rotation(st.phase).cos) >> 10;   // tenths
        const Turn t = rotation(normal(sway));
        const i32 along = st.slide << 2;   // Q10 px
        spent += put_turned(frame, node::rod, plate::pendulum_x * unit, plate::pendulum_y * unit, unit, t.cos, t.sin, shade::rod, stroke);
        spent += put_turned(frame, node::regulator, plate::pendulum_x * unit - ((t.sin * along) >> 10),
                            plate::pendulum_y * unit + ((t.cos * along) >> 10), unit, t.cos, t.sin, shade::regulator, fill_and_stroke);
    }
    spent += put_angle(frame, node::damper, plate::pivot_x * unit, plate::pivot_y * unit, unit, st.damper, shade::damper, stroke);
    spent += put(frame, node::flash, plate::lamp_x * unit, plate::lamp_y * unit, unit,
                 !st.running ? shade::dark : st.glow < 61 ? shade::lit : st.glow < 184 ? shade::fading : shade::dark, stroke);

    const i32 spacing = static_cast<i32>(tables::spacing[st.chain.size]);
    spent += ghost(st, frame, spacing, st.running && !input.reduced_motion);
    u32 room = fresh ? 0xffffu : budget > spent ? budget - spent : 0;

    // The chain: a bob moves (half a pixel or more) with both its coils,
    // three nodes at a time, as many as the budget allows, resuming where the
    // last frame stopped; the coils always span the bobs as they stand.
    // A bob's radius is a share of the spacing that grows with dispersion,
    // from 0.085 (beads) to 0.3 (heavy bobs), in steps of 1/64.
    i32 scale = (spacing * (497 + ((1260 * st.lump) >> 15))) >> 15;   // radius / 5.6 px, Q10
    scale = scale > 2 * unit ? 2 * unit : scale < 16 ? 16 : (scale + 8) & ~15;
    const i32 groove = (plate::groove * scale) >> 10;
    const bool cubic = s.springs >= 16384;
    auto weight_x = [&](u32 j) { return plate::hook_left * unit + static_cast<i32>(j + 1) * spacing; };
    auto place_coil = [&](u32 j) -> u32 {   // the coil on weight j's left (j = n: the last, to the right hook)
        const i32 x0 = j == 0 ? plate::hook_left * unit : weight_x(j - 1) + groove;
        const i32 y0 = j == 0 ? plate::row_y * unit : plate::row_y * unit - (st.shown[j - 1] << 2);
        const i32 x1 = j == n ? plate::hook_right * unit : weight_x(j) - groove;
        const i32 y1 = j == n ? plate::row_y * unit : plate::row_y * unit - (st.shown[j] << 2);
        const i32 dx = x1 - x0, dy = y1 - y0;
        const u32 length = chain::root(static_cast<u32>((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4))) << 4;   // Q10
        const i32 safe = length < 64 ? 64 : static_cast<i32>(length);
        i32 c = (dx << 10) / safe, sn = (dy << 10) / safe;   // a few coils a frame
        c = c > unit ? unit : c;
        sn = sn > unit ? unit : sn < -unit ? -unit : sn;
        i32 coil_scale = static_cast<i32>(length / plate::coil);
        coil_scale = coil_scale < 64 ? 64 : coil_scale > 2 * unit ? 2 * unit : coil_scale;
        const u32 on = cubic ? node::straight + j : node::cone + j, off = cubic ? node::cone + j : node::straight + j;
        return put_turned(frame, on, (x0 + x1) / 2, (y0 + y1) / 2, coil_scale, c, sn, cubic ? shade::hard : shade::coil, stroke) +
               conceal(frame, off);
    };
    // Bobs and coils past the chain's end hide; a new spacing or coil
    // kind redraws everything, a group at a time.
    for (u32 j = n; j < chain::most && room; ++j) {
        const u32 changed = conceal(frame, node::mass + j) + conceal(frame, node::cone + j + 1) + conceal(frame, node::straight + j + 1);
        room -= changed > room ? room : changed;
    }
    const u32 from = st.rotor;
    for (u32 pass = 0; pass < n && room >= 3; ++pass) {
        const u32 j = (from + pass) % n;
        const Node& d = frame.nodes[node::mass + j];
        const Node& c = frame.nodes[cubic ? node::straight + j : node::cone + j];
        const bool stale = fresh || d.scale != scale || d.x != weight_x(j) || magnitude(st.drawn[j] - st.shown[j]) >= 128 ||
                           (c.scale == unit && c.x == hidden_x * unit);   // its coil is the other kind
        if (!stale) continue;
        st.shown[j] = st.drawn[j];
        u32 changed = put(frame, node::mass + j, weight_x(j), plate::row_y * unit - (st.shown[j] << 2), scale, shade::mass, fill_and_stroke);
        changed += place_coil(j);
        changed += place_coil(j + 1);
        room -= changed > room ? room : changed;
        st.rotor = (j + 1) % n;
    }

    // The seed's pin, its point on the bob's crown, lifted 4 px while the chain runs.
    if (!most) conceal(frame, node::pin);
    else put(frame, node::pin, weight_x(most - 1),
             plate::row_y * unit - (st.shown[most - 1] << 2) - ((plate::bob * scale) >> 10) - (st.lift << 2), unit, shade::pin, stroke);
}
}
