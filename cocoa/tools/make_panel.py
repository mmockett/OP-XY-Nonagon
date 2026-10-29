#!/usr/bin/env python3
"""Writes panel.svg.fragment, Cocoa's screen artwork, and plate.h, the shapes
display.cpp reshapes its outlines to. Run it after changing a shape here and
commit all three.

One object fills the screen: a mug of hot chocolate in section, to scale at
1.5 px a millimetre: 80 mm across inside and 90 mm deep, its wall china,
glass or enamelled tin, the handle at the left. Four greys and three
weights: the mug boldest and brightest, the spoon and the drink's surface a
step down, the drink's detail finer. A spoon stands in the drink, its bowl
on the floor, leaning on the cup's lip or wall, and rocks to tap the floor.
The drink shows its own pitch: while bubbles remain its lines are frothy,
and they clear from the floor up as the bubbles rise out; the drink is in
tune when every line is clear. The lines' density is the drink's darkness
(powder); powder heaps on the floor where it has not been stirred in.

The three reshaping outlines are the vessel (by cup), the drink's surface
(meniscus, the stir's dip and the tap's ripples), and the spoon, bowl and
handle in one (by spoon: a wooden spoon to a silver teaspoon). They are
always drawn upright: the device places a turned reshaping outline otherwise
than the browser, so display.cpp turns the spoon's points itself. Lines are
closed ribbons or rings; nothing large is filled. Every outline written here
is checked against what the device draws badly (device_rules)."""
import math
import pathlib
import random
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "cocoa"
FONT = 'font-family="UniversNextPro-Light"'
STROKE = 0.9              # the finest line: about a pixel, so it holds at 1:1 on the 480 x 220 screen
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# The mug (display.h: scene). Millimetres to pixels, the axis and the inner floor.
MM = 1.5
CX, BOTTOM = 250.0, 200.0
INNER = 40 * MM          # 60 px
DEPTH = 90 * MM          # 135 px: the rim at y 65
RIM = BOTTOM - DEPTH
VESSEL_AT = (CX, 140.0)  # the vessel's node, drawn at twice its points
WALL = {"china": 6 * MM, "glass": 4.5 * MM, "tin": 0.8 * MM}   # the outer wall's thickness low down
WATER_LINES = 20
WATER_STEP = 6.0
HANDLE_MID = 115.0       # the spoon's handle node, this far along it from the tip

# Nodes (display.h).
VESSEL, SURFACE, SPOON_NODE = 0, 1, 2   # the reshaping outlines, never turned (display.cpp turns their points)
HEADER = 3
BUBBLE, BUBBLES = 11, 40
FOAM, FOAMS = 51, 20
POP, POPS = 71, 4
HANDLE_CHINA, HANDLE_TIN = 75, 76
WATER = 77                # clear lines, then the same lines frothy
FROTH = 97
HEAP = 117                # undissolved powder on the floor
CLUMP, CLUMPS = 118, 3    # and hanging in the drink
SHINE = 121
GRAIN, GRAINS = 122, 2    # the wooden spoon's grain: along the handle, a ring in the bowl
SHAKE, SHAKES = 124, 3    # strokes beside the bowl while it taps fast
RING, RINGS = 127, 3      # ripples on the floor where the bowl taps
SWIRL, SWIRLS = 130, 4    # the stirred drink turning
NODES = 134
# Three weights: the mug brightest and boldest, the spoon and the drink's
# surface a step down, the drink's detail finer. A palette entry's stroke is
# its weight (px).
WEIGHT = {15: 1.6, 13: 1.1}
POINTS = 64
HIDDEN = (640, 320)


def num(v, places=4):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points, places=2):
    """A closed path, started so that its closing edge is its longest: the
    device draws a short closing edge as a long sliver."""
    n = len(points)
    j = max(range(n), key=lambda i: math.dist(points[i], points[(i + 1) % n]))
    points = points[j + 1:] + points[:j + 1]
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


