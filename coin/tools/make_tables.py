#!/usr/bin/env python3
"""Writes tables.h, Coin's constants: the log2/exp2 tables the engine and the
screen share, the disk materials' modes and the bases' fixed resonances. Run
it after changing a number here, and commit both."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATE = 44100

# The disk's modes: ratio to the ring (the key), T60 while it rolls (s), and
# level. A free circular plate's first modes are (2,0), (0,1) and (3,0) at
# about 1 : 1.73 : 2.33; the material shifts them a little (its Poisson ratio)
# and sets how long and how bright they ring.
DISKS = {
    "steel": ([1.0, 1.732, 2.330], [0.9, 0.6, 0.4], [1.0, 0.55, 0.40]),
    "brass": ([1.0, 1.727, 2.318], [0.55, 0.32, 0.2], [1.0, 0.40, 0.22]),
    "glass": ([1.0, 1.746, 2.355], [0.8, 0.7, 0.6], [0.85, 0.70, 0.60]),
    "wood":  ([1.0, 1.618, 2.205], [0.09, 0.06, 0.04], [2.0, 0.92, 0.50]),
}
# A real disk is never quite round, so each of its modes is a pair a little
# apart, and the pair beats. Each material's split (Hz) at the ring; the
# upper pair splits 1.6 times as much.
SPLIT = {"steel": 0.7, "brass": 1.1, "glass": 0.45, "wood": 1.6}

# The base's resonances: fixed frequencies (Hz), T60 (s) and level. Short and
# low, a body under the disk rather than tones of their own.
BASES = {
    "glass": ([1180], [0.1], [1.0]),
    "wood":  ([205], [0.06], [1.0]),
    "stone": ([640], [0.025], [0.9]),
}


def q(v, bits):
    return int(round(v * (1 << bits)))


def decay(t60):
    return math.exp(-math.log(1000) / (t60 * RATE))


lines = ["#pragma once",
         "// Written by tools/make_tables.py: edit that and run it again.",
         "#include <opxy/types.hpp>",
         "",
         "namespace coin {",
         "namespace tables {",
         "using opxy::i32;",
         "using opxy::u32;",
         ""]

lines.append("// log2(1 + i / 32), Q16, and 2^(i / 32), Q30: read with linear interpolation.")
lines.append("const u32 log2_of[33] = {" + ", ".join(str(q(math.log2(1 + i / 32), 16)) for i in range(33)) + "};")
lines.append("const u32 exp2_of[33] = {" + ", ".join(str(min(q(2 ** (i / 32), 30), 2**31 - 1)) for i in range(33)) + "};")
lines.append("")

names = list(DISKS)
lines.append("// Disk materials: " + ", ".join(names) + ".")
lines.append("// Each mode's ratio to the ring / 4 (Q31), its loss a sample while rolling (1 - r^2, Q31) and its level (Q31).")
lines.append("const i32 disk_ratio[4][3] = {" + ", ".join(
    "{" + ", ".join(str(q(r / 4, 31)) for r in DISKS[n][0]) + "}" for n in names) + "};")
lines.append("const i32 disk_loss[4][3] = {" + ", ".join(
    "{" + ", ".join(str(q(1 - decay(t) ** 2, 31)) for t in DISKS[n][1]) + "}" for n in names) + "};")
lines.append("const i32 disk_level[4][3] = {" + ", ".join(
    "{" + ", ".join(str(min(q(g / 2, 31), 2**31 - 1)) for g in DISKS[n][2]) + "}" for n in names) + "};   // halved")
lines.append("// Each material's split between a mode and its partner, as a phase increment.")
lines.append("const u32 split[4] = {" + ", ".join(str(round(SPLIT[n] / RATE * 2**32)) for n in names) + "};")
lines.append("")

bases = list(BASES)
lines.append("// Bases: " + ", ".join(bases) + ". Each one's resonance as 2r cos w (Q30), 1 - r^2 (Q31)")
lines.append("// and its input gain, level x sin w (Q31), so an impulse rings at its level.")
c1, c2, gain = [], [], []
for n in bases:
    f, t, g = BASES[n]
    w = [2 * math.pi * x / RATE for x in f]
    r = [decay(x) for x in t]
    c1.append([q(2 * ri * math.cos(wi), 30) for ri, wi in zip(r, w)])
    c2.append([q(1 - ri * ri, 31) for ri in r])
    gain.append([q(gi * math.sin(wi), 31) for gi, wi in zip(g, w)])
for name, table in (("base_c1", c1), ("base_loss", c2), ("base_gain", gain)):
    lines.append(f"const i32 {name}[3] = {{" + ", ".join(str(row[0]) for row in table) + "};")
lines.append("")
# sin(pi x) / x as a polynomial in x^2 (Taylor to x^10), Q28, for x in [0, 0.5].
coefficients = [(-1) ** k * math.pi ** (2 * k + 1) / math.factorial(2 * k + 1) for k in range(6)]
lines.append("// sin(pi x) / x = sum of c[k] x^(2k), Q28: Taylor's series to x^10, for 0 <= x <= 0.5.")
lines.append("const i32 sine_series[6] = {" + ", ".join(str(q(c, 28)) for c in coefficients) + "};")
lines.append("")
lines.append("}")
lines.append("}")
(ROOT / "tables.h").write_text("\n".join(lines) + "\n")
print("wrote", ROOT / "tables.h")
