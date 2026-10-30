#!/usr/bin/env python3
"""Writes panel.svg.fragment, Drop's screen artwork. Run it after changing a
shape here and commit both files.

An engraved plate: a No. 8 cast-iron skillet, 270 mm across its rim and 48 mm
deep, with its two pour spouts, a helper handle and a 145 mm handle ending in a
teardrop hole, seen from 30 degrees above in parallel projection (the same as
display.h's scene), centred under the header. The drops are drawn 2.6 times
larger than the skillet, so their lobes can be counted.

Three weights, for the screen's 480 x 222 pixels: the drops brightest and
boldest (the palette's brightest greys carry its widest stroke), the
skillet's outline a step down, its inner edges another, and a few faint
strokes on its wall. The reshaping outlines are the drops' rims; each drop
also has its vapour layer's bright underside, its dark shadow and a plume of
steam, and the screen has spray, sparks, spatter dried on the iron, a water
mark where the next drop lands, the ratchet's teeth and the heat's rings.

Lines are closed rings or ribbons with a width of their own, each path
started on its longest edge: the OP-XY draws a path traced out and back, or
with points within an eighth of a pixel, or closing over less than a
quarter, as streaks (tests/frame_rules.cpp). Only the shadows and the
spatter are filled."""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "drop"
FONT = 'font-family="UniversNextPro-Light"'
# The palette's stroke widths, by grey: the brighter, the bolder.
WIDTH = lambda i: 1.5 if i >= 13 else 1.1 if i >= 11 else 0.9 if i >= 7 else 0.7
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# The scene (display.h): X right, Y away, Z up, mm about the floor's centre;
# the camera 30 degrees above, parallel projection, 0.9 px a mm.
CX, CY, SCALE = 188, 148, 0.9
SIN_E, COS_E = 0.5, 0.8660
# The skillet, mm: a No. 8.
RIM_OUT, RIM_IN, FLOOR_R, DEPTH = 135, 129, 116, 45
BASE_R, BASE_Z = 123, -5
SPOUT = 7                     # the spouts' reach past the rim, mm
# The ratchet's track (display.h): its centre and radius, mm.
TRACK_Y, TRACK_R = 35, 50
DROP_SCALE = 2.6              # px a mm, for the drops (display.h)
UNIT_R = 20                   # px: the radius the drops' rings are authored at

# Nodes (display.h).
RIMS, HEADER = 0, 3
UNDERSIDE, SPRAY, SPRAYS, SPARK, SPARKS = 11, 14, 6, 20, 4
STAIN, TEETH, HEAT, HEATS = 24, 25, 26, 4
STEAM = 30
SHADOW, MARK, MARKS = 33, 36, 12
HEAT_R = (38, 58, 78, 98)       # mm: the heat's discoloured rings on the iron
NODES = 48
POINTS = 64


def project(x, y, z):
    return CX + x * SCALE, CY - (y * SIN_E + z * COS_E) * SCALE


def num(v, places=4):
    text = f"{round(v, places):.{places}f}"
    text = text.rstrip("0").rstrip(".") if "." in text else text
    return "0" if text in ("", "-0") else text


def outline(points, places=4):
    """A closed path as the OP-XY draws one well: repeated points (as written,
    at `places`) dropped, and started on its longest edge, so that its
    closing edge is never a sliver."""
    pts = []
    for x, y in points:
        p = (num(x, places), num(y, places))
        if not pts or p != pts[-1]:
            pts.append(p)
    while len(pts) > 1 and pts[-1] == pts[0]:
        pts.pop()
    f = [(float(x), float(y)) for x, y in pts]
    start = max(range(len(f)), key=lambda i: math.hypot(f[i][0] - f[i - 1][0], f[i][1] - f[i - 1][1]))
    pts = pts[start:] + pts[:start]
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def path(d, fill, stroke="none", width=None, node=None, at=None):
    if node is not None and fill != "none":
        width = None   # the palette lends a filled node its stroke and width
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<path{ident}{move} d="{d}" fill="{fill}" stroke="{stroke}"{w}/>'


def circle(cx, cy, r, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}"{w}/>'


def text(x, y, size, fill, words):
    return (f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" {FONT} '
            f'font-size="{num(size)}px">{words}</text>')


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


def spout(theta):
    """How far the rim reaches out at angle theta (degrees): two pour spouts, front and back."""
    out = 0.0
    for at in (90, 270):
        d = (theta - at + 180) % 360 - 180
        if abs(d) < 14:
            out += SPOUT * math.cos(math.pi * d / 28) ** 2
    return out


