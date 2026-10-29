#!/usr/bin/env python3
"""Writes tables.h: the log2 and exp2 tables Drop's physics reads, and the
constants it derives from the papers' measurements. Run it after changing a
number here and commit both files."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
Q16 = 65536
RATE = 44100
BLOCK = 64


def q16(octaves):
    return round(octaves * Q16)


log2_of = [round(math.log2(1 + i / 32) * Q16) for i in range(33)]
exp2_of = [min(2147483647, round(2 ** (i / 32) * 2 ** 30)) for i in range(33)]

# The liquids, at their boiling points: water, ethanol, liquid nitrogen.
# Capillary length l_c = sqrt(sigma / (rho g)).
liquids = [
    # name, sigma (N/m), rho (kg/m^3), Leidenfrost point (C), life against water
    ("water", 0.0589, 958, 200, 1.0),
    ("ethanol", 0.0177, 757, 165, 0.44),
    ("nitrogen", 0.00885, 807, -100, 0.25),
]
lc = [math.sqrt(s / (r * 9.81)) for _, s, r, _, _ in liquids]
lc_water = lc[0]
# A star's frequency is set by the capillary length alone (Ma & Burton; Brunet
# 2018): f ~ sqrt(g / l_c). Water's beats come at 23 Hz (Singla & Rivera 2020),
# twice the star's oscillation.
beat_offset = [q16(0.5 * math.log2(lc_water / c)) for c in lc]
# The lobes' wavelength is fixed too, so n ~ R: n = 13 at R = 24 mm for water
# (Ma & Burton), 1.85 mm of radius a lobe, scaling with l_c.
lobe_mm = [1.85 * c / lc_water for c in lc]
lobe_log = [q16(math.log2(m)) for m in lobe_mm]
leidenfrost = [t * 10 for _, _, _, t, _ in liquids]
life_offset = [q16(math.log2(l)) for *_, l in liquids]

block_rate = RATE / BLOCK
constants = {
    "beat_hz": q16(math.log2(23)),                      # water's beat rate on the plateau, log2 Hz
    "slowest_beat": q16(math.log2(0.5)),               # beat: 0.5 Hz to 23 Hz, the papers' rate
    "beat_span": q16(math.log2(46)),
    "per_block": q16(30 - math.log2(block_rate)),      # log2 of a whole life (2^30) over the blocks in a second
    "increment": q16(32 - math.log2(RATE)),            # log2 of a 1 Hz phase increment, Q32 cycles a sample
    "sphere_ratio": q16(math.log2(2 / 1.8)),           # the n = 2 star's rate at nu = 1.8, where it rounds
    "sphere_nu": q16(math.log2(1.8)),
    "gone_nu": q16(math.log2(0.25)),                   # the drop is gone (R ~ 0.46 mm of water)
    "half_second": q16(math.log2(0.5)),
    "life_span": q16(math.log2(120)),                  # life: 0.5 s to 60 s
    "sizzle_life": q16(math.log2(0.2)),                # just under the Leidenfrost point
}

lines = [
    "#pragma once",
    "// Written by tools/make_tables.py: edit that and run it again.",
    "#include <opxy/types.hpp>",
    "",
    "namespace drop {",
    "namespace tables {",
    "using opxy::i32;",
    "using opxy::u32;",
    "",
    "// log2(1 + i / 32), Q16, and 2^(i / 32), Q30: read with linear interpolation.",
    "const u32 log2_of[33] = {" + ", ".join(map(str, log2_of)) + "};",
    "const u32 exp2_of[33] = {" + ", ".join(map(str, exp2_of)) + "};",
    "",
    "// The liquids at their boiling points: water, ethanol, liquid nitrogen.",
    "// Capillary lengths " + ", ".join(f"{c * 1000:.2f}" for c in lc) + " mm.",
    "// Their Leidenfrost points, tenths of a degree C.",
    "const i32 leidenfrost[3] = {" + ", ".join(map(str, leidenfrost)) + "};",
    "// The star's beat rate over water's, octaves Q16: sqrt(l_c water / l_c).",
    "const i32 beat_offset[3] = {" + ", ".join(map(str, beat_offset)) + "};",
    "// log2 of the radius a lobe takes, mm Q16: " + ", ".join(f"{m:.2f}" for m in lobe_mm) + " mm.",
    "const i32 lobe_log[3] = {" + ", ".join(map(str, lobe_log)) + "};",
    "// How long a drop lives against water's, octaves Q16.",
    "const i32 life_offset[3] = {" + ", ".join(map(str, life_offset)) + "};",
    "",
]
for name, value in constants.items():
    lines.append(f"const i32 {name} = {value};")
lines += ["", "}", "}", ""]
(ROOT / "tables.h").write_text("\n".join(lines))
print("wrote tables.h")
