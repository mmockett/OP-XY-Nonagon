#!/usr/bin/env python3
"""Writes plate.h (Sand's plate: mode shapes, figures, their nodal outlines and
the grain click filters), panel.svg.fragment (the screen's artwork) and the
Figure labels and node count in definition.json. Run it after changing
anything here and commit all three. `--sheet out.svg` also writes a
sheet of every figure with its outlines, for checking them by eye.

The plate is a square with free edges, modelled the way Chladni's figures
usually are: mode (m, n, s) has the shape

    cos(m pi x) cos(n pi y) + s cos(n pi x) cos(m pi y)      x, y in [0, 1]

and a frequency proportional to m^2 + n^2, the + and - pair split a little,
as a real plate splits them. A figure is the mode the bow drives; the note
sounds at its frequency and the next seven modes up the plate ring above it.

Pure Python, no packages: it runs wherever the SDK does."""
import itertools
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARGS = sys.argv[1:]
SHEET = None
if '--sheet' in ARGS:
    at = ARGS.index('--sheet')
    SHEET = pathlib.Path(ARGS[at + 1])
    del ARGS[at:at + 2]
ID = "sand"

MODES = 7          # sounding modes per figure: the bowed mode and six above it, sounded in pairs
PATHS = 4          # mutable outlines (nodes 0..3)
POINTS = 64        # points per outline
SPLIT = 0.02       # how far a +/- pair splits about m^2 + n^2
HIGHEST = 9        # largest m or n in the plate's mode list
GAP = 1.1          # least ratio between neighbouring sounding modes

# The figures, in the order Figure sweeps them: from a single diagonal to a
# flower, each the bowed mode (m, n, s). The plate is held, as Chladni held
# his, by a clamp at its centre, so every figure (and every mode that rings)
# has a node there.
FIGURES = [(1, 2, 1), (1, 3, 1), (2, 3, 1), (1, 4, 1), (2, 4, -1), (3, 4, 1),
           (2, 5, 1), (2, 5, -1), (3, 5, 1), (1, 6, -1), (3, 6, 1), (4, 6, -1)]

# The view: the apparatus as an engraver draws it, from the front and a little
# to the left, above. A point on the plate's top, x left to right and y back
# to front (0..1), sits on screen at
#     (PX + (x - 0.5) W + (y - 0.5) S,  PY + (y - 0.5) H)
# so the square plate is a parallelogram W wide and H deep, leaning S. The
# bowed edge is its right edge (x = 1); along it runs the mode shapes' first
# coordinate u, from the back (0) to the front (1), and the second, v, runs in
# from that edge: x = 1 - v, y = u. Outlines are drawn at half size in path
# units (+-64) and scaled by 2.
PX, PY, W, H, S = 236, 100, 206, 88, -46
THICK = 4.5               # the plate's thickness on screen (px)
PATH_SCALE = 2
GRAINS_SHOWN = 48


def shape(m, n, s, x, y):
    return math.cos(m * math.pi * x) * math.cos(n * math.pi * y) + s * math.cos(n * math.pi * x) * math.cos(m * math.pi * y)


def frequency(m, n, s):
    w = m * m + n * n
    return w if m == n else w * (1 + s * SPLIT)


def clamped(m, n, s):
    """Whether the mode is still at the plate's centre, where the clamp holds it."""
    return abs(shape(m, n, s, 0.5, 0.5)) < 1e-9


def plate_modes():
    modes = []
    for n in range(1, HIGHEST + 1):
        for m in range(0, n + 1):
            for s in ((1,) if m == n else (1, -1)):
                if clamped(m, n, s):
                    modes.append((frequency(m, n, s), m, n, s))
    modes.sort()
    return modes


def figure_modes(figure):
    """The bowed mode and seven more up the plate: (m, n, s, ratio). High
    figures sit among dense modes, so each one chosen is at least GAP above
    the last: the plate's shimmer stays a chord rather than a beating smear."""
    m, n, s = figure
    assert clamped(m, n, s), f"figure {figure} moves at the clamp"
    f0 = frequency(m, n, s)
    chosen = [(m, n, s, 1.0)]
    for f, a, b, c in plate_modes():
        if len(chosen) < MODES and f / f0 >= chosen[-1][3] * GAP:
            chosen.append((a, b, c, f / f0))
    assert len(chosen) == MODES
    return chosen


def bow_point(figure):
    """Where the bow sits: on the near edge (y = 0), at the figure's loudest
    point away from the corners."""
    m, n, s = figure
    best = max((abs(shape(m, n, s, i / 200, 0)), i / 200) for i in range(30, 171))
    return best[1], 0.0


# ---------------------------------------------------------------------------
# Nodal lines: marching squares on the figure's shape, nudged off exact zeros
# so crossings open into near-touching curves. Every curve then either runs
# from edge to edge or closes on itself (a ring).