def rim(r, z, a0=0.0, a1=360.0, n=96, lip=True):
    pts = []
    for i in range(n):
        t = a0 + (a1 - a0) * i / (n - 1 if a1 - a0 < 360 else n)   # a whole turn does not repeat its start
        rr = r + (spout(t) if lip else 0)
        a = math.radians(t)
        pts.append(project(rr * math.cos(a), rr * math.sin(a), z))
    return pts


def line(p0, p1, h=0.3, places=1):
    """A straight stroke: a thin quadrilateral, h wide."""
    length = math.hypot(p1[0] - p0[0], p1[1] - p0[1]) or 1
    nx, ny = -(p1[1] - p0[1]) / length * h, (p1[0] - p0[0]) / length * h
    return outline([p0, p1, (p1[0] + nx, p1[1] + ny), (p0[0] + nx, p0[1] + ny)], places)


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

static = 0


def engrave(d, grey, width):
    global static
    static += 1
    out.append(path(d, "none", G(grey), width))


# ---- The skillet. ----
# Its outline in one weight, its inner edges a step down, a few faint strokes.
OUTLINE, OUTLINE_W = 11, 1.0
EDGE, EDGE_W = 7, 0.7
FAINT, FAINT_W = 4, 0.6


RIBBON = 0.5   # px: a line's own width, so it has area (the OP-XY streaks a line traced out and back)


def stroke(points, h=RIBBON):
    """An open line as one closed path: a ribbon h wide."""
    return outline(ribbon(points, h), 1)


def engrave_line(points, grey, width):
    """A line in the static art, its stroke less the ribbon's own width."""
    engrave(stroke(points), grey, round(max(0.2, width - RIBBON), 2))


def engrave_long(points, grey, width, h=RIBBON):
    """A line too long for one path (the loader takes 64 points): ribbons that
    meet end to end. Its stroke is the line's width less the ribbon's own, so a
    ribbon under the stroke's width draws solid, as a single stroke would."""
    for i in range(0, len(points) - 1, 31):
        engrave(outline(ribbon(points[i:i + 32], h), 2), grey, round(max(0.2, width - h), 2))


def closed(points):
    return points + points[:1]


def check(body):
    """The device's rules for every artwork path: one subpath, at most 64 points,
    neighbours at least 1/8 px apart, a closing edge of at least 1/4 px."""
    for d in re.findall(r' d="([^"]+)"', body):
        assert d.count("M") == 1, d[:60]
        shape = [(float(x), float(y)) for x, y in re.findall(r"(-?[\d.]+),(-?[\d.]+)", d)]
        assert 3 <= len(shape) <= 64, (len(shape), d[:60])
        for i, a in enumerate(shape):
            b = shape[(i + 1) % len(shape)]
            assert abs(a[0] - b[0]) + abs(a[1] - b[1]) >= (0.25 if i == len(shape) - 1 else 0.125), (a, b)


# The outside: up the left side, round the bottom's front edge, up the right.
left_top = project(-RIM_OUT, 0, DEPTH)
right_top = project(RIM_OUT, 0, DEPTH)
bottom = rim(BASE_R, BASE_Z, 180, 360, 64, lip=False)
engrave_long([left_top] + bottom + [right_top], OUTLINE, OUTLINE_W)
# The rim, outside and in, with its spouts.
engrave_long(closed(rim(RIM_OUT, DEPTH, n=112)), OUTLINE, OUTLINE_W, 0.3)
engrave_long(closed(rim(RIM_IN, DEPTH, n=112)), EDGE, EDGE_W, 0.3)
# The floor's edge, where it can be seen behind the front wall.
engrave_long(rim(FLOOR_R, 0, -16, 196, 72, lip=False), EDGE, EDGE_W)
# Three faint strokes down each end of the outside wall, where it turns away.
for u in (0.93, 0.8, 0.62, -0.62, -0.8, -0.93):
    t = 180 + math.degrees(math.acos(u))
    a = math.radians(t)
    r_top = RIM_OUT + spout(t) - 1.5
    engrave_line([project(r_top * math.cos(a), r_top * math.sin(a), DEPTH - 3),
                  project((BASE_R + 1) * math.cos(a), (BASE_R + 1) * math.sin(a), BASE_Z + 3)], FAINT, FAINT_W)


# The helper handle opposite: a lug 26 mm out from the rim, 48 mm across, its
# front edge 7 mm thick.
def lug(n=30):
    x0 = -(RIM_OUT - 3)
    return [(x0 - 26 * math.cos(math.radians(p)), 24 * math.sin(math.radians(p)))
            for p in [-90 + 180 * i / (n - 1) for i in range(n)]]