# ---- What the OP-XY draws badly, beyond the SDK's rules (learned on the
# device, MNMD's docs/SCREEN.md): neighbouring points within an eighth of a
# pixel merge; a closing edge under a quarter of a pixel draws as a long
# sliver; an outline with no area, or turning straight back on itself, draws
# streaks. Every outline written here is checked.
def device_rules(points, what, scale=1.0):
    pts = [(x * scale, y * scale) for x, y in points]
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        assert math.dist(a, b) >= 0.125 + 1e-9 or (i == n - 1 and math.dist(a, b) >= 0.25), (what, "points merge", i, a, b)
    assert math.dist(pts[-1], pts[0]) >= 0.25, (what, "closing edge under a quarter pixel")
    area = abs(sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))) / 2
    assert area > 0.2, (what, "no area", area)
    for i in range(n):
        a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
        u, v = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
        lu, lv = math.hypot(*u), math.hypot(*v)
        assert (u[0] * v[0] + u[1] * v[1]) / (lu * lv) > -0.995, (what, "turns straight back at point", i)


def path(d, stroke, width=STROKE, node=None, at=None, fill="none"):
    pts = [tuple(map(float, q.split(","))) for q in re.findall(r"-?[\d.]+,-?[\d.]+", d)]
    device_rules(pts, f"node {node}")
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if (fill == "none" or node is None) else ""
    return f'<path{ident}{move} d="{d}" fill="{fill}" stroke="{stroke}"{w}/>'


def circle(cx, cy, r, stroke, width=STROKE, node=None, at=None, fill="none"):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if (fill == "none" or node is None) else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}"{w}/>'


def ribbon(points, h):
    """A thin line: out along the points, back offset along each point's normal
    by h, at least 0.4 px so that it has area on the device."""
    h = h if abs(h) >= 0.4 else math.copysign(0.4, h)
    back = []
    n = len(points)
    for i in range(n - 1, -1, -1):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        length = math.hypot(tx, ty) or 1
        back.append((points[i][0] - ty / length * h, points[i][1] + tx / length * h))
    return points + back


def line(x0, y0, x1, y1, n=2):
    return [(x0 + (x1 - x0) * i / (n - 1), y0 + (y1 - y0) * i / (n - 1)) for i in range(n)]


def arc(cx, cy, r, a0, a1, n, ry=None):
    ry = r if ry is None else ry
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / (n - 1)), cy + ry * math.sin(a0 + (a1 - a0) * i / (n - 1)))
            for i in range(n)]