def contours(f, N=160):
    eps = 1e-7
    g = [[f(i / N, j / N) + eps for j in range(N + 1)] for i in range(N + 1)]
    segments = []

    def cross(p, q, a, b):
        t = a / (a - b)
        return (round(p[0] + (q[0] - p[0]) * t, 9), round(p[1] + (q[1] - p[1]) * t, 9))
    for i in range(N):
        for j in range(N):
            corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            v = [g[a][b] for a, b in corners]
            pts = [(a / N, b / N) for a, b in corners]
            edges = []
            for k in range(4):
                a, b = v[k], v[(k + 1) % 4]
                if (a > 0) != (b > 0):
                    edges.append((k, cross(pts[k], pts[(k + 1) % 4], a, b)))
            if len(edges) == 2:
                segments.append((edges[0][1], edges[1][1]))
            elif len(edges) == 4:
                centre = f((i + 0.5) / N, (j + 0.5) / N) + eps
                # Pair the crossings so the centre's sign region stays connected.
                if (centre > 0) == (v[0] > 0):
                    segments.append((edges[0][1], edges[3][1]))
                    segments.append((edges[1][1], edges[2][1]))
                else:
                    segments.append((edges[0][1], edges[1][1]))
                    segments.append((edges[2][1], edges[3][1]))
    # Link segments into polylines.
    ends = {}
    for k, (a, b) in enumerate(segments):
        ends.setdefault(a, []).append(k)
        ends.setdefault(b, []).append(k)
    used = [False] * len(segments)
    lines = []
    for start in range(len(segments)):
        if used[start]:
            continue
        used[start] = True
        line = list(segments[start])
        for direction in (0, 1):
            while True:
                tip = line[-1]
                nxt = [k for k in ends[tip] if not used[k]]
                if not nxt:
                    break
                k = nxt[0]
                used[k] = True
                a, b = segments[k]
                line.append(b if a == tip else a)
            line.reverse()
        lines.append(line)
    return lines


def on_border(p, tol=1e-6):
    return p[0] < tol or p[0] > 1 - tol or p[1] < tol or p[1] > 1 - tol


def border_t(p):
    """Position along the border, 0..4, anticlockwise on screen from (0, 0)."""
    x, y = p
    d = [y, 1 - x, 1 - y, x]
    side = d.index(min(d))
    return [x, 1 + y, 2 + (1 - x), 3 + (1 - y)][side] % 4


CORNERS = [(0.0, (0.0, 0.0)), (1.0, (1.0, 0.0)), (2.0, (1.0, 1.0)), (3.0, (0.0, 1.0))]


def border_run(t0, t1):
    """Corners passed going round from t0 to t1, always the same way, so that
    no outline ever runs back along the border over itself."""
    forward = (t1 - t0) % 4
    return [p for c, p in CORNERS + [(c + 4, p) for c, p in CORNERS] if t0 < c < t0 + forward]


def length(line):
    return sum(math.dist(line[i], line[i + 1]) for i in range(len(line) - 1))


def resample(line, count, closed=False):
    pts = line + ([line[0]] if closed else [])
    total = length(pts)
    out = []
    steps = count if closed else count - 1
    for k in range(count):
        target = total * k / steps
        acc = 0.0
        for i in range(len(pts) - 1):
            seg = math.dist(pts[i], pts[i + 1])
            if acc + seg >= target or i == len(pts) - 2:
                t = 0 if seg == 0 else min(1, (target - acc) / seg)
                out.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t))
                break
            acc += seg
    return out


def walk(curves, flip=False):
    """One closed outline through edge-to-edge curves: along a curve, then
    along the border (which the artwork draws in the same ink) to the nearest
    unvisited curve end, and finally back round the border to the start. `flip`
    takes the first curve the other way, which leaves it on its other side.
    Returns runs of (kind, points)."""
    left = list(curves)
    runs = []
    first = min(left, key=lambda c: min(border_t(c[0]), border_t(c[-1])))
    left.remove(first)
    if (border_t(first[-1]) < border_t(first[0])) != flip:
        first = first[::-1]
    start_t = border_t(first[0])
    runs.append(('curve', first))
    here = border_t(first[-1])
    while left:
        def gap(c, end):
            return (border_t(c[end]) - here) % 4
        best = min(((gap(c, e), i, e) for i, c in enumerate(left) for e in (0, -1)))
        c = left.pop(best[1])
        if best[2] == -1:
            c = c[::-1]
        runs.append(('border', border_run(here, border_t(c[0]))))
        runs.append(('curve', c))
        here = border_t(c[-1])
    runs.append(('border', border_run(here, start_t)))
    return runs


def outline_points(runs, count):
    corners = sum(len(p) for k, p in runs if k == 'border')
    curves = [p for k, p in runs if k == 'curve']
    budget = count - corners
    assert budget >= 2 * len(curves), "too many curves for one outline"
    lengths = [length(c) for c in curves]
    total = sum(lengths)
    # At least two points a curve (its ends), the rest by length.
    shares = [2] * len(curves)
    spare = budget - 2 * len(curves)
    exact = [spare * l / total for l in lengths]
    for i, e in enumerate(exact):
        shares[i] += int(e)
    order = sorted(range(len(curves)), key=lambda i: exact[i] - int(exact[i]), reverse=True)
    for i in order[:budget - sum(shares)]:
        shares[i] += 1
    out = []
    ci = 0
    for kind, pts in runs:
        if kind == 'border':
            out += pts
        else:
            out += resample(pts, shares[ci])
            ci += 1
    assert len(out) == count, (len(out), count)
    return out


