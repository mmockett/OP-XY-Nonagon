#pragma once
// The header's captions, in real units, for display.cpp.
#include <opxy/page_header.hpp>
#include "ghost.h"

namespace ghost {
namespace captions {
using namespace opxy::draw;

const char* const vowel_names[vowel_count] = {"air", "oo", "oh", "ah", "eh", "ee"};   // air: breath alone
const char* const titles[8] = {"ring", "voice", "split", "trade", "beat", "orbit", "near", "bed"};

inline char* put(char* out, u32 value) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = char('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = '\0';
    return out;
}
// value / 10^places as "1.5" or "0.03".
inline char* decimal(char* out, u32 value, u32 places) {
    u32 scale = 1;
    for (u32 i = 0; i < places; ++i) scale *= 10;
    out = put(out, value / scale);
    *out++ = '.';
    u32 rest = value % scale;
    for (u32 i = places; i; --i) { u32 s = 1; for (u32 k = 1; k < i; ++k) s *= 10; *out++ = char('0' + rest / s % 10); }
    *out = '\0';
    return out;
}
inline void append(char (&value)[header_capacity], const char* unit) {
    u32 at = 0;
    while (at < header_capacity - 1 && value[at]) ++at;
    for (u32 i = 0; unit[i] && at < header_capacity - 1; ++i) value[at++] = unit[i];
    value[at] = '\0';
}
inline void percent(char (&value)[header_capacity], u32 level16) {
    put(value, (level16 * 100 + 32768) >> 16);
    append(value, "%");
}

inline void fill(const Input& input, char (&values)[8][header_capacity]) {
    const u32 ring = ring_ms(level(input.slots[kRing]));
    if (ring < 1000) { put(values[kRing], ring / 10 * 10); append(values[kRing], "ms"); }
    else { decimal(values[kRing], (ring + 50) / 100, 1); append(values[kRing], "s"); }
    const u32 position = vowel_position(level(input.slots[kVoice]));
    copy(values[kVoice], header_capacity, vowel_names[(position + 32768) >> 16]);
    percent(values[kSplit], level(input.slots[kSplit]));
    // trade: seconds for the halves to swap and swap back.
    const u32 trade = trade_mhz(level(input.slots[kTrade]));
    if (!trade) copy(values[kTrade], header_capacity, "still");
    else if (trade >= 100) { decimal(values[kTrade], (10000 + trade / 2) / trade, 1); append(values[kTrade], "s"); }
    else { put(values[kTrade], (1000 + trade / 2) / trade); append(values[kTrade], "s"); }
    const u32 beat = beat_mhz(level(input.slots[kBeat]));
    if (!beat) copy(values[kBeat], header_capacity, "0Hz");
    else { decimal(values[kBeat], (beat + 50) / 100, 1); append(values[kBeat], "Hz"); }
    const u32 orbit = level(input.slots[kOrbit]), circle = orbit_mhz(orbit);
    if (!orbit) copy(values[kOrbit], header_capacity, "front");
    else if (!circle) percent(values[kOrbit], orbit_spread(orbit));
    else { decimal(values[kOrbit], (10000 + circle / 2) / circle, 1); append(values[kOrbit], "s"); }   // seconds a circle
    const u32 cm = near_cm(level(input.slots[kNear]));
    if (cm >= 100) { decimal(values[kNear], (cm + 5) / 10, 1); append(values[kNear], "m"); }
    else { put(values[kNear], cm); append(values[kNear], "cm"); }
    // bed: the sample's share, or without one the whisper's.
    percent(values[kBed], level(input.slots[kBed]));
}
}
}