def fill_to(points, n):
    """Adds points along the longest segments of a closed polygon until it has n."""
    pts = list(points)
    while len(pts) < n:
        k = max(range(len(pts)), key=lambda i: math.dist(pts[i], pts[(i + 1) % len(pts)]))
        a, b = pts[k], pts[(k + 1) % len(pts)]
        pts.insert(k + 1, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    assert len(pts) == n, len(pts)
    return pts


def distinct(contour):
    out = []
    for p in contour:
        if not out or math.dist(out[-1], p) > 0.3:
            out.append(p)
    if math.dist(out[0], out[-1]) < 0.3:
        out.pop()
    return out


# ---- The vessels, in section: one closed contour each, the wall cut through.
def vessel(kind):
    L, R, top, floor = CX - INNER, CX + INNER, RIM, BOTTOM
    if kind == "china":     # stoneware: 6 mm wall, 8 mm base, a rounded rim and foot
        w, base, r = WALL["china"], 8 * MM, 3 * MM
        inner = [(L, floor - r)] + arc(L + r, floor - r, r, math.pi, math.pi / 2, 4)[1:] \
            + arc(R - r, floor - r, r, math.pi / 2, 0, 4)[:-1] + [(R, floor - r)]
        outer = [(R + w, floor + base - 2 * r)] + arc(R + w - 2 * r, floor + base - 2 * r, 2 * r, 0, math.pi / 2, 5)[1:] \
            + [(R + w - 13, floor + base), (R + w - 16, floor + base - 1.6), (L - w + 16, floor + base - 1.6), (L - w + 13, floor + base)] \
            + arc(L - w + 2 * r, floor + base - 2 * r, 2 * r, math.pi / 2, math.pi, 5)[:-1]
        contour = arc(L - w / 2, top, w / 2, math.pi, 2 * math.pi, 6) + inner + \
            arc(R + w / 2, top, w / 2, math.pi, 2 * math.pi, 6) + outer
    elif kind == "glass":   # a tumbler: 3 mm at the lip to 4.5 mm low down, a 10 mm base with a punt
        w0, w1, base = 3 * MM, WALL["glass"], 10 * MM
        inner = [(L, floor - 2)] + arc(L + 2, floor - 2, 2, math.pi, math.pi / 2, 3)[1:] \
            + arc(R - 2, floor - 2, 2, math.pi / 2, 0, 3)[:-1] + [(R, floor - 2)]
        outer = [(R + w1, floor + base - 3)] + arc(R + w1 - 3, floor + base - 3, 3, 0, math.pi / 2, 4)[1:] \
            + [(CX + 32, floor + base), (CX + 19, floor + base - 3.4), (CX - 19, floor + base - 3.4), (CX - 32, floor + base)] \
            + arc(L - w1 + 3, floor + base - 3, 3, math.pi / 2, math.pi, 4)[:-1]
        contour = arc(L - w0 / 2, top, w0 / 2, math.pi, 2 * math.pi, 5) + inner + \
            arc(R + w0 / 2, top, w0 / 2, math.pi, 2 * math.pi, 5) + outer
    else:                   # an enamelled tin mug: a 0.8 mm wall, a rolled bead at the lip, a pressed foot
        w, bead = WALL["tin"], 1.8 * MM
        inner = [(L, floor - 3)] + arc(L + 3, floor - 3, 3, math.pi, math.pi / 2, 4)[1:] \
            + arc(R - 3, floor - 3, 3, math.pi / 2, 0, 4)[:-1] + [(R, floor - 3)]
        outer = [(R + w, floor - 3), (R + w - 1, floor + 1), (R - 8, floor + w + 3.2), (L + 8, floor + w + 3.2),
                 (L - w + 1, floor + 1), (L - w, floor - 3)]
        contour = [(L, top + 0.8)] + inner + [(R, top + 0.8)] \
            + arc(R + bead, top, bead, math.pi, math.pi * 2.6, 8) + [(R + w, top + bead + 1.6)] + outer \
            + [(L - w, top + bead + 1.6)] + arc(L - bead, top, bead, math.pi * 0.4, math.pi * 2, 8)
    return fill_to(distinct(contour), POINTS)


VESSELS = {k: vessel(k) for k in ("china", "glass", "tin")}
# The centre of each cup's rounded right rim (vessel() draws it so).
LIPS = {"china": (CX + INNER + WALL["china"] / 2, RIM), "glass": (CX + INNER + 1.5 * MM, RIM),
        "tin": (CX + INNER + 1.8 * MM, RIM)}


# ---- The spoon, face on, along +x from the bowl's tip (px), in two parts:
# the bowl (about the tip) and the handle (about HANDLE_MID along). A short,
# fat wooden spoon (soft) that leans inside the mug against its wall, and a
# long, slim silver teaspoon (hard) that rests on the rim, point for point,
# to morph. Each rests with its tip on the floor and rocks about where it
# leans.
SPOON = {"wood": dict(length=74, bowl=42, width=38, neck=13, end=15, drop=4.0),
         "silver": dict(length=108, bowl=34, width=21, neck=3, end=8.5, drop=0.6)}
TIP_X = CX - 5


def resample(points, n, ends=False):
    """n points along a polyline, evenly by length; without its first point
    unless `ends` (the polyline's own points kept as the curve)."""
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + math.dist(a, b))
    out = []
    for k in range(n):
        d = cum[-1] * (k / (n - 1) if ends else (k + 1) / n)
        i = max(0, min(len(cum) - 2, next((q for q in range(len(cum) - 1) if cum[q + 1] >= d), len(cum) - 2)))
        f = (d - cum[i]) / ((cum[i + 1] - cum[i]) or 1)
        out.append((points[i][0] + (points[i + 1][0] - points[i][0]) * f, points[i][1] + (points[i + 1][1] - points[i][1]) * f))
    return out