engrave(outline([project(x, y, DEPTH + 0.5) for x, y in lug()], 1), OUTLINE, OUTLINE_W)
front_lug = [p for p in lug() if p[1] <= 0.5]
engrave_line([project(*front_lug[0], DEPTH + 0.5)] + [project(x, y, DEPTH - 6.5) for x, y in front_lug] +
             [project(*front_lug[-1], DEPTH + 0.5)], EDGE, EDGE_W)


# The handle: a flat bar 145 mm long rising 8 degrees, 32 mm wide at the rim
# narrowing to 25, a rounded end with a teardrop hole, its front side 9 mm thick.
def handle_plan(n=60):
    pts = []
    x0, x1, end_r = RIM_OUT - 6, 252, 17
    for i in range(n):                      # the near (front) edge, rim to end
        x = x0 + (x1 - x0) * i / (n - 1)
        pts.append((x, -(16 - 3.5 * (x - x0) / (x1 - x0))))
    for i in range(1, 40):                  # round the end
        a = -math.pi / 2 + math.pi * i / 40
        pts.append((x1 + 6 + end_r * math.cos(a) * 1.15, end_r * math.sin(a)))
    for i in range(n):                      # the far edge, back to the rim
        x = x1 - (x1 - x0) * i / (n - 1)
        pts.append((x, 12.5 + 3.5 * i / (n - 1)))
    return pts


def lift(x):
    return DEPTH + 1 + (x - RIM_OUT) * math.tan(math.radians(8))


plan = handle_plan()
engrave_long(closed([project(x, y, lift(x)) for x, y in plan]), OUTLINE, OUTLINE_W, 0.3)
front = [p for p in plan if p[1] < 0 or p[0] > 250]
engrave_long([project(x, y, lift(x) - 9) for x, y in front] + [project(*front[-1], lift(front[-1][0]))], EDGE, EDGE_W)
hole = []
for i in range(40):                         # a teardrop, its point toward the rim
    a = 2 * math.pi * i / 40
    r = 6 * (1 - 0.45 * math.cos(a)) if math.cos(a) < 0 else 6
    hole.append(project(258 + r * math.cos(a) * (1.6 if math.cos(a) < 0 else 1), r * math.sin(a), lift(258) + 1.5))
engrave(outline(hole, 1), EDGE, EDGE_W)

# ---- Nodes. ----
# The heat's discolouration: four ragged rings tempered into the iron round
# the burner, shown as the plate heats, the inner first. Only what shows over
# the front wall's line of sight is drawn.
for k, r in enumerate(HEAT_R):
    reach = math.degrees(math.asin(min(1, 46 / r)))
    pts = []
    for i in range(32):
        t = -reach + (180 + 2 * reach) * i / 31
        rr = r * (1 + 0.035 * math.sin(math.radians(3 * t) + k) + 0.025 * math.sin(math.radians(7 * t) + 2 * k))
        pts.append(project(rr * math.cos(math.radians(t)), rr * math.sin(math.radians(t)), 0))
    out.append(path(stroke(pts), "none", G(8), 0.9, HEAT + k))
# The ratchet's teeth: a sawtooth ring round the track, 56 teeth (drawn; the
# real ones are 1.5 mm), the slope facing the way they push (anticlockwise
# seen from above).
teeth = []
for i in range(32):
    a0, a1 = 2 * math.pi * i / 32, 2 * math.pi * (i + 0.85) / 32
    teeth.append(project((TRACK_R - 6) * math.cos(a0), TRACK_Y + (TRACK_R - 6) * math.sin(a0), 0))
    teeth.append(project((TRACK_R + 6) * math.cos(a1), TRACK_Y + (TRACK_R + 6) * math.sin(a1), 0))
out.append(path(outline(teeth, 1), "none", G(8), 0.9, TEETH))


def flat_ring(r, n=48, squash=SIN_E):
    return [(r * math.cos(2 * math.pi * i / n), -r * squash * math.sin(2 * math.pi * i / n)) for i in range(n)]


# The water mark where the next drop lands, irregular, at the first drop's site.
site = project(0, TRACK_Y - TRACK_R, 0)
stain = [(x * (1 + 0.06 * math.sin(3 * i * 2 * math.pi / 48 + 1) + 0.04 * math.sin(7 * i * 2 * math.pi / 48)),
          y * (1 + 0.06 * math.sin(3 * i * 2 * math.pi / 48 + 1) + 0.04 * math.sin(7 * i * 2 * math.pi / 48)))
         for i, (x, y) in enumerate(flat_ring(UNIT_R))]
