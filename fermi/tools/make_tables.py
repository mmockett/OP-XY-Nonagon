#!/usr/bin/env python3
"""Writes tables.h: Fermi's chain constants (mode shapes, frequencies, the
leapfrog's energy factors), the sine table the partials read, and the energy
knob's calibration from tools/calibration.json (tools/calibrate.py)."""
import json
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAL = json.loads((ROOT / "tools" / "calibration.json").read_text())
NS = CAL["N"]
MS = CAL["m"]
VOICED = 16
BUDGET = 96           # mass-steps a block (chain.h)


def omega(N, k):
    return 2 * math.sin(k * math.pi / (2 * (N + 1)))


def arr(name, ctype, values, per=12):
    lines = []
    for i in range(0, len(values), per):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per]) + ",")
    return f"const {ctype} {name}[{len(values)}] = {{\n" + "\n".join(lines) + "\n};"


out = ["#pragma once",
       "// Written by tools/make_tables.py. Do not edit.",
       "#include <opxy/types.hpp>",
       "",
       "namespace fermi {",
       "namespace tables {",
       "using opxy::i16;", "using opxy::u16;", "using opxy::i32;", "using opxy::u32;",
       f"const u32 sizes = {len(NS)};",
       arr("masses", "u32", NS),
       arr("most_steps", "u32", [BUDGET // N for N in NS]),
       "// log2 of most_steps, Q12.",
       arr("log2_most_steps", "i32", [round(4096 * math.log2(BUDGET // N)) for N in NS]),
       ""]

# Half mode shapes: S[k][j] = sqrt(2/(N+1)) sin(j k pi/(N+1)) for k = 1..N and
# j = 1..N/2, Q15. The other half follows from S[N+1-j][k] = (-1)^(k+1) S[j][k].
shape, offset = [], []
for N in NS:
    offset.append(len(shape))
    for k in range(1, N + 1):
        for j in range(1, N // 2 + 1):
            shape.append(round(32767 * math.sqrt(2 / (N + 1)) * math.sin(j * k * math.pi / (N + 1))))
out += ["// Mode k's shape over the first half of the chain, Q15: shape[shape_at[n] + (k - 1) N/2 + j - 1].",
        arr("shape_at", "u32", offset), arr("shape", "i16", shape, 16), ""]

# Frequencies, Q15, k = 0..32 (0 unused), and the leapfrog's kinetic factor
# sqrt(1 - (w dt / 2)^2), Q15, for dt = 1/2, 1/4, 1/8.
w, lf = [], []
for N in NS:
    for k in range(33):
        w.append(round(32768 * omega(N, k)) if 0 < k <= N else 0)
        for sh in (1, 2, 3):
            dt = 2.0 ** -sh
            lf.append(round(32767 * math.sqrt(1 - (omega(N, k) * dt / 2) ** 2)) if 0 < k <= N else 0)
out += ["// The chain's dispersion, w_k = 2 sin(k pi / 2(N+1)), Q15: omega[n * 33 + k].",
        arr("omega", "u32", w),
        "// sqrt(1 - (w_k dt / 2)^2), Q15: kinetic[(n * 33 + k) * 3 + shift - 1].",
        arr("kinetic", "u16", lf), ""]

# A note's start, x_j = sin(j m pi/(N+1)) / d0 = shape * sqrt((N+1)/2) / d0, Q11.
# d0 = w_m, the bond strain's amplitude for unit displacement.
start, strain = [], []
for N in NS:
    for m in MS:
        start.append(round(2048 * math.sqrt((N + 1) / 2) / omega(N, m)))
        strain.append(round(32768 * omega(N, m)))
out += ["// sqrt((N+1)/2) / w_m, Q11: start[n * 4 + m - 1]; and w_m, Q15.",
        arr("start", "u32", start), arr("strain", "u32", strain),
        "// 2 / (N + 1), Q14: a mode's energy over the note's.",
        arr("inverse_energy", "u32", [round(16384 * 2 / (N + 1)) for N in NS]), ""]

# Partial ratios: the chain's, w_k / w_m, and the harmonic k / m, Q16, k = 0..16.
ratio, harmonic = [], []
for N in NS:
    for m in MS:
        for k in range(VOICED + 1):
            ratio.append(round(65536 * omega(N, k) / omega(N, m)) if 0 < k <= N else 0)
for m in MS:
    for k in range(VOICED + 1):
        harmonic.append(round(65536 * k / m))
out += ["// Partial k's ratio to the struck mode's: the chain's, ratio[(n * 4 + m - 1) * 17 + k], Q16;",
        "// and harmonic, harmonic[(m - 1) * 17 + k].",
        arr("ratio", "u32", ratio), arr("harmonic", "u32", harmonic), ""]

# Dispersion's reach: the blend toward the chain's own ratios that sets the top
# voiced partial (struck in mode 1) 500 cents flat, Q12. Below 1 for small
# chains, whose own law is already steeper; above 1 for large ones.
TOP_FLAT = 1 - 2 ** (-500 / 1200)
stretch = []
for N in NS:
    top = min(N, 10)
    stretch.append(round(4096 * min(8.0, TOP_FLAT / (1 - omega(N, top) / omega(N, 1) / top))))
out += ["// Dispersion 1's blend toward the chain's own ratios, Q12: the top voiced partial 500 cents flat.",
        arr("stretch", "u32", stretch), ""]

# The energy knob: log2 of the strengths pure, deep and thermal (Q12) at the
# five springs positions; and log2 of the breathing period in chain time
# units (Q12) at nine energy positions.
anchors, periods = [], []
for N in NS:
    for m in MS:
        for row in CAL["anchors"][f"{N},{m}"]:
            anchors += [round(4096 * math.log2(v)) for v in row]
        for row in CAL["periods"][f"{N},{m}"]:
            # a run too pure to breathe measurably takes the next position's period
            row = list(row)
            for i in range(len(row) - 2, -1, -1):
                if row[i] is None:
                    row[i] = row[i + 1]
            periods += [round(4096 * math.log2(v)) for v in row]
out += ["// log2 of the strengths at pure, deep and thermal, Q12: anchor[((n * 4 + m - 1) * 5 + s) * 3 + i].",
        arr("anchor", "i32", anchors),
        "// log2 of the breathing period in chain time, Q12: period[((n * 4 + m - 1) * 5 + s) * 9 + e].",
        arr("period", "i32", periods, 9), ""]

# 2^(i/256), Q15 (32768..65535 as u16 via the top bit), 257 entries.
out += ["// 2^(i / 256) - 1, Q16.",
        arr("exp2_fraction", "u16", [min(65535, round(65536 * (2 ** (i / 256) - 1))) for i in range(257)]), ""]

# The screen's: the masses' spacing, reciprocals and log2.
out += ["// The spacing of bobs between hooks 348 px apart, Q10.",
        arr("spacing", "u32", [round(348 * 1024 / (N + 1)) for N in NS]),
        "// 1 / i, Q16, for i = 0..64 (0 unused).",
        arr("reciprocal", "u32", [0] + [round(65536 / i) for i in range(1, 65)]),
        "// 1 / (i / 4) px, Q20, for lengths in quarter pixels, i = 0..255 (0 unused).",
        arr("inverse_length", "u32", [0] + [round(4 * 1048576 / i) for i in range(1, 256)]),
        "// sqrt(i / 256) x 65536, i = 0..256: square roots by table.",
        arr("sqrt_table", "u32", [round(65536 * math.sqrt(i / 256)) for i in range(257)]),
        "// log2(1 + i / 256), Q12.",
        arr("log2_fraction", "u16", [round(4096 * math.log2(1 + i / 256)) for i in range(257)]), ""]

# Damping's frequency law needs 1 / w^2 of the top voiced mode (Q8); the voice's tilt, sqrt(m / k)
# above the struck mode (Q15); and the body: one cycle of a sine with a little
# second and third harmonic, its peak at full scale, with the gain (Q15) that
# brings its RMS back to a sine's.
inverse_square = [round(256 / omega(N, min(N, VOICED)) ** 2) for N in NS]
tilt = []
for m in MS:
    for k in range(VOICED + 1):
        tilt.append(32767 if k <= m else round(32767 * math.sqrt(m / k)))
B2, B3 = 0.32, 0.12
body = [math.sin(2 * math.pi * i / 4096) + B2 * math.sin(4 * math.pi * i / 4096) + B3 * math.sin(6 * math.pi * i / 4096)
        for i in range(4096)]
peak = max(abs(v) for v in body)
body_gain = round(32767 * peak / math.sqrt(1 + B2 * B2 + B3 * B3))
out += ["// 1 / w_m^2, Q4: inverse_square[n * 4 + m - 1].",
        arr("inverse_square", "u32", inverse_square),
        "// The voice's tilt: 1 up to the struck mode, sqrt(m / k) above it, Q15: tilt[(m - 1) * 17 + k].",
        arr("tilt", "u16", tilt),
        f"// The body: sin + {B2} sin 2 + {B3} sin 3, peak normalized, Q15; body_gain restores a sine's RMS, Q15.",
        f"const u32 body_gain = {body_gain};",
        arr("body", "i16", [round(32767 * v / peak) for v in body], 16), ""]

# One cycle of sine, 4096 points, Q15.
out += ["// sin(2 pi i / 4096), Q15.",
        arr("sine", "i16", [round(32767 * math.sin(2 * math.pi * i / 4096)) for i in range(4096)], 16), ""]

out += ["}", "}", ""]
(ROOT / "tables.h").write_text("\n".join(out))
print("wrote tables.h")