def spoon(kind):
    """The spoon's whole silhouette, one closed outline of 64 points along +x
    from the bowl's tip (px): the bowl, an egg widest toward the tip, running
    into the handle, round at its end. Point for point alike for wood and
    silver, to morph: the tip, 16 points up the bowl, 11 along the handle, 9
    round its end, 11 back and 16 down the bowl."""
    p = SPOON[kind]
    bl, w, length, cap = p["bowl"] * MM, p["width"] * MM, p["length"] * MM, p["end"] / 2 * MM
    neck = p["neck"] / 2 * MM
    egg = []
    for k in range(200):
        t = math.pi * k / 199
        x = bl / 2 * (1 - math.cos(t))
        egg.append((x, -max(w / 2 * math.sin(t) * (1 - 0.22 * x / bl), neck * (x / bl) ** 8)))
    stem = []
    for k in range(60):
        t = k / 59
        x = bl + (length - cap - bl) * t
        widen = neck + (cap - neck) * (t ** 2.2 if kind == "silver" else t ** 0.4)
        if kind == "silver" and 0.55 < t < 0.85:   # the fiddle's waist
            widen *= 1 - 0.18 * math.sin(math.pi * (t - 0.55) / 0.3)
        stem.append((x, -widen))
    up = resample(egg, 16) + resample([egg[-1]] + stem, 11)
    round_end = [(length - cap + cap * math.cos(-math.pi / 2 + math.pi * k / 10), cap * math.sin(-math.pi / 2 + math.pi * k / 10))
                 for k in range(1, 10)]
    pts = [(0.0, 0.0)] + up + round_end + [(x, -y) for x, y in up[::-1]]
    assert len(pts) == POINTS, (kind, len(pts))
    return pts


# ---- Where the spoon rests, solved against each cup's own outline. The tip
# stands on the floor; the spoon tips over toward the right wall until it
# touches the cup (the rim's rounded lip for a long spoon, the inner wall for
# a short one), never crossing a line of it. Each tap rocks it about a point
# that keeps it clear: the lip's centre of curvature, or the handle end's.
# Solved for 33 shapes from wood to silver, as display.cpp morphs them, and
# checked at every lift taps can give (0 to 24 degrees).
MORPHS = 33
LIFT_MOST = 24.0     # degrees: taps' highest rest (display.cpp)
CLEAR = 1.4          # px between the spoon's line and the cup's, centre to centre (each about 0.9 px wide)
HEADER_BOTTOM = 46.0 # px: nothing of the spoon above this, a clear margin under the header


def morphed(m):
    """The spoon's outline in its own frame (tip at 0, +x along it), m from wood 0 to silver 1, densified."""
    pts = [(ax + (bx - ax) * m, ay + (by - ay) * m) for (ax, ay), (bx, by) in zip(spoon("wood"), spoon("silver"))]
    dense = []
    for i in range(len(pts)):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
        n = max(1, math.ceil(math.dist((x0, y0), (x1, y1)) / 1.5))
        dense += [(x0 + (x1 - x0) * j / n, y0 + (y1 - y0) * j / n) for j in range(n)]
    return dense


def segments(points):
    out = []
    for i in range(len(points)):
        (x0, y0), (x1, y1) = points[i], points[(i + 1) % len(points)]
        out.append((x0, y0, x1, y1, min(x0, x1) - CLEAR, max(x0, x1) + CLEAR, min(y0, y1) - CLEAR, max(y0, y1) + CLEAR))
    return out


def clear(points, segs, margin=CLEAR):
    """True when every point is at least margin from every segment."""
    m2 = margin * margin
    for px, py in points:
        for x0, y0, x1, y1, a, b, c, d in segs:
            if px < a or px > b or py < c or py > d:
                continue
            dx, dy = x1 - x0, y1 - y0
            t = ((px - x0) * dx + (py - y0) * dy) / (dx * dx + dy * dy or 1)
            t = 0 if t < 0 else 1 if t > 1 else t
            ex, ey = x0 + t * dx - px, y0 + t * dy - py
            if ex * ex + ey * ey < m2:
                return False
    return True


