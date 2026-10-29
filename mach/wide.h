#pragma once
#include "flight.h"

// 64-bit integers as two 32-bit words, from 32-bit arithmetic only: the
// Blackfin firmware links no 64-bit or count-leading-zero helpers, and the
// voice wants none of their calls in its inner work. Products of 32-bit words
// are exact; shifts take the bits a fixed-point result needs.
namespace mach {
namespace wide {
struct W {
    u32 lo;
    i32 hi;
};

inline W make(i32 v) { W w; w.lo = static_cast<u32>(v); w.hi = v < 0 ? -1 : 0; return w; }
inline W make_u(u32 v) { W w; w.lo = v; w.hi = 0; return w; }
inline W power(u32 n) { W w; w.lo = n < 32 ? 1u << n : 0; w.hi = n < 32 ? 0 : static_cast<i32>(1u << (n - 32)); return w; }   // 2^n, n < 63

inline W neg(W w) { W r; r.lo = ~w.lo + 1u; r.hi = ~w.hi + (r.lo == 0 ? 1 : 0); return r; }
inline W add(W a, W b) { W r; r.lo = a.lo + b.lo; r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0); return r; }
inline W sub(W a, W b) { W r; r.lo = a.lo - b.lo; r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0); return r; }
inline bool negative(W w) { return w.hi < 0; }
inline bool zero(W w) { return !w.lo && !w.hi; }

// Unsigned 32 x 32 -> 64, from four 16 x 16 products.
inline W umul(u32 a, u32 b) {
    const u32 al = a & 0xffffu, ah = a >> 16, bl = b & 0xffffu, bh = b >> 16;
    const u32 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const u32 mid = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    W w;
    w.lo = (mid << 16) | (ll & 0xffffu);
    w.hi = static_cast<i32>(hh + (lh >> 16) + (hl >> 16) + (mid >> 16));
    return w;
}
inline u32 umul_hi(u32 a, u32 b) { return static_cast<u32>(umul(a, b).hi); }
inline u32 magnitude(i32 v) { return v < 0 ? 0u - static_cast<u32>(v) : static_cast<u32>(v); }
// Signed 32 x 32 -> 64.
inline W mul(i32 a, i32 b) {
    const W p = umul(magnitude(a), magnitude(b));
    return (a < 0) != (b < 0) ? neg(p) : p;
}

inline W shl(W w, u32 n) {
    if (!n) return w;
    W r;
    if (n < 32) { r.hi = static_cast<i32>((static_cast<u32>(w.hi) << n) | (w.lo >> (32 - n))); r.lo = w.lo << n; }
    else { r.hi = static_cast<i32>(w.lo << (n - 32)); r.lo = 0; }
    return r;
}
// w >> n (arithmetic), its low 32 bits.
inline u32 shr(W w, u32 n) {
    if (!n) return w.lo;
    if (n < 32) return (w.lo >> n) | (static_cast<u32>(w.hi) << (32 - n));
    return static_cast<u32>(w.hi >> (n - 32 < 31 ? n - 32 : 31));
}
// w >> n, held within +-limit.
inline i32 shr_within(W w, u32 n, i32 limit) {
    // The bits above the low word must all copy its sign for w >> n to fit.
    const i32 above = n < 32 ? w.hi >> n : w.hi >> 31;
    const i32 low = static_cast<i32>(shr(w, n));
    if (above != (low >> 31)) return above < 0 ? -limit : limit;
    return low > limit ? limit : low < -limit ? -limit : low;
}

// 1 / x for x in [0.5, 1) as Q32, returned as Q30, no division: Newton steps
// from a table's first guess. Precise: 16 guesses (within 1/32), two steps
// (within 2^-20). Quick: 64 guesses (within 1/128), one step (within 2^-14).
inline u32 reciprocal(u32 x, const u32* first, bool quick) {
    u32 y = quick ? first[16 + ((x >> 25) & 63)] : first[(x >> 27) & 15];
    for (int i = quick ? 1 : 0; i < 2; ++i) {
        const u32 e = umul_hi(x, y);
        y = shr(umul(y, 0x80000000u - e), 30);
    }
    return y;
}

// Leading zeros of a nonnegative 64-bit value.
inline u32 leading(W w) { return w.hi ? flight::clz(static_cast<u32>(w.hi)) : 32 + flight::clz(w.lo); }
// The top 32 bits after shifting left by n leading zeros.
inline u32 top(W w, u32 n) { return static_cast<u32>(shl(w, n).hi); }

// num / den * 2^q, saturated to +-(2^31 - 1), without division.
inline i32 ratio(W num, W den, i32 q, const u32* first, bool quick = false) {
    const i32 most = 2147483647;
    if (zero(num)) return 0;
    const bool flip = negative(num) != negative(den);
    if (zero(den)) return flip ? -most : most;
    if (negative(num)) num = neg(num);
    if (negative(den)) den = neg(den);
    const u32 nn = leading(num), nd = leading(den);
    const W p = umul(top(num, nn), reciprocal(top(den, nd), first, quick));
    const i32 shift = 62 + static_cast<i32>(nn) - static_cast<i32>(nd) - q;
    if (shift >= 64) return 0;
    if (shift <= 0) return flip ? -most : most;
    const u32 above = shift < 32 ? static_cast<u32>(p.hi) >> shift : 0;
    const u32 r = shr(p, static_cast<u32>(shift));
    const i32 v = above || r > static_cast<u32>(most) ? most : static_cast<i32>(r);
    return flip ? -v : v;
}

// The integer square root of a nonnegative 64-bit value below 2^62: the root
// of its top bits, refined by one Newton step.
inline u32 isqrt(W v, const u32* first, bool quick = false) {
    if (!v.hi) return flight::isqrt(v.lo);
    u32 n = 32 - flight::clz(static_cast<u32>(v.hi));
    n += n & 1;
    u32 y = flight::isqrt(shr(v, n)) << (n >> 1);
    if (y) {
        const u32 q = static_cast<u32>(ratio(v, make_u(y), 0, first, quick));
        y = (y >> 1) + (q >> 1) + (y & q & 1);
    }
    return y;
}
}
}
