#!/usr/bin/env python3
"""Writes panel.svg.fragment, Cocoa's screen artwork, and plate.h, the shapes
display.cpp reshapes its outlines to. Run it after changing a shape here and
commit all three.

One object fills the screen: a mug of hot chocolate in section, engraved as
in Tyndall's Sound or Ganot's Physics, to scale at 1.5 px a millimetre: 80 mm
across inside and 90 mm deep, its wall china, glass or enamelled tin, the
handle at the left, steam rising off the drink. A teaspoon stands in the
drink, its bowl on the floor and its handle on the rim or the wall, and rocks
to tap the floor. The drink shows its own pitch: while bubbles remain
its engraved lines are frothy, and they clear from the floor up as the
bubbles rise out; the drink is in tune when every line is clear. The lines'
density is the drink's darkness (powder), cocoa floats on it and sediment
lies on the floor where it has not been stirred in.

The four reshaping outlines are the vessel (by cup), the drink's surface
(meniscus, the stir's dip and the tap's ripples), and the spoon's bowl and
handle (by spoon: a wooden spoon to a silver teaspoon). Lines are closed
ribbons or rings; nothing large is filled."""
import math
import pathlib
import random
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "cocoa"
FONT = 'font-family="UniversNextPro-Light"'
STROKE = 0.9              # every node's line: about a pixel, so it holds at 1:1 on the 480 x 220 screen
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
VESSEL, SURFACE, BOWL, HANDLE = 0, 1, 2, 3
HEADER = 4
BUBBLE, BUBBLES = 12, 40
FOAM, FOAMS = 52, 20
POP, POPS = 72, 4
HANDLE_CHINA, HANDLE_TIN = 76, 77
GLINT, GLINTS = 78, 2
CHIP, CHIPS = 80, 5
HATCH = 85
WATER = 88                # clear lines, then the same lines frothy
FROTH = 108
SPECK, SPECKS = 128, 10
SEDIMENT, SEDIMENTS = 138, 8
SHINE = 146
STEAM, STEAMS = 147, 3
GRAIN, GRAINS = 150, 3    # the wooden spoon's grain: two along the handle, one in the bowl
SHAKE, SHAKES = 153, 3    # strokes beside the bowl while it taps fast
RING, RINGS = 156, 3      # ripples on the floor where the bowl taps
CLUMP, CLUMPS = 159, 5    # undissolved powder suspended in the drink
SWIRL, SWIRLS = 164, 4    # the stirred drink turning
NODES = 168
POINTS = 64
HIDDEN = (640, 320)


def num(v, places=4):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points, places=2):
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


def path(d, stroke, width=STROKE, node=None, at=None, fill="none"):
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
    """A thin line: out along the points, back offset along each point's normal by h."""
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