def placed(shape, angle):
    """The spoon at angle with its tip at TIP_X, standing on the floor: (points, tip)."""
    c, s = math.cos(angle), math.sin(angle)
    pts = [(x * c - y * s, x * s + y * c) for x, y in shape]
    tip = (TIP_X, BOTTOM - CLEAR - 0.1 - max(y for _, y in pts))
    return [(tip[0] + x, tip[1] + y) for x, y in pts], tip


def rocked(points, pivot, lift):
    c, s = math.cos(lift), math.sin(lift)
    return [(pivot[0] + (x - pivot[0]) * c - (y - pivot[1]) * s, pivot[1] + (x - pivot[0]) * s + (y - pivot[1]) * c)
            for x, y in points]


def lean_on(kind, m):
    """The spoon's rest on cup `kind` at morph m: (pivot, angle in radians, tip)."""
    segs = segments(VESSELS[kind])
    shape = morphed(m)
    a = math.radians(-89.0)
    assert clear(placed(shape, a)[0], segs), (kind, m, "upright")
    while clear(placed(shape, a + math.radians(1))[0], segs):
        a += math.radians(1)
    b = a + math.radians(1)
    for _ in range(20):
        mid = (a + b) / 2
        if clear(placed(shape, mid)[0], segs):
            a = mid
        else:
            b = mid
    angle = a
    rest_points, tip = placed(shape, angle)
    assert min(y for _, y in rest_points) >= HEADER_BOTTOM, (kind, m, "into the header", min(y for _, y in rest_points))
    # Rock about the lip's centre (where the rim rounds over) or the handle end's.
    lip = LIPS[kind]
    p = SPOON["wood"] if m < 0.5 else SPOON["silver"]
    length = (SPOON["wood"]["length"] + (SPOON["silver"]["length"] - SPOON["wood"]["length"]) * m) * MM
    cap = (SPOON["wood"]["end"] + (SPOON["silver"]["end"] - SPOON["wood"]["end"]) * m) / 2 * MM
    end = (tip[0] + (length - cap) * math.cos(angle), tip[1] + (length - cap) * math.sin(angle))
    for pivot in (lip, end):
        if all(clear(rocked(rest_points, pivot, math.radians(l / 2)), segs, CLEAR - 0.4) for l in range(1, int(LIFT_MOST * 2) + 1)):
            return pivot, angle, tip
    raise SystemExit(f"no pivot keeps the spoon off the {kind} cup at morph {m:.3f}")


out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{HEADER + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
               f'font-size="{size}px">-</text>')

# ---- The drink's lines: horizontals up to the drink's surface, as many
# as the drink is dark (powder). Each has a frothy twin, shown while bubbles
# fill the drink at its height.
for k in range(WATER_LINES):
    y = BOTTOM - WATER_STEP * (k + 0.6)
    inset = 3.0 + (k % 3) * 2.0
    pts = [(CX - INNER + inset + 2 * j * (INNER - inset) / 8, y + 0.22 * math.sin(j * 1.7 + k)) for j in range(9)]
    out.append(path(outline(ribbon(pts, 0.3)), G(7), 0.3, WATER + k, HIDDEN))
for k in range(WATER_LINES):
    y = BOTTOM - WATER_STEP * (k + 0.6)
    inset = 3.0 + (k % 3) * 2.0
    n = 24
    pts = [(CX - INNER + inset + j * 2 * (INNER - inset) / (n - 1), y + 2.0 * math.sin(j * 2.4 + k) * (0.7 + 0.3 * math.sin(j * 0.7)))
           for j in range(n)]
    out.append(path(outline(ribbon(pts, 0.3), 1), G(9), 0.3, FROTH + k, HIDDEN))