out.append(path(outline(stain, 1), "none", G(7), 0.9, STAIN, site))

# Spatter dried on the iron round the drops' landing places, as many marks as
# the crackle: in place, still when no note plays. Four round each place,
# taken in turn so that a little crackle marks every place. Drawn under the
# drops; the screen fills them.
spots = [(270, 0), (150, 1), (30, 2)]
import random
rng = random.Random(8)
for j in range(MARKS):
    at, _ = spots[j % 3]
    sx, sy = TRACK_R * math.cos(math.radians(at)), TRACK_Y + TRACK_R * math.sin(math.radians(at))
    a = math.radians(rng.uniform(0, 360) if j < 3 else (j // 3) * 90 + rng.uniform(-30, 30) + (j % 3) * 40)
    d = rng.uniform(30, 44)
    x, y = project(sx + d * math.cos(a), sy + d * math.sin(a), 0)
    size = rng.uniform(6.5, 9.0)
    splat = []
    for i in range(10):   # a splash: five uneven spikes round a ring
        b = 2 * math.pi * i / 10 + j
        r = size * (rng.uniform(0.7, 1.4) if i % 2 == 0 else 0.4)
        splat.append((x + r * math.cos(b), y + r * math.sin(b) * SIN_E * 1.2))
    out.append(path(outline(splat, 1), "none", G(9), 0.9, MARK + j))
# Each drop's shadow on the iron: a dark ellipse, its footprint, drawn under
# everything on the drops, which stays on the iron as the vapour lifts the
# drop off it (the screen fills it).
for k in range(3):
    oval = [(UNIT_R * 0.9 * x / UNIT_R, y * 0.9) for x, y in flat_ring(UNIT_R, 32)]
    out.append(path(outline(oval, 1), "none", G(2), 0.7, SHADOW + k, site))
# The drops' rims: 64-point outlines.
for k in range(3):
    ring = [(x, y) for x, y in flat_ring(UNIT_R, POINTS)]
    out.append(path(outline(ring, 1), "none", G(15), 1.5, RIMS + k, (site[0] + 60 * k, site[1])))
# Each drop's vapour layer: its bright underside, seen as a crescent of light
# along its front, between the drop and the iron.
for k in range(3):
    arc = [(UNIT_R * 0.96 * math.cos(math.radians(t)), -UNIT_R * 0.96 * SIN_E * math.sin(math.radians(t)))
           for t in [196 + 148 * i / 23 for i in range(24)]]
    out.append(path(stroke(arc, 0.6), "none", G(10), 0.9, UNDERSIDE + k, site))
# Each drop's steam: a billowing plume rising from its top, 48 px tall,
# puffs widening as they rise; the screen scales it with how fast the drop
# evaporates. Its outline is the union of the puffs, edge by edge.
for k in range(3):
    puffs = [((1.2 + 0.45 * i) * (1 if (i + k) % 2 else -1), -3 - 5.6 * i, 2.6 + 0.75 * i) for i in range(8)]
    top = min(y - r for x, y, r in puffs)
    left, right = [], []
    for i in range(30):
        y = -0.5 + (top + 0.5) * i / 29
        xs = [(x - math.sqrt(r * r - (y - cy) ** 2), x + math.sqrt(r * r - (y - cy) ** 2))
              for x, cy, r in puffs if abs(y - cy) < r]
        if not xs:
            xs = [(-0.6, 0.6)]
        left.append((min(a for a, b in xs), y))
        right.append((max(b for a, b in xs), y))
    out.append(path(outline(right + left[::-1], 1), "none", G(11), 1.1, STEAM + k, site))
# Spray from a boiling drop, and the sparks of a floating one's brief contacts.
for k in range(SPRAYS):
    out.append(path(stroke([(-2.6, 0), (0, -0.8), (2.6, 0)]), "none", G(11), 1.1, SPRAY + k, site))
for k in range(SPARKS):
    out.append(path(stroke([(0, 0), (3.5, 0), (7, 0)]), "none", G(13), 1.5, SPARK + k, site))


for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{WIDTH(i)}"/>')
out.append("</g>")

ids = [int(m) for m in re.findall(rf'id="{ID}_node_(\d+)"', "\n".join(out))]
assert sorted(ids) == list(range(NODES)), ("every node once", sorted(set(range(NODES)) - set(ids)))
body = "\n".join(out) + "\n"
check(body)
assert len(body) <= 64 * 1024, len(body)
(ROOT / "panel.svg.fragment").write_text(body)
print("wrote panel.svg.fragment,", NODES, "nodes,", static, "static paths,", len(body), "bytes")