def bowl(kind):
    p = SPOON[kind]
    bl, w = p["bowl"] * MM, p["width"] * MM
    pts = []
    for i in range(POINTS):
        t = 2 * math.pi * i / POINTS
        x = bl / 2 * (1 - math.cos(t))
        y = w / 2 * math.sin(t) * (1 - 0.22 * x / bl)     # an egg, widest toward the tip
        neck = p["neck"] / 2 * MM * (x / bl) ** 8         # narrowing into the neck, not to a point
        pts.append((x, math.copysign(max(abs(y), neck), y if abs(y) > 1e-9 else (1 if i < POINTS // 2 else -1))))
    return pts


def handle(kind):
    p = SPOON[kind]
    x0, x1 = (p["bowl"] - 0.4) * MM, p["length"] * MM
    cap = p["end"] / 2 * MM
    upper = []
    for i in range(29):
        t = i / 28
        x = x0 + (x1 - cap - x0) * t
        widen = p["neck"] / 2 + (p["end"] / 2 - p["neck"] / 2) * (t ** 2.2 if kind == "silver" else t ** 0.4)
        if kind == "silver" and t > 0.55:   # the fiddle's waist and shoulder
            widen *= 1 - 0.18 * math.sin(math.pi * (t - 0.55) / 0.3) if t < 0.85 else 1
        upper.append((x, -widen * MM))
    end = arc(x1 - cap, 0, cap, -math.pi / 2 + 0.3, math.pi / 2 - 0.3, 6)
    pts = upper + end + [(x, -y) for x, y in upper[::-1]]
    pts = [(x - HANDLE_MID, y) for x, y in distinct(pts)]
    return fill_to(pts, POINTS)


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
HEADER_BOTTOM = 40.0 # px: nothing of the spoon above this, under the header


def morphed(m):
    """The spoon's outline in its own frame (tip at 0, +x along it), m from wood 0 to silver 1, densified."""
    shapes = []
    for part in ((bowl("wood"), bowl("silver"), 0.0), (handle("wood"), handle("silver"), HANDLE_MID)):
        a, b, shift = part
        pts = [(ax + (bx - ax) * m + shift, ay + (by - ay) * m) for (ax, ay), (bx, by) in zip(a, b)]
        dense = []
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            n = max(1, math.ceil(math.dist((x0, y0), (x1, y1)) / 1.5))
            dense += [(x0 + (x1 - x0) * j / n, y0 + (y1 - y0) * j / n) for j in range(n)]
        shapes += dense
    return shapes


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

# ---- Behind the drink, the far half of the mug's inside, shaded as a
# cylinder: upright lines, closer toward the sides, never under 8 px apart.
for i in (2, 4, 7, 13, 16, 18):
    x = CX - INNER * math.cos(math.pi * i / 20)
    out.append(path(outline(ribbon(line(x, RIM + 5, x, BOTTOM - 5, 3), 0.3)), G(4), 0.6))
# The water lines: engraved horizontals up to the drink's surface, as many
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
# beside the spoon, lumps about the floor and clumps hanging in the drink.
# Each about its own node: the heap about the middle of its foot, scaled
# down as the drink is stirred in.
rng = random.Random(1982)   # Crawford's year
for _ in range(SEDIMENTS * 12):
    rng.random()            # the old sediment's draws, so the scatter below stays where it was
lumpy = random.Random(1879)
heap = [(x, -15.5 * (1 - (x / 23) ** 2) ** 0.7 * (1 + 0.09 * math.sin(x * 1.3) + 0.05 * math.sin(x * 2.9)))
        for x in [-23 + 46 * j / 22 for j in range(23)]]
out.append(path(outline(heap), G(6), STROKE, SEDIMENT, HIDDEN, fill="#000000"))
for k in range(1, SEDIMENTS):
    r = lumpy.uniform(3.6, 5.2)
    mound = [(r * math.cos(a), -0.4 - r * 0.8 * math.sin(a) * lumpy.uniform(0.75, 1.15)) for a in [math.pi * j / 8 for j in range(9)]]
    out.append(path(outline(mound), G(6), STROKE, SEDIMENT + k, HIDDEN, fill="#000000"))
for k in range(CLUMPS):
    r = lumpy.uniform(3.2, 4.6)
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
# Cocoa powder floating on the surface: small irregular flecks.
for k in range(SPECKS):
    r = rng.uniform(1.6, 2.6) + 1.0
    fleck = [(r * math.cos(a) * rng.uniform(0.7, 1.2), r * 0.6 * math.sin(a) * rng.uniform(0.7, 1.2))
             for a in [2 * math.pi * j / 7 for j in range(7)]]
    out.append(path(outline(fleck), G(7), STROKE, SPECK + k, HIDDEN, fill="#000000"))

# The vessel (outline 0), as china.
half = [((x - VESSEL_AT[0]) / 2, (y - VESSEL_AT[1]) / 2) for x, y in VESSELS["china"]]
out.append(path(outline(half), G(14), STROKE, VESSEL, VESSEL_AT))
# The handles, at the left: china's thick C, tin's strap with its rivets
# showing as two small rings in the band; and the marks of each material:
# glass's glints and tin's chipped enamel.
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
for k, x in enumerate((L - WALL["glass"] + 1.6, CX + INNER + 1.6)):
    out.append(path(outline(ribbon(line(x, RIM + 12, x, BOTTOM - 16, 4), 0.7)), G(11), 0.3, GLINT + k, HIDDEN))
chips = [(L - 1.5, RIM + 3, 3.0), (CX + INNER + 1.2, RIM + 30, 2.6), (L - 0.8, 150, 2.2), (CX + INNER + 0.8, 176, 2.8),
         (CX - 30, BOTTOM + 4.2, 2.4)]
for k, (x, y, r) in enumerate(chips):
    chip = [(x + r * math.cos(a) * rng.uniform(0.6, 1.1), y + r * math.sin(a) * rng.uniform(0.6, 1.1))
            for a in [2 * math.pi * j / 8 for j in range(8)]]
    out.append(path(outline(chip), G(10), 0.35, CHIP + k, HIDDEN, fill="#000000"))
for _ in range(18):
    rng.random()            # china's crazing, gone (too fine for the screen): its draws, so the scatter below stays

# China's cut wall and base, hatched at 45 degrees as an engraver sections a
# solid: a sawtooth across each band, its back along the outer edge.
def hatch(x0, y0, x1, y1, step=5.0):
    """Diagonal strokes across the band x0..x1 (width) running down y0..y1."""
    w = x1 - x0
    pts, y = [], y0
    while y + w < y1:
        pts += [(x1, y), (x0, y + w)]
        y += step
    back = [(x1 + 0.35, y + w - step), (x1 + 0.35, y0 - 0.35)]
    return pts + back


def hatch_across(x0, x1, y0, y1, step=5.0):
    """The same across a horizontal band (the base), left to right."""
    h = y1 - y0
    pts, x = [], x0
    while x + h < x1:
        pts += [(x, y1), (x + h, y0)]
        x += step
    return pts + [(x + h - step, y1 + 0.35), (x0 - 0.35, y1 + 0.35)]


w = WALL["china"]
out.append(path(outline(hatch(L - w + 0.6, RIM + 5, L - 0.6, BOTTOM + 2)), G(5), 0.25, HATCH, HIDDEN))
out.append(path(outline(hatch(CX + INNER + 0.6, RIM + 5, CX + INNER + w - 0.6, BOTTOM + 2)), G(5), 0.25, HATCH + 1, HIDDEN))
out.append(path(outline(hatch_across(L + 6, CX + INNER - 14, BOTTOM + 0.6, BOTTOM + 8 * MM - 2.2)), G(5), 0.25, HATCH + 2, HIDDEN))

# Steam off the hot drink: three wisps rising from the mouth of the mug.
for k, (x, phase) in enumerate(((CX - 30, 0.0), (CX - 4, 1.9), (CX + 22, 3.7))):
    pts = [(x + 3.2 * math.sin(phase + j * 0.55) + j * 0.35, RIM - 3 - j * 1.7) for j in range(11)]
    out.append(path(outline(ribbon(pts, 0.4)), G(6), 0.3, STEAM + k, HIDDEN))

# The drink's surface (outline 1), at 59 mm, flat.
surface = ribbon([(-INNER / 2 + 0.2 + i * (INNER - 0.4) / 31, 0.0) for i in range(32)], 0.18)
out.append(path(outline([(x, y) for x, y in surface]), G(13), STROKE, SURFACE, (CX, BOTTOM - 59 * MM)))
# The spoon: its bowl (outline 2) and handle (outline 3), silver, at rest
# in the drink.
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
out.append(path(outline(turned(bowl("silver"), TIP)), G(15), STROKE, BOWL, TIP))
out.append(path(outline(turned(handle("silver"), TIP)), G(15), STROKE, HANDLE, at=
                (TIP[0] + HANDLE_MID * c, TIP[1] + HANDLE_MID * sn)))
# The silver bowl's shine, about the tip, drawn at its own size. (No line
# along silver's handle: at 5 px wide it would merge with the outline at 1:1,
# and a mutable outline cannot be filled.)
sp = SPOON["silver"]
bl, bw = sp["bowl"] * MM, sp["width"] * MM
shine = ribbon(arc(0.55 * bl, 0, 0.3 * bl, math.pi * 0.62, math.pi * 1.15, 8, 0.3 * bw), 0.5)
out.append(path(outline(shine), G(12), 0.3, SHINE, HIDDEN))
# The wooden spoon's grain: two wavering lines along its handle (about the
# handle's node) and a growth ring in its bowl (about the tip).
wp = SPOON["wood"]
x0, x1 = wp["bowl"] * MM + 3 - HANDLE_MID, wp["length"] * MM - 5 - HANDLE_MID
for k, (y, phase) in enumerate(((-3.6, 0.0), (3.2, 2.1))):
    pts = [(x0 + (x1 - x0) * j / 14, y + 0.9 * math.sin(phase + j * 0.8) * (0.4 + 0.6 * j / 14)) for j in range(15)]
    out.append(path(outline(ribbon(pts, 0.35)), G(9), 0.3, GRAIN + k, HIDDEN))
bl, bw = wp["bowl"] * MM, wp["width"] * MM
ring = [(0.62 * bl + 0.3 * bl * math.cos(a), 0.27 * bw * math.sin(a) * (1 + 0.08 * math.sin(3 * a))) for a in
        [math.pi * (0.35 + 1.3 * j / 20) for j in range(21)]]
out.append(path(outline(ribbon(ring, 0.35)), G(9), 0.3, GRAIN + 2, HIDDEN))

for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')
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
emit("bowl", "// The spoon's bowl about its tip (wood, silver), drawn at twice.",
     [[(x / 2, y / 2) for x, y in bowl(k)] for k in ("wood", "silver")])
emit("handle", f"// The spoon's handle about a point {HANDLE_MID:g} px from the tip (wood, silver), drawn at twice.",
     [[(x / 2, y / 2) for x, y in handle(k)] for k in ("wood", "silver")])
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
lines.append("// Where each powder fleck floats, Q10 px from the axis.")
lines.append("const opxy::i32 fleck[10] = {" + ", ".join(str(round((-INNER + 8 + k * (2 * INNER - 16) / 9 + rng.uniform(-2, 2)) * 1024)) for k in range(SPECKS)) + "};")
lines += ["}", "}"]
(ROOT / "plate.h").write_text("\n".join(lines) + "\n")
print("wrote plate.h")