# Undissolved powder, more for less stirring (stir): a heap on the floor
# beside the spoon and clumps hanging in the drink. Each about its own
# node: the heap about the middle of its foot, scaled down as the drink is
# stirred in.
rng = random.Random(1982)   # Crawford's year
for _ in range(8 * 12):
    rng.random()            # the old sediment's draws, so the scatter below stays where it was
lumpy = random.Random(1879)
heap = [(x, -15.5 * (1 - (x / 23) ** 2) ** 0.7 * (1 + 0.09 * math.sin(x * 1.3) + 0.05 * math.sin(x * 2.9)))
        for x in [-23 + 46 * j / 22 for j in range(23)]]
out.append(path(outline(heap), G(6), STROKE, HEAP, HIDDEN, fill="#000000"))
for k in range(CLUMPS):
    r = lumpy.uniform(4.0, 5.0)
    clump = [(r * math.cos(a) * lumpy.uniform(0.7, 1.2), r * 0.75 * math.sin(a) * lumpy.uniform(0.7, 1.2))
             for a in [2 * math.pi * j / 9 for j in range(9)]]
    out.append(path(outline(clump), G(7), STROKE, CLUMP + k, HIDDEN, fill="#000000"))
# The stirred drink turning: the near halves of rings about the axis, a
# little tilted, each a stroke with an arrowhead's barb at its end; placed
# up the drink and narrowing toward the floor, a funnel (display.cpp).
for k in range(SWIRLS):
    a0, a1 = math.pi * (0.06 + 0.04 * k), math.pi * (0.9 - 0.03 * k)
    pts = [(52 * math.cos(a0 + (a1 - a0) * j / 23), 7 * math.sin(a0 + (a1 - a0) * j / 23) - 0.05 * 52 * math.cos(a0 + (a1 - a0) * j / 23))
           for j in range(24)]
    end = pts[-1]
    barb = [end, (end[0] + 4.5, end[1] - 3.2)]
    out.append(path(outline(ribbon(pts + barb[1:], 0.35)), G(8), 0.3, SWIRL + k, HIDDEN))

# The bubbles, the foam and the pops: rings, scaled to their radius.
for k in range(BUBBLES):
    out.append(circle(0, 0, 2, G(12), STROKE, BUBBLE + k, HIDDEN, fill="#000000"))
for k in range(FOAMS):
    out.append(circle(0, 0, 3, G(10), STROKE, FOAM + k, HIDDEN, fill="#000000"))
for k in range(POPS):
    out.append(circle(0, 0, 3, G(9), 0.35, POP + k, HIDDEN))
for _ in range(10 * 15):
    rng.random()            # the floating flecks' draws (gone: the lines' density shows the powder)

# The vessel (outline 0), as china.
half = [((x - VESSEL_AT[0]) / 2, (y - VESSEL_AT[1]) / 2) for x, y in VESSELS["china"]]
out.append(path(outline(half), G(14), STROKE, VESSEL, VESSEL_AT))
# The handles, at the left: china's thick C and tin's strap; glass has none.
L = CX - INNER


def band(outer_points, inner_points):
    return outer_points + inner_points[::-1]


wall = L - WALL["china"]
china = band(arc(wall + 10.5, 132, 46, math.pi * 0.57, math.pi * 1.43, 20, 44),
             arc(wall + 10.5, 132, 33, math.pi * 0.61, math.pi * 1.39, 20, 31))
out.append(path(outline(china), G(14), STROKE, HANDLE_CHINA, HIDDEN))
wall = L - WALL["tin"]
tin = band([(wall - 1, RIM + 10)] + arc(wall - 18, RIM + 35, 20, -math.pi * 0.5, -math.pi * 1.5, 16) + [(wall - 1, RIM + 60)],
           [(wall - 1, RIM + 13.5)] + arc(wall - 18, RIM + 35, 16.5, -math.pi * 0.54, -math.pi * 1.46, 16) + [(wall - 1, RIM + 56.5)])
out.append(path(outline(tin), G(14), STROKE, HANDLE_TIN, HIDDEN))
for _ in range(5 * 16 + 18):
    rng.random()            # tin's chips and china's crazing, gone: their draws, so the scatter below stays

