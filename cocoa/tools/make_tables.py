#!/usr/bin/env python3
"""Writes tables.h, the constants Cocoa's engine and screen share. Run it after
changing a figure here and commit both.

The physics, in the units the tables use:

Wood (1930): a bubbly liquid's compressibility and density are the volume
averages of its parts', so with void fraction x its sound speed c is

    c^2 / c_l^2 = 1 / ((1 - x + x rho_g / rho_l) (1 - x + x K)),   K = rho_l c_l^2 / (rho_g c_g^2)

(liquid 1000 kg/m3 at 1500 m/s, air 1.2 kg/m3 at 343 m/s: K = 15937). A tiny
x makes the liquid nearly as compressible as air while it keeps water's
density, so c collapses: 0.1% of air halves it twice.

The column: the drink in a cylindrical mug of inner radius a = 40 mm, filled
to h, with a rigid side and bottom and a free surface. Its modes are
f = c / (2 pi) sqrt((j'_ml / a)^2 + ((2n - 1) pi / (2h))^2); the lowest,
(0, 0, 1), is the quarter-wave c / 4h, the key. The others' ratios to it
depend on h / a, which fill sets (20 to 80 mm).

The bubbles: eight classes of radius r_med 2^((i - 3.5) / 3.5) (an octave
either side of the median), log-normal in volume. Each rises at its Stokes
speed v = 2 g r^2 drho / (9 mu) (mu = 2 mPa s, hot milk; at most 0.25 m/s),
so the bottom of the column clears first. A quarter-wave mode weighs the
compressibility by p^2 = cos^2(pi z / 2h), largest at the bottom, so the
column rings as if its void fraction were x_eff = sum d_i W(s_i), where s_i
is the share of the column class i has cleared and
W(s) = 2 int_s^1 cos^2(pi u / 2) du = (1 - s) - sin(pi s) / pi.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATE, BLOCK = 44100.0, 64

# Wood's equation.
RHO_L, C_L, RHO_G, C_G = 1000.0, 1500.0, 1.2, 343.0
K = RHO_L * C_L ** 2 / (RHO_G * C_G ** 2)
DENSITY = 1 - RHO_G / RHO_L          # 1 - x (1 - rho_g / rho_l) is the density's share

# The mug and its drink.
RADIUS_MM = 40.0
FILL_MM = (20.0, 80.0)
MUG_MM = 90.0                        # the mug's inner height, for the walls' loading
STOKES = 2 * 9.81 * 1000.0 / (9 * 0.002)   # v = STOKES r^2, m/s for r in m
TOP_SPEED = 0.25                     # m/s: a millimetre bubble's terminal speed
FIZZ_UM = (120.0, 1200.0)            # the median radius
CLASSES = 8
SPREAD = 0.6                          # log-normal sigma of the radius (natural log)

# The liquid's modes: (m, l) Bessel j'_ml (0 for the plain column), n, level.
# The tap weights the axisymmetric modes (m = 0): the plain column's odd
# harmonics 1, 3, 5 carry the tone, the others shimmer above it. At 59 mm the
# (1,1,1) mode sits an octave up and the set is nearly consonant.
LIQUID = [((0, 0), 0.0, 1, 1.0),
          ((1, 1), 1.8412, 1, 0.35),
          ((0, 1), 3.8317, 1, 0.12),
          ((0, 0), 0.0, 2, 0.45),
          ((1, 1), 1.8412, 2, 0.08),
          ((0, 0), 0.0, 3, 0.22)]

# The cup's wall modes, empty: Hz, T60 (s) and level for its (2,0), (3,0) and
# (4,0) bending modes. Liquid loads the wall and lowers them (French 1983:
# f = f0 / sqrt(1 + 1.2 (h / H)^4)) and damps them (1 + 1.5 h / H).
CUPS = {"china": [(1650, 0.12, 0.15), (4480, 0.08, 0.08), (8950, 0.05, 0.04)],
        "glass": [(1150, 0.60, 0.20), (3200, 0.45, 0.12), (6100, 0.30, 0.06)],
        "tin": [(720, 0.70, 0.30), (1890, 0.50, 0.20), (3410, 0.35, 0.12)]}
# The liquid's fundamental T60 once clear, by cup.
LIQUID_T60 = {"china": 0.9, "glass": 1.5, "tin": 1.3}

Q16 = 65536


def octaves(v):
    return round(v * Q16)


def decay_rate(t60):
    """Amplitude loss in octaves a block for a T60 in seconds, Q16."""
    return octaves(math.log2(1000.0) * BLOCK / (t60 * RATE))


def fill_mm(i, n=64):
    return FILL_MM[0] + (FILL_MM[1] - FILL_MM[0]) * i / n


lines = ["#pragma once",
         "// Written by tools/make_tables.py: edit that and run it again.",
         "#include <opxy/types.hpp>",
         "",
         "namespace cocoa {",
         "namespace tables {",
         "using opxy::i16;",
         "using opxy::i32;",
         "using opxy::u32;",
         "using opxy::u16;",
         ""]


def emit(decl, values, per=12):
    lines.append(decl + " = {")
    for k in range(0, len(values), per):
        lines.append("    " + ", ".join(str(v) for v in values[k:k + per]) + ",")
    lines.append("};")


lines.append(f"// Wood: K = rho_l c_l^2 / (rho_g c_g^2) = {K:.1f}; (K - 1) / 2^14 and the density's share, Q31.")
lines.append(f"const i32 wood_k = {round((K - 1) / 16384 * 2 ** 31)};")
lines.append(f"const i32 wood_density = {round(DENSITY * 2 ** 31)};")
lines.append("// 2^15 / (K - 1), Q10: a void fraction (Q31) from 4^drop - 1 (Q16).")
lines.append(f"const u32 wood_inverse = {round(2 ** 15 / (K - 1) * 1024)};")

emit("// sin(2 pi i / 4096), Q15, read without interpolation (its spurs are 72 dB down).\nconst i16 sine[4096]",
     [round(32767 * math.sin(2 * math.pi * i / 4096)) for i in range(4096)], per=16)
emit("// log2(1 + i / 256), Q16.\nconst u32 log2_fraction[257]",
     [round(math.log2(1 + i / 256) * Q16) for i in range(257)])
emit("// 2^(i / 256), Q30.\nconst u32 exp2_fraction[257]",
     [min(round(2 ** (i / 256) * 2 ** 30), 2 ** 31 - 1) for i in range(257)])
emit("// W(s) = (1 - s) - sin(pi s) / pi at s = i / 512, Q30: the share of the\n"
     "// column's pressure-weighted compressibility a class still adds once it has\n"
     "// cleared s of the column from the bottom.\nconst i32 clearing[513]",
     [round(((1 - s) - math.sin(math.pi * s) / math.pi) * 2 ** 30) for s in [i / 512 for i in range(513)]])

# The classes: each one's share of the void (Q31) and its speed over the
# median's, in octaves (Q16).
weights = [math.exp(-((i - 3.5) / 3.5 * math.log(2)) ** 2 / (2 * SPREAD ** 2)) for i in range(CLASSES)]
total = sum(weights)
shares = [round(w / total * (2 ** 31 - 64)) for w in weights]
emit("// Each bubble class's share of the void (log-normal in radius, sigma 0.6), Q31.\nconst i32 class_share[8]", shares)
emit("// Each class's radius over the median's, in octaves, Q16: an octave either side.\nconst i32 class_radius[8]",
     [octaves((i - 3.5) / 3.5) for i in range(CLASSES)])

lines.append("// Stokes: log2 of 2 g drho / (9 mu) and of the top speed (m/s), Q16;")
lines.append("// log2 of the median radius at fizz 0 (m), Q16, and the span (octaves).")
lines.append(f"const i32 log2_stokes = {octaves(math.log2(STOKES))};")
lines.append(f"const i32 log2_top_speed = {octaves(math.log2(TOP_SPEED))};")
lines.append(f"const i32 log2_radius = {octaves(math.log2(FIZZ_UM[0] * 1e-6))};")
lines.append(f"const i32 radius_span = {octaves(math.log2(FIZZ_UM[1] / FIZZ_UM[0]))};")
lines.append("// log2 of 2^30 x a block's seconds (a clearing rate's numerator), Q16.")
lines.append(f"const i32 log2_block = {octaves(30 + math.log2(BLOCK / RATE))};")
lines.append("// log2 of a phase increment (Q32 cycles a sample) for 1 Hz, Q16.")
lines.append(f"const i32 log2_hertz = {octaves(32 - math.log2(RATE))};")
lines.append("// Minnaert: a bubble of radius r (m) rings at 3.26 / r Hz; the surface's")
lines.append("// foam bubbles that burst are ten times the rising ones'. log2 of 0.326, Q16.")
lines.append(f"const i32 log2_minnaert = {octaves(math.log2(0.326))};")

# The liquid's mode ratios over fill (65 points), log2, Q16.
rows = []
for k, ((m, l), j, n, level) in enumerate(LIQUID):
    row = []
    for i in range(65):
        h = fill_mm(i) / RADIUS_MM
        ratio = math.sqrt((j * 2 * h / math.pi) ** 2 + (2 * n - 1) ** 2)
        row.append(octaves(math.log2(ratio)))
    rows.append(row)
lines.append("// The liquid's modes (0,0,1), (1,1,1), (0,1,1), (0,0,2), (1,1,2), (0,0,3):")
lines.append("// log2 of each one's ratio to the quarter-wave at 65 fills from 20 to 80 mm, Q16.")
lines.append("const i32 liquid_ratio[6][65] = {")
for row in rows:
    lines.append("    {" + ", ".join(str(v) for v in row) + "},")
lines.append("};")
emit("// Each liquid mode's strike, Q15.\nconst i32 liquid_level[6]", [round(l * 32767) for _, _, _, l in LIQUID])
emit("// The liquid's fundamental decay once clear, by cup, octaves a block, Q16.\nconst i32 liquid_decay[3]",
     [decay_rate(LIQUID_T60[c]) for c in CUPS])

cups = list(CUPS.values())
lines.append("// The cup's modes, empty: log2 of their phase increments (Q16), decays")
lines.append("// (octaves a block, Q16) and strikes (Q15).")
lines.append("const i32 cup_pitch[3][3] = {")
for c in cups:
    lines.append("    {" + ", ".join(str(octaves(math.log2(f / RATE * 2 ** 32))) for f, _, _ in c) + "},")
lines.append("};")
lines.append("const i32 cup_decay[3][3] = {")
for c in cups:
    lines.append("    {" + ", ".join(str(decay_rate(t)) for _, t, _ in c) + "},")
lines.append("};")
lines.append("const i32 cup_level[3][3] = {")
for c in cups:
    lines.append("    {" + ", ".join(str(round(l * 32767)) for _, _, l in c) + "},")
lines.append("};")
emit("// The drink's loading of the wall over fill (65 points): log2 of the modes'\n"
     "// fall (Q16, negative) and their damping's growth (Q16 octaves).\nconst i32 cup_lowering[65]",
     [octaves(-0.5 * math.log2(1 + 1.2 * (fill_mm(i) / MUG_MM) ** 4)) for i in range(65)])
emit("const i32 cup_damping[65]", [octaves(math.log2(1 + 1.5 * fill_mm(i) / MUG_MM)) for i in range(65)])

# The spoon: a contact of duration tau excites a mode at f by about
# 1 / sqrt(1 + (f / fc)^4), fc = 0.6 / tau: 97 points over log2 (f / fc) from
# -6 to 6 octaves.
emit("// The spoon's strike at f over its contact's corner fc, 1 / sqrt(1 + (f/fc)^4),\n"
     "// at log2(f / fc) = -6 + i / 8, Q15.\nconst i32 spoon_gain[97]",
     [round(32767 / math.sqrt(1 + 2 ** (4 * (-6 + i / 8)))) for i in range(97)])
lines.append("// The spoon's contact: 5 ms (soft) to 0.15 ms (hard); log2 of its corner")
lines.append("// 0.6 / tau as an increment at the softest (Q16) and the span (octaves, Q16).")
lines.append(f"const i32 log2_soft_corner = {octaves(math.log2(0.6 / 0.005 / RATE * 2 ** 32))};")
lines.append(f"const i32 corner_span = {octaves(math.log2(0.005 / 0.00015))};")
lines += ["}", "}"]
(ROOT / "tables.h").write_text("\n".join(lines) + "\n")
print("wrote tables.h; K =", round(K, 1))
