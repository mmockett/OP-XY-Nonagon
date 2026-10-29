#pragma once
// Written by tools/make_tables.py: edit that and run it again.
#include <opxy/types.hpp>

namespace drop {
namespace tables {
using opxy::i32;
using opxy::u32;

// log2(1 + i / 32), Q16, and 2^(i / 32), Q30: read with linear interpolation.
const u32 log2_of[33] = {0, 2909, 5732, 8473, 11136, 13727, 16248, 18704, 21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346, 38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207, 52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047, 65536};
const u32 exp2_of[33] = {1073741824, 1097253708, 1121280436, 1145833280, 1170923762, 1196563654, 1222764986, 1249540052, 1276901417, 1304861917, 1333434672, 1362633090, 1392470869, 1422962010, 1454120821, 1485961921, 1518500250, 1551751076, 1585730000, 1620452965, 1655936265, 1692196547, 1729250827, 1767116489, 1805811301, 1845353420, 1885761398, 1927054196, 1969251188, 2012372174, 2056437387, 2101467502, 2147483647};

// The liquids at their boiling points: water, ethanol, liquid nitrogen.
// Capillary lengths 2.50, 1.54, 1.06 mm.
// Their Leidenfrost points, tenths of a degree C.
const i32 leidenfrost[3] = {2000, 1650, -1000};
// The star's beat rate over water's, octaves Q16: sqrt(l_c water / l_c).
const i32 beat_offset[3] = {0, 22852, 40748};
// log2 of the radius a lobe takes, mm Q16: 1.85, 1.14, 0.78 mm.
const i32 lobe_log[3] = {58165, 12461, -23331};
// How long a drop lives against water's, octaves Q16.
const i32 life_offset[3] = {0, -77622, -131072};

const i32 beat_hz = 296456;
const i32 slowest_beat = -65536;
const i32 beat_span = 361992;
const i32 per_block = 1348174;
const i32 increment = 1086030;
const i32 sphere_ratio = 9962;
const i32 sphere_nu = 55574;
const i32 gone_nu = -131072;
const i32 half_second = -65536;
const i32 life_span = 452650;
const i32 sizzle_life = -152170;

}
}