# The drink's surface (outline 1), at 59 mm, flat.
surface = ribbon([(-INNER / 2 + 0.2 + i * (INNER - 0.4) / 31, 0.0) for i in range(32)], 0.18)
out.append(path(outline([(x, y) for x, y in surface]), G(13), STROKE, SURFACE, (CX, BOTTOM - 59 * MM)))
POSES = {k: [lean_on(k, i / (MORPHS - 1)) for i in range(MORPHS)] for k in ("china", "glass", "tin")}
TIP = POSES["china"][-1][2]
angle = POSES["china"][-1][1]
c, sn = math.cos(angle), math.sin(angle)
def turned(points, at):
    return [((x * c - y * sn) / 2, (x * sn + y * c) / 2) for x, y in points]
# Strokes beside the bowl's tip while it taps fast, and the ripples it sends
# across the floor: drawn before the spoon, about the tip in the spoon's
# frame (+x along it) and about the floor below the tip.
for k in range(SHAKES):
    r = 4.5 + 4.0 * k
    out.append(path(outline(ribbon(arc(2.0, 0, r, math.pi * 0.8, math.pi * 1.2, 7), 0.45)), G(11), 0.3, SHAKE + k, HIDDEN))
for k in range(RINGS):
    ring = arc(0, 0, 8, 0, 2 * math.pi * 31 / 32, 32, 2.2)
    out.append(path(outline(ring), G(11), 0.3, RING + k, HIDDEN))
# The spoon (outline 2), silver, at rest: its points turned here, about its
# middle, and halved, as display.cpp draws it.
half_length = SPOON["silver"]["length"] * MM / 2
out.append(path(outline(turned([(x - half_length, y) for x, y in spoon("silver")], TIP)), G(13), STROKE, SPOON_NODE,
                (TIP[0] + half_length * c, TIP[1] + half_length * sn)))
# The silver bowl's shine, about the tip, drawn at its own size. (No line
# along silver's handle: at 5 px wide it would merge with the outline at 1:1,
# and a mutable outline cannot be filled.)
sp = SPOON["silver"]
bl, bw = sp["bowl"] * MM, sp["width"] * MM
shine = ribbon(arc(0.55 * bl, 0, 0.3 * bl, math.pi * 0.62, math.pi * 1.15, 8, 0.3 * bw), 0.5)
out.append(path(outline(shine), G(12), 0.3, SHINE, HIDDEN))
# The wooden spoon's grain: a wavering line along its handle (about the
# handle's node) and a growth ring in its bowl (about the tip).
wp = SPOON["wood"]
x0, x1 = wp["bowl"] * MM + 3 - HANDLE_MID, wp["length"] * MM - 5 - HANDLE_MID
pts = [(x0 + (x1 - x0) * j / 14, 0.9 * math.sin(j * 0.8) * (0.4 + 0.6 * j / 14)) for j in range(15)]
out.append(path(outline(ribbon(pts, 0.35)), G(9), 0.3, GRAIN, HIDDEN))
bl, bw = wp["bowl"] * MM, wp["width"] * MM
ring = [(0.62 * bl + 0.3 * bl * math.cos(a), 0.27 * bw * math.sin(a) * (1 + 0.08 * math.sin(3 * a))) for a in
        [math.pi * (0.35 + 1.3 * j / 20) for j in range(21)]]
out.append(path(outline(ribbon(ring, 0.35)), G(9), 0.3, GRAIN + 1, HIDDEN))

for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{WEIGHT.get(i, STROKE)}"/>')
out.append("</g>")

body = "\n".join(out)
ids = [int(m) for m in re.findall(rf'id="{ID}_node_(\d+)"', body)]
assert sorted(ids) == list(range(NODES)), ("every node once", sorted(set(range(NODES)) - set(ids)))
static = len(re.findall("<path|<circle", body)) - NODES - len(GREYS)
(ROOT / "panel.svg.fragment").write_text(body + "\n")
print("wrote panel.svg.fragment,", NODES, "nodes,", static, "static paths,", len(body), "bytes")