def crossings(points):
    """How many pairs of a closed outline's edges cross."""
    def side(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    n, found = len(points), 0
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            c, d = points[j], points[(j + 1) % n]
            if side(a, b, c) * side(a, b, d) < 0 and side(c, d, a) * side(c, d, b) < 0:
                found += 1
    return found


def drawn(outline):
    """An outline as display.cpp gets it: path units, Q8."""
    return distinct(tidy([path_units(pt) for pt in outline], POINTS))


def walks_crossing(group, flip=False):
    """The crossings in one walk through these curves."""
    if not group:
        return 0
    try:
        return crossings(drawn(outline_points(walk(group, flip), POINTS)))
    except AssertionError:
        return 10 ** 6   # too many curves for one outline


def best_walk(group):
    """The outline through these curves, whichever way round crosses itself less."""
    return outline_points(walk(group, walks_crossing(group, True) < walks_crossing(group)), POINTS)


def cells(curves):
    """The curves round each cell of the figure, as indices: the curves part the
    plate into cells, and a walk round one cell's curves can't cross itself."""
    ends = sorted((border_t(c[e]), i, e) for i, c in enumerate(curves) for e in (0, -1))
    at = {(i, e): k for k, (_, i, e) in enumerate(ends)}
    seen, found = set(), []
    for start in range(len(ends)):
        k, cell = start, set()
        while k not in seen:
            seen.add(k)
            _, i, e = ends[(k + 1) % len(ends)]
            cell.add(i)
            k = at[(i, -1 - e)]
        if cell:
            found.append(cell)
    return found


def by_cells(curves, walks):
    """Shares the curves among the walks: the cells that between them hold the
    most curves, each curve in the cell with fewest so far; then what is left,
    which the walks must carry across a cell, to the shortest walk."""
    found = cells(curves)
    chosen = max(itertools.combinations(found, min(walks, len(found))), key=lambda c: (len(set().union(*c)), -sum(map(len, c))))
    groups = [[] for _ in chosen]
    for i, c in sorted(enumerate(curves), key=lambda ic: -length(ic[1])):
        fits = [g for g, cell in zip(groups, chosen) if i in cell] or groups
        min(fits, key=lambda g: sum(map(length, g)) + 0.05 * len(g)).append(c)
    return groups


def untangle(groups):
    """Move curves between the walks while that leaves fewer crossings: curves
    that pass close at one of the figure's nodal crossings, resampled, can cross."""
    while True:
        counts = [min(walks_crossing(g), walks_crossing(g, True)) for g in groups]
        if not sum(counts):
            return groups
        best = None
        for a, group in enumerate(groups):
            if not counts[a]:
                continue
            for c in group:
                for b in range(len(groups)):
                    if b == a:
                        continue
                    left = [x for x in group if x is not c]
                    joined = groups[b] + [c]
                    total = (min(walks_crossing(left), walks_crossing(left, True)) +
                             min(walks_crossing(joined), walks_crossing(joined, True)) - counts[a] - counts[b])
                    if best is None or total < best[0]:
                        best = (total, a, b, c)
        if best is None or best[0] >= 0:
            return groups
        _, a, b, c = best
        groups[a] = [x for x in groups[a] if x is not c]
        groups[b] = groups[b] + [c]


def figure_outlines(figure):
    m, n, s = figure
    lines = contours(lambda x, y: shape(m, n, s, x, y))
    curves, rings = [], []
    for line in lines:
        if length(line) < 0.02:
            continue  # a sliver at a near-touch
        snap = []
        for p in (line[0], line[-1]):
            snap.append(on_border(p, 1.5 / 160))
        if snap[0] and snap[1]:
            line = list(line)
            for idx in (0, -1):
                x, y = line[idx]
                d = [x, 1 - x, y, 1 - y]
                side = d.index(min(d))
                line[idx] = [(0.0, y), (1.0, y), (x, 0.0), (x, 1.0)][side]
            curves.append(line)
        elif math.dist(line[0], line[-1]) < 0.02:
            rings.append(line[:-1])
        else:
            raise SystemExit(f"figure {figure}: a curve ends inside the plate")
    walks = PATHS - len(rings)
    assert walks >= 1, f"figure {figure} has too many rings"
    groups = [g for g in untangle(by_cells(curves, walks)) if g]
    outlines = [best_walk(g) for g in groups]
    outlines += [resample(r, POINTS, closed=True) for r in rings]
    return outlines


# What the OP-XY does with a reshaping outline: it merges neighbouring points
# within an eighth of a pixel, draws a turn straight back at a point as a long
# streak, and a closing edge under a quarter of a pixel as a sliver. So each
# outline keeps its neighbours at least TIDY_APART apart, has its out-and-back
# spikes (a walk running along the border and back) cut off, is refilled to
# its points by splitting its longest edges, and starts at its longest edge.
TIDY_APART = 0.3      # path px (0.6 px on screen)
TIDY_STRAIGHT = 0.01  # sin^2 of the sharpest turn back kept (about 6 degrees short of a reversal)


def tidy(points, count):
    pts = list(points)
    while True:
        n = len(pts)
        cut = next((i for i in range(n) if math.dist(pts[i - 1], pts[i]) < TIDY_APART), None)
        if cut is None:
            for i in range(n):
                a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
                d = (b[0] - a[0], b[1] - a[1])
                e = (c[0] - b[0], c[1] - b[1])
                cross, dot = d[0] * e[1] - d[1] * e[0], d[0] * e[0] + d[1] * e[1]
                if dot < 0 and cross * cross < TIDY_STRAIGHT * (d[0] ** 2 + d[1] ** 2) * (e[0] ** 2 + e[1] ** 2):
                    cut = i
                    break
        if cut is None:
            break
        del pts[cut]
    while len(pts) < count:
        i = max(range(len(pts)), key=lambda k: math.dist(pts[k], pts[(k + 1) % len(pts)]))
        a, b = pts[i], pts[(i + 1) % len(pts)]
        pts.insert(i + 1, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    i = max(range(len(pts)), key=lambda k: math.dist(pts[k], pts[(k + 1) % len(pts)]))
    return pts[i + 1:] + pts[:i + 1]  # the longest edge closes the outline


def distinct(points):
    """Nudge repeated points (corners passed twice) apart by 1/256 path unit,
    toward the plate's centre so they stay within the path extent."""
    seen = set()
    out = []
    for x, y in points:
        q = (round(x * 256), round(y * 256))
        while q in seen:
            q = (q[0] - (1 if q[0] > 0 else -1), q[1])
        seen.add(q)
        out.append(q)
    return out


def view(u, v):
    """A point of the shapes' plane (u along the bowed edge, v in from it) on
    screen, relative to the plate's centre (px)."""
    x, y = 1 - v, u
    return ((x - 0.5) * W + (y - 0.5) * S, (y - 0.5) * H)


def on_screen(x, y):
    """A point of the plate's top (x left to right, y back to front) on screen (px)."""
    return (PX + (x - 0.5) * W + (y - 0.5) * S, PY + (y - 0.5) * H)


# The plate tilts about the column's joint below its front edge, so every part
# of it, the figure's outlines included, is drawn from that point.
PIVOT = (PX, PY + H / 2 + THICK)


def path_units(p):
    """The shapes' plane (0..1) to path units (+-64): half the screen offset
    from the pivot."""
    dx, dy = view(p[0], p[1])
    return ((dx + PX - PIVOT[0]) / PATH_SCALE, (dy + PY - PIVOT[1]) / PATH_SCALE)


# ---------------------------------------------------------------------------
# Grain clicks: a two-pole band-pass per Grain step from dust (a soft, bright
# tick at 5.5 kHz) to pebbles (a lower, ringing knock), each scaled to the same energy,
# so every grain's rattle is about as loud.

CLICK_STEPS = 17


def click_filter(g):
    fc = 5500 * (700 / 5500) ** g
    q = 1.5 * (4.0 / 1.5) ** g
    r = math.exp(-math.pi * fc / (q * 44100))
    theta = 2 * math.pi * fc / 44100
    c1, c2 = 2 * r * math.cos(theta), r * r
    y1 = y2 = 0.0
    energy = 0.0
    for i in range(4000):
        x = 1.0 if i == 0 else 0.0
        y = x + c1 * y1 - c2 * y2
        y2, y1 = y1, y
        energy += y * y
    return 0.99 / math.sqrt(energy), c1, c2


def q(v, bits):
    return int(round(v * (1 << bits)))


def build_header():
    lines = ["#pragma once",
             "// Generated by tools/make_plate.py: edit the script, not this file.",
             "#include <opxy/types.hpp>",
             "",
             "namespace sand {",
             f"const opxy::u32 kModes = {MODES}, kFigures = {len(FIGURES)}, kPaths = {PATHS}, kPoints = {POINTS};",
             "",
             f"// cos(m pi i / 256) for m = 0..{HIGHEST}, i = 0..256, Q15.",
             f"const opxy::i16 kCos[{HIGHEST + 1}][257] = {{"]
    for m in range(HIGHEST + 1):
        vals = [max(-32767, min(32767, q(math.cos(m * math.pi * i / 256), 15))) for i in range(257)]
        lines.append("    {" + ", ".join(map(str, vals)) + "},")
    lines.append("};")
    lines.append("")
    lines.append("// A sounding mode: its shape (m, n, s), its frequency over the bowed mode's")
    lines.append("// (Q16; s is 1 for the + pair, 0 for -), log2 of that (Q16) and how strongly the bow at the edge reaches it (Q15).")
    lines.append("struct PlateMode { opxy::u8 m, n, s, spare; opxy::u32 ratio; opxy::i32 log2_ratio; opxy::i32 bow; };")
    lines.append("const PlateMode kFigureModes[kFigures][kModes] = {")
    for fig in FIGURES:
        bx, by = bow_point(fig)
        entries = []
        for m, n, s, ratio in figure_modes(fig):
            peak = 2.0 if m != n else 1.0
            reach = abs(shape(m, n, s, bx, by)) / 2.0
            entries.append(f"{{{m}, {n}, {1 if s > 0 else 0}, 0, {q(ratio, 16)}u, {q(math.log2(ratio), 16)}, {min(32767, q(reach, 15))}}}")
        lines.append("    {" + ", ".join(entries) + "},")
    lines.append("};")
    lines.append("// Where the bow sits on the near edge (0..65535 along it).")
    lines.append("const opxy::u16 kBowAt[kFigures] = {" + ", ".join(str(min(65535, q(bow_point(f)[0], 16))) for f in FIGURES) + "};")
    # The beat of each figure's bowed mode against its near-degenerate twin:
    # a slow, gentle rate of its own, 0.35 to 1.1 Hz on metal (Q8 Hz).
    beats = [0.35 + 0.75 * ((k * 0.6180339887 + 0.37) % 1.0) for k in range(len(FIGURES))]
    lines.append("// How fast each figure's bowed mode beats against its twin on metal (Q8 Hz).")
    lines.append("const opxy::u16 kBeat[kFigures] = {" + ", ".join(str(q(b, 8)) for b in beats) + "};")
    names = []
    for m, n, s in FIGURES:
        names.append(f'"{m}:{n}{"+" if s > 0 else "-"}"')
    lines.append("// The figure's caption: its mode pair.")
    lines.append("const char* const kFigureName[kFigures] = {" + ", ".join(names) + "};")
    lines.append("")
    lines.append("// Each figure's nodal lines as outlines in path units (+-64, Q8), and how")
    lines.append("// many of its kPaths outlines it uses.")
    counts = []
    lines.append("const opxy::i16 kOutline[kFigures][kPaths][kPoints][2] = {")
    sheet_outlines = []
    for fig in FIGURES:
        outlines = figure_outlines(fig)
        sheet_outlines.append(outlines)
        counts.append(len(outlines))
        lines.append("    {")
        for p in range(PATHS):
            if p < len(outlines):
                pts = drawn(outlines[p])
            else:
                pts = [(round(256 * math.cos(2 * math.pi * k / POINTS)), round(256 * math.sin(2 * math.pi * k / POINTS))) for k in range(POINTS)]
            lines.append("        {" + ", ".join(f"{{{x}, {y}}}" for x, y in pts) + "},")
        lines.append("    },")
    lines.append("};")
    lines.append("const opxy::u8 kOutlinesUsed[kFigures] = {" + ", ".join(map(str, counts)) + "};")
    lines.append("")
    # The bow's stick and slip: the note's 2nd to 4th harmonics at -6, -10 and
    # -15 dB of the note, as one waveform read at the bowed mode's phase.
    wave = [math.sin(2 * t) + 0.632 * math.sin(3 * t) + 0.356 * math.sin(4 * t)
            for t in [2 * math.pi * i / 1024 for i in range(1024)]]
    peak = max(abs(v) for v in wave)
    lines.append("// The bow's harmonics: 2nd, 3rd and 4th at 1, 0.632 and 0.356, one cycle in")
    lines.append("// 1024 steps (Q15), divided by their peak, kHarmonicPeak (Q12).")
    lines.append(f"const opxy::i32 kHarmonicPeak = {q(peak, 12)};")
    lines.append("const opxy::i16 kHarmonicWave[1024] = {" + ", ".join(str(q(v / peak * 0.9999, 15)) for v in wave) + "};")
    lines.append("// Grain clicks from dust to pebbles: a band-pass's input gain, c1 / 2 and c2")
    lines.append("// (y = g x + c1 y1 - c2 y2), Q31, each with an impulse response of unit energy.")
    lines.append(f"const opxy::u32 kClickSteps = {CLICK_STEPS};")
    lines.append(f"const opxy::i32 kClick[{CLICK_STEPS}][3] = {{")
    for i in range(CLICK_STEPS):
        g, c1, c2 = click_filter(i / (CLICK_STEPS - 1))
        lines.append(f"    {{{min(2147483647, q(g, 31))}, {q(c1 / 2, 31)}, {q(c2, 31)}}},")
    lines.append("};")
    lines.append("}")
    return "\n".join(lines) + "\n", sheet_outlines


# ---------------------------------------------------------------------------
# Artwork: the apparatus, as an engraving of Chladni's (1787) would show it,
# in greys and fine line only. A square plate on its stand, seen from the
# front and a little to the left: its top with the figure's nodal lines and
# the sand, its front and left edges in thickness; the knurled clamp nut at
# its centre; the turned column and the round base on three levelling screws;
# a violin bow across the plate's right edge and, as the bow gives way to a
# strike, a felt mallet over it. Lines are thin closed ribbons; the only fills
# are the clamp's washer and nut, which hide the lines under them.
#
# Every part of the plate is a node, drawn from the column's joint, so the
# whole plate can tip on its column (Tilt). Each control changes it plainly:
# the material its thickness and marks (plain metal, glints on glass, grain
# in wood), Settle its finish (a polished sheen or a
# matte, rosined tooth), Shake its edge (doubled, as an engraver shows a thing
# that trembles), Grain the grains' size, Bow the bow's contact and the
# mallet's approach.

HEADER_FIRST = PATHS                  # 4..11
PLATE_TOP = HEADER_FIRST + 8          # 12: the plate's top edge
THICKNESS = PLATE_TOP + 1             # 13..15: its edges in thickness, by material
GLASS_MARKS = THICKNESS + 3           # 16..17: glints
WOOD_MARKS = GLASS_MARKS + 2          # 18..21: grain
CLAMP = WOOD_MARKS + 4                # 22..24: washer, nut, nut's top
SHEEN = CLAMP + 3                     # 25..26: a polished plate's reflections
MATTE = SHEEN + 2                     # 27..28: a sticky plate's rosined tooth
TREMOR = MATTE + 2                    # 29..30: a shaken plate's double image
BOW_FIRST = TREMOR + 2                # 31..34: stick, head, tip face, hair
MALLET = BOW_FIRST + 4                # 35..36: shaft, head
GRAIN_FIRST = MALLET + 2              # 37..84
DRIFT_FIRST = GRAIN_FIRST + GRAINS_SHOWN  # 85..132: the sand each grain stands for, settled on a line
NODES = DRIFT_FIRST + GRAINS_SHOWN
FONT = 'font-family="UniversNextPro-Light"'
GREYS = ['#000000', '#0d0d0d', '#141414', '#1c1c1c', '#262626', '#333333', '#404040', '#4d4d4d',
         '#5c5c5c', '#6e6e6e', '#808080', '#999999', '#b3b3b3', '#cccccc', '#e6e6e6', '#ffffff']
LINE = GREYS[8]          # the nodal lines
# Three line weights: the plate's top, brightest and boldest; the rest of the
# apparatus a step down; marks on the plate another step down.
MAIN, MAIN_WIDTH = GREYS[13], 1.5
SECOND, SECOND_WIDTH = GREYS[10], 1.0
DETAIL_WIDTH = 1.0
DEPTH = H / W            # how much a circle on the plate's plane is flattened
COLUMN_X, COLUMN_TOP = PIVOT
BASE_Y, BASE_R = 191, 34
BOW_ANGLE = math.radians(27)          # the bow below horizontal, its head over the plate
MATERIAL_THICK = [4.5, 3.0, 8.0]      # metal, glass, wood (px)
MALLET_AT = (0.2, 0.8)                # where the mallet strikes (plate x, y)
MALLET_ANGLE = math.radians(145)      # its shaft, down and to the left, off the screen


def num(v, digits=4):
    s = f"{v:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def outline(points):
    pts = [(num(x), num(y)) for x, y in points]
    assert len(set(pts)) == len(pts), "repeated point"
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def ribbon(points, w0, w1=None):
    """A closed ribbon along a polyline: how a line is drawn."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        w = (w0 if w1 is None else w0 + (w1 - w0) * i / max(1, n - 1)) / 2
        left.append((points[i][0] - dy / length * w, points[i][1] + dx / length * w))
        right.append((points[i][0] + dy / length * w, points[i][1] - dx / length * w))
    return left + right[::-1]


def ellipse(cx, cy, rx, ry, n=24, a0=0.0, a1=2 * math.pi):
    full = abs(a1 - a0 - 2 * math.pi) < 1e-9
    steps = n if full else n - 1
    return [(cx + rx * math.cos(a0 + (a1 - a0) * k / (n if full else steps)),
             cy + ry * math.sin(a0 + (a1 - a0) * k / (n if full else steps))) for k in range(n if full else n)]


def path(d, fill, stroke, width, node=None, at=None, extra=""):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    transform = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    return f'<path{ident}{transform} d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"{extra}/>'


def line(points, width, grey, node=None, at=None):
    return path(outline(ribbon(points, width)), grey, grey, 0.1, node, at)


def wobble(x, seed):
    """A hand-cut wave: a few incommensurate sines."""
    return (0.010 * math.sin(x * 13.7 + seed) + 0.006 * math.sin(x * 31.3 + 2.1 * seed) +
            0.003 * math.sin(x * 71.0 + 0.7 * seed))


def rotate(points, angle):
    c, s = math.cos(angle), math.sin(angle)
    return [(x * c - y * s, x * s + y * c) for x, y in points]


def bow_parts():
    """The bow, from its hair's contact with the plate's edge at (0, 0), laid
    along +x then turned to BOW_ANGLE: a Tourte head over the plate, the stick
    running off the screen with its camber toward the hair, and the hair below
    it. Returns (points, fill, idle grey, lit grey) per node: stick, head, tip
    face, hair."""
    k = 1.5
    def scaled(points):
        return [(x * k, y * k) for x, y in points]
    stick = [(x, k * (-5.4 + 1.5 * math.sin(math.pi * min(1.0, x / 320)) - 1.2 * (x / 320) ** 2))
             for x in range(20, 330, 10)]
    head = scaled([(-8.2, 0.4), (-8.7, -3.6), (-8.5, -7.6), (-7.8, -10.6), (-6.4, -12.2), (-4.6, -12.0),
                   (-2.4, -10.9), (0.6, -9.2), (4.0, -7.5), (8.0, -6.4), (13.4, -6.2), (13.4, -4.6),
                   (9.0, -4.6), (5.2, -3.8), (2.6, -2.2), (1.3, 0.4)])
    face = ribbon(scaled([(-7.3, -0.3), (-7.7, -3.8), (-7.5, -7.6), (-6.9, -10.2), (-5.9, -11.2)]), 1.1)
    hair = ribbon([(x, 0.9 * x / 330) for x in range(2, 333, 22)], 1.4)
    parts = [(ribbon(stick, 1.5, 2.4), "fill", 9, 11), (head, "none", 9, 11), (face, "fill", 8, 10), (hair, "fill", 10, 13)]
    return [(rotate(pts, BOW_ANGLE), fill, idle, lit) for pts, fill, idle, lit in parts]


BOW_GREYS = [(idle, lit) for _, _, idle, lit in bow_parts()]


def rel(pt):
    """A screen point relative to the pivot."""
    return (pt[0] - PIVOT[0], pt[1] - PIVOT[1])


def top(x, y):
    """A point of the plate's top (x, y in 0..1) relative to the pivot."""
    return rel(on_screen(x, y))


def node_path(points, node, fill, stroke, width, extra=""):
    return path(outline(points), fill, stroke, width, node, PIVOT, extra)


def node_line(points, width, grey, node):
    return path(outline(ribbon(points, width)), grey, grey, 0.1, node, PIVOT)


def thickness(t):
    """The plate's front and left edges, t px thick, as one closed band."""
    bl, fl, fr = top(0, 0), top(0, 1), top(1, 1)
    down = lambda p: (p[0], p[1] + t)
    return [bl, fl, fr, down(fr), down(fl), down(bl)]


def zigzag(y, amplitude, period, x0=0.04, x1=0.96):
    """A rough, rosined tooth across the plate at depth y: a fine zigzag."""
    pts, k, x = [], 0, x0
    while x <= x1:
        pts.append(top(x, y + (amplitude if k % 2 else -amplitude)))
        x += period
        k += 1
    return pts


def build_panel():
    out = [f'<g id="{ID}_panel">',
           "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
           '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000" stroke="#000000" stroke-width="0.5"/>']
    bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i, colour in enumerate(bar):
        out.append(f'<path id="{ID}_node_{HEADER_FIRST + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                   f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
    texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
    texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
    texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
    for i, (x, y, ink, size) in enumerate(texts):
        out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
                   f'font-size="{size}px">-</text>')

    # The stand, static, under the plate: the column from the joint down,
    # turned, then the base on its levelling screws.
    cx = COLUMN_X
    profile = [(COLUMN_TOP + 1, 6.0), (COLUMN_TOP + 3.5, 6.0), (COLUMN_TOP + 5.0, 4.6), (COLUMN_TOP + 7.0, 3.6),
               (COLUMN_TOP + 26, 3.2), (COLUMN_TOP + 27.5, 5.0), (COLUMN_TOP + 30.5, 5.0), (COLUMN_TOP + 32, 3.4),
               (BASE_Y - 6, 3.6), (BASE_Y - 2.5, 6.0), (BASE_Y, 7.5)]
    for side in (-1, 1):
        out.append(line([(cx + side * r, y) for y, r in profile], SECOND_WIDTH, SECOND))
    for y, r in [(COLUMN_TOP + 3.5, 6.0), (COLUMN_TOP + 27.5, 5.0)]:
        out.append(line(ellipse(cx, y, r, r * DEPTH, 9, 0, math.pi), SECOND_WIDTH, SECOND))
    ry = BASE_R * DEPTH
    back = [pt for pt in ellipse(cx, BASE_Y, BASE_R, ry, 48, math.pi, 2 * math.pi) if abs(pt[0] - cx) > 8.2]
    out.append(line([pt for pt in back if pt[0] < cx], SECOND_WIDTH, SECOND))
    out.append(line([pt for pt in back if pt[0] > cx], SECOND_WIDTH, SECOND))
    out.append(line(ellipse(cx, BASE_Y, BASE_R, ry, 32, 0, math.pi), SECOND_WIDTH, SECOND))

    # The plate, every part a node from the pivot. Behind it, the shaken
    # plate's doubled edges.
    corners = [top(0, 0), top(1, 0), top(1, 1), top(0, 1)]
    silhouette = corners[:3] + [(x, y + THICK) for x, y in (top(1, 1), top(0, 1), top(0, 0))]
    for k in range(2):
        out.append(node_path(silhouette, TREMOR + k, "none", GREYS[8], DETAIL_WIDTH))
    # The figure's nodal lines, under the plate's edge, which hides where they
    # run along the border between one line and the next: authored as distinct
    # rings; the first frame reshapes and places them.
    for p in range(PATHS):
        pts = [((8 + p) * math.cos(2 * math.pi * k / POINTS), (8 + p) * math.sin(2 * math.pi * k / POINTS)) for k in range(POINTS)]  # 0.8 px apart or more
        out.append(path(outline(pts), "none", LINE, 0.5, p, PIVOT, ' stroke-linejoin="round"'))

    for k, t in enumerate(MATERIAL_THICK):
        out.append(node_path(thickness(t), THICKNESS + k, "none", SECOND, SECOND_WIDTH))
    out.append(node_path(corners, PLATE_TOP, "none", MAIN, MAIN_WIDTH))
    for k, (a, b) in enumerate([((0.05, 0.32), (0.32, 0.05)), ((0.62, 0.95), (0.95, 0.62))]):
        out.append(node_line([top(*a), top(*b)], 1.2, GREYS[12], GLASS_MARKS + k))
    for k, (y, seed) in enumerate([(0.14, 0.4), (0.38, 2.3), (0.62, 4.1), (0.86, 1.2)]):
        out.append(node_line([top(x / 33, y + 1.6 * wobble(x / 33, seed)) for x in range(1, 33)], DETAIL_WIDTH, GREYS[8], WOOD_MARKS + k))
    for k, (a, b) in enumerate([((0.08, 0.62), (0.62, 0.08)), ((0.22, 0.9), (0.9, 0.22))]):
        out.append(node_line([top(*a), top(*b)], 1.4, GREYS[10], SHEEN + k))
    for k, y in enumerate([0.3, 0.7]):
        out.append(node_line(zigzag(y, 0.03, 0.035), DETAIL_WIDTH, GREYS[6], MATTE + k))

    # The clamp at the plate's centre: its washer and the knurled nut.
    c = rel((PX, PY))
    out.append(node_path(ellipse(c[0], c[1], 10, 10 * DEPTH, 32), CLAMP, "#000000", SECOND, SECOND_WIDTH))
    nut = (ellipse(c[0], c[1] - 6, 6.5, 6.5 * DEPTH, 13, math.pi, 2 * math.pi) + ellipse(c[0], c[1], 6.5, 6.5 * DEPTH, 13, 0, math.pi))
    out.append(node_path(nut, CLAMP + 1, "#000000", SECOND, SECOND_WIDTH))
    knurl = []
    for k in range(13):  # the nut's top and its knurled side, as one outline
        a = math.pi * k / 12
        knurl.append((c[0] + 6.5 * math.cos(a), c[1] - 6 + 6.5 * DEPTH * math.sin(a)))
    knurl += [(c[0] + 6.5 * math.cos(a), c[1] - 6 - 6.5 * DEPTH * math.sin(a)) for a in [math.pi * k / 12 for k in range(11, 0, -1)]]
    out.append(node_path(knurl, CLAMP + 2, "none", SECOND, SECOND_WIDTH))

    # The settled sand: a drift for each grain, a low mound along x that the
    # screen turns along the nodal line it lies on and lengthens with Sand;
    # under the grains.
    drift = [(5.6 * math.cos(a), 1.7 * math.sin(a) * (1 + 0.25 * math.cos(a))) for a in [2 * math.pi * k / 14 for k in range(14)]]
    for g in range(GRAINS_SHOWN):
        out.append(path(outline(drift), GREYS[8], GREYS[9], 0.3, DRIFT_FIRST + g, PIVOT))

    # The sand: fine grains, authored at the pivot.
    for g in range(GRAINS_SHOWN):
        out.append(f'<circle id="{ID}_node_{GRAIN_FIRST + g}" transform="matrix(1 0 0 1 {num(PIVOT[0])} {num(PIVOT[1])})" '
                   f'cx="0" cy="0" r="1.3" fill="{GREYS[11]}" stroke="{GREYS[11]}" stroke-width="0.3"/>')

    # The mallet: a felt head on a cane shaft, authored with its head at the
    # spot it strikes; the screen brings it in as Bow gives way to a strike.
    at = on_screen(*MALLET_AT)
    shaft = rotate([(x, 0) for x in range(5, 90, 6)], MALLET_ANGLE)
    out.append(path(outline(ribbon(shaft, 1.3, 1.9)), GREYS[9], GREYS[9], 0.1, MALLET, at))
    out.append(path(outline(ellipse(0, -2.6, 5.2, 3.4, 20)), "none", GREYS[12], 0.6, MALLET + 1, at))

    # The bow, authored at figure 0's bow point.
    at = on_screen(1, bow_point(FIGURES[0])[0])
    for k, (pts, fill, idle, lit) in enumerate(bow_parts()):
        grey = GREYS[idle]
        out.append(path(outline(pts), grey if fill == "fill" else "none", grey, 0.12 if fill == "fill" else 0.7, BOW_FIRST + k, at))

    for i, grey in enumerate(GREYS):
        out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="0.5"/>')
    out.append("</g>")
    return "\n".join(out) + "\n"


def build_view():
    """view.h: the view's numbers and node numbers for display.cpp."""
    q10 = lambda v: int(round(v * 1024))
    return "\n".join([
        "#pragma once",
        "// Generated by tools/make_plate.py: the view the artwork is drawn in. Edit the script.",
        "#include <opxy/types.hpp>",
        "",
        "namespace sand {",
        "// The plate's centre on screen and its parallelogram (px): W wide, H deep, leaning S.",
        f"const opxy::i32 view_x = {PX}, view_y = {PY}, view_w = {W}, view_h = {H}, view_s = {S};",
        "// The plate tips about this point, the column's joint (Q10 px); outlines are drawn at this scale (Q10).",
        f"const opxy::i32 pivot_x = {q10(PIVOT[0])}, pivot_y = {q10(PIVOT[1])}, path_scale = {q10(PATH_SCALE)};",
        "// The bow's axis (Q10, down and to the right): its stroke draws it back along it, over the plate.",
        f"const opxy::i32 bow_cos = {q10(math.cos(BOW_ANGLE))}, bow_sin = {q10(math.sin(BOW_ANGLE))};",
        "// The mallet's striking spot on screen and its shaft's direction (Q10).",
        f"const opxy::i32 mallet_x = {q10(on_screen(*MALLET_AT)[0])}, mallet_y = {q10(on_screen(*MALLET_AT)[1])};",
        f"const opxy::i32 mallet_cos = {q10(math.cos(MALLET_ANGLE))}, mallet_sin = {q10(math.sin(MALLET_ANGLE))};",
        "// Node numbers.",
        f"const opxy::u32 header_first = {HEADER_FIRST}, plate_top = {PLATE_TOP}, thickness_first = {THICKNESS},",
        f"                glass_first = {GLASS_MARKS}, wood_first = {WOOD_MARKS}, clamp_first = {CLAMP}, sheen_first = {SHEEN},",
        f"                matte_first = {MATTE}, tremor_first = {TREMOR}, bow_first = {BOW_FIRST}, mallet_first = {MALLET},",
        f"                grain_first = {GRAIN_FIRST}, drift_first = {DRIFT_FIRST}, node_total = {NODES};",
        "}", ""])


def build_sheet(outlines):
    cols = 4
    cell = 200
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{cols * cell}" height="{((len(FIGURES) + cols - 1) // cols) * cell}" style="background:#000">']
    colours = ["#e6e6e6", "#b3b3b3", "#808080", "#5c5c5c"]
    for i, fig in enumerate(FIGURES):
        ox, oy = (i % cols) * cell + 20, (i // cols) * cell + 20
        out.append(f'<rect x="{ox}" y="{oy}" width="160" height="160" fill="#000000" stroke="{LINE}"/>')
        for p, pts in enumerate(outlines[i]):
            d = "M" + " L".join(f"{ox + (1 - y) * 160:.1f},{oy + x * 160:.1f}" for x, y in pts) + " Z"
            out.append(f'<path d="{d}" fill="none" stroke="{colours[p]}" stroke-width="1" opacity="0.8"/>')
        bx, _ = bow_point(fig)
        out.append(f'<rect x="{ox + 162}" y="{oy + bx * 160 - 9}" width="3" height="18" fill="#ddd"/>')
        out.append(f'<text x="{ox}" y="{oy + 180}" fill="#ccc" font-size="11">{i}: {fig} paths {len(outlines[i])}</text>')
    out.append("</svg>")
    return "\n".join(out)


header, outlines = build_header()
(ROOT / "plate.h").write_text(header)
(ROOT / "view.h").write_text(build_view())
# Keep definition.json's Figure labels and node count in step with the figures and artwork.
import json
definition_path = ROOT / "definition.json"
definition = json.loads(definition_path.read_text())
definition["parameters"][0]["mapping"]["labels"] = [f'{m}:{n}{"+" if s > 0 else "-"}' for m, n, s in FIGURES]
definition["resources"]["display"]["nodes"] = NODES
definition_path.write_text(json.dumps(definition, indent=2) + "\n")
panel = build_panel()
# The device's rules for every artwork path: one subpath, at most 64 points,
# neighbours at least 1/8 px apart, a closing edge of at least 1/4 px.
for d in re.findall(r' d="([^"]+)"', panel):
    assert d.count("M") == 1, d[:60]
    points = [(float(x), float(y)) for x, y in re.findall(r"(-?[\d.]+),(-?[\d.]+)", d)]
    assert 3 <= len(points) <= 64, (len(points), d[:60])
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        assert abs(a[0] - b[0]) + abs(a[1] - b[1]) >= (0.25 if i == len(points) - 1 else 0.125), (a, b)
assert len(panel) <= 64 * 1024, len(panel)
(ROOT / "panel.svg.fragment").write_text(panel)
if SHEET:
    SHEET.write_text(build_sheet(outlines))
print(f"plate.h, view.h, panel.svg.fragment: {len(FIGURES)} figures, {NODES} nodes")