# ---- plate.h: the outlines' shapes, Q10 px about their nodes.
def q10(points):
    return [(round(x * 1024), round(y * 1024)) for x, y in points]


def emit(name, comment, shapes):
    lines.append(comment)
    lines.append(f"const opxy::i32 {name}[{len(shapes)}][64][2] = {{")
    for s in shapes:
        pts = q10(s)
        assert len(set(pts)) == POINTS and all(abs(v) <= 64 * 1024 for p in pts for v in p), name
        lines.append("    {" + ", ".join(f"{{{x}, {y}}}" for x, y in pts) + "},")
    lines.append("};")


lines = ["#pragma once",
         "// Written by tools/make_panel.py: edit that and run it again.",
         "#include <opxy/types.hpp>",
         "",
         "namespace cocoa {",
         "namespace plate {"]
emit("vessel", "// Each cup in section (china, glass, tin), about the vessel's node, drawn at twice.",
     [[((x - VESSEL_AT[0]) / 2, (y - VESSEL_AT[1]) / 2) for x, y in VESSELS[k]] for k in ("china", "glass", "tin")])
emit("spoon", "// The spoon's whole outline (wood, silver), along +x about its middle, halved:\n"
     "// display.cpp turns its points and draws it upright at twice.",
     [[((x - SPOON[k]["length"] * MM / 2) / 2, y / 2) for x, y in spoon(k)] for k in ("wood", "silver")])
for k in ("china", "glass", "tin"):
    device_rules([((x - VESSEL_AT[0]) / 2, (y - VESSEL_AT[1]) / 2) for x, y in VESSELS[k]], f"the {k} vessel's outline")
for i in range(MORPHS):
    m = i / (MORPHS - 1)
    device_rules([((ax + (bx - ax) * m - (SPOON["wood"]["length"] + (SPOON["silver"]["length"] - SPOON["wood"]["length"]) * m) * MM / 2) / 2,
                   (ay + (by - ay) * m) / 2) for (ax, ay), (bx, by) in zip(spoon("wood"), spoon("silver"))], f"the spoon's outline at {m:.2f}")
lines.append("// Each spoon's half length, from its tip to its middle (Q10 px).")
lines.append("const opxy::i32 half_length[2] = {" + ", ".join(str(round(SPOON[k]["length"] * MM / 2 * 1024)) for k in ("wood", "silver")) + "};")
lines.append("// The spoon at rest, for each cup (china, glass, tin) and each of 33 shapes")
lines.append("// from wood to silver: its tip's height on the floor and the point it rocks")
lines.append("// about (Q10 px), and its angle (tenths of a degree, clockwise), leaning on")
lines.append("// the cup without crossing it (make_panel.py: lean_on).")
lines.append("const opxy::i32 tip_x = " + str(round(TIP_X * 1024)) + ";")
for name, pick in (("tip_y", lambda q: round(q[2][1] * 1024)),
                   ("pivot_x", lambda q: round(q[0][0] * 1024)), ("pivot_y", lambda q: round(q[0][1] * 1024)),
                   ("lean", lambda q: round(math.degrees(q[1]) * 10))):
    lines.append(f"const opxy::i32 {name}[3][{MORPHS}] = {{")
    for k in ("china", "glass", "tin"):
        lines.append("    {" + ", ".join(str(pick(q)) for q in POSES[k]) + "},")
    lines.append("};")
across = [round(rng.uniform(-INNER + 5, INNER - 5) * 1024) for _ in range(BUBBLES)]
up = [round(rng.uniform(0.02, 0.98) * 1024) for _ in range(BUBBLES)]
lines.append("// Each bubble's place across the mug (Q10 px from the axis) and up its")
lines.append("// class's column (Q10 of its height): a fixed scatter, five to a class.")
lines.append("const opxy::i32 across[40] = {" + ", ".join(map(str, across)) + "};")
lines.append("const opxy::i32 up[40] = {" + ", ".join(map(str, up)) + "};")
lines += ["}", "}"]
(ROOT / "plate.h").write_text("\n".join(lines) + "\n")
print("wrote plate.h")
