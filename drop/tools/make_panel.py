#!/usr/bin/env python3
"""Writes panel.svg.fragment, Drop's screen artwork. Run it after changing a
shape here and commit both files.

An engraved plate in greys and fine line: a No. 8 cast-iron skillet, 270 mm
across its rim and 48 mm deep, with its two pour spouts and a 145 mm handle
ending in a teardrop hole, seen from 30 degrees above in parallel projection
(the same as display.h's scene). The drops are drawn 2.6 times larger than the
skillet, so their lobes can be counted. The reshaping outlines are the drops'
rims and the newest drop's reflection; every drop also has its vapour layer's
bright underside, an engraver's reflection, a shadow and a plume of steam, and the screen has spray, sparks, droplets,
spatter dried on the iron, a water mark where the next drop lands, the
ratchet's teeth and the heat's rings.

Lines are closed ribbons or rings; nothing large is filled."""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "drop"
FONT = 'font-family="UniversNextPro-Light"'
STROKE = 0.45
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# The scene (display.h): X right, Y away, Z up, mm about the floor's centre;
# the camera 30 degrees above, parallel projection, 0.95 px a mm.
CX, CY, SCALE = 175, 150, 1.0
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
RIMS, REFLECTION, HEADER = 0, 3, 4
UNDERSIDE, MIRROR, SPRAY, SPRAYS, SPARK, SPARKS = 12, 15, 18, 6, 24, 4
STAIN, TEETH, HEAT, HEATS = 28, 29, 30, 4
STEAM, SPATTER, SPATTERS = 34, 37, 6
SHADOW, MARK, MARKS = 43, 46, 12
HEAT_R = (38, 58, 78, 98)       # mm: the heat's discoloured rings on the iron
NODES = 58
POINTS = 64


def project(x, y, z):
    return CX + x * SCALE, CY - (y * SIN_E + z * COS_E) * SCALE


def num(v, places=4):
    text = f"{round(v, places):.{places}f}"
    text = text.rstrip("0").rstrip(".") if "." in text else text
    return "0" if text in ("", "-0") else text


def outline(points, places=4):
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


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
        t = a0 + (a1 - a0) * i / (n - (0 if a1 - a0 < 360 else 1) - (1 if a1 - a0 < 360 else 0))
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
# The exterior wall, under the rim at the front: its bottom edge, its two
# silhouettes and its engraved shading, upright strokes closer toward the ends.
bottom = rim(BASE_R, BASE_Z, 180, 360, 64, lip=False)
engrave(outline(ribbon(bottom, 0.5), 1), 9, 0.4)
for side in (180, 0):
    a = math.radians(side)
    top = project((RIM_OUT) * math.cos(a), 0, DEPTH)
    low = project(BASE_R * math.cos(a), 0, BASE_Z)
    engrave(line(top, low, 0.45), 9, 0.4)
for i in range(1, 18):
    u = 1 - 2 * i / 18                      # even across the wall's width, 15 px apart
    t = 180 + math.degrees(math.acos(u))    # 180 .. 360 through the front
    if 6 <= i <= 12 and i % 2:
        continue                            # sparser in the middle, where the light falls
    a = math.radians(t)
    r_top = RIM_OUT + spout(t) - 1.5
    p0 = project(r_top * math.cos(a), r_top * math.sin(a), DEPTH - 3)
    p1 = project((BASE_R + 1) * math.cos(a), (BASE_R + 1) * math.sin(a), BASE_Z + 2)
    engrave(line(p0, p1, 0.32), 6, 0.3)

# The rim: its outer and inner edges and the spouts.
engrave(outline(rim(RIM_OUT, DEPTH, n=112), 1), 12, 0.6)
engrave(outline(rim(RIM_IN, DEPTH, n=112), 1), 10, 0.45)

# Inside: the floor's edge where it can be seen (behind the front wall's
# line of sight) and the back wall's shading.
floor_edge = rim(FLOOR_R, 0, -16, 196, 72, lip=False)
engrave(outline(ribbon(floor_edge, 0.45), 1), 8, 0.4)
for i in range(15):
    u = 1 - 2 * (i + 0.5) / 15              # even across the screen, 16 px apart
    t = math.degrees(math.acos(u))
    a = math.radians(t)
    depth = math.sin(a)                     # the back wall is in shade; its middle darkest
    p0 = project((FLOOR_R + 1) * math.cos(a), (FLOOR_R + 1) * math.sin(a), 1.5)
    p1 = project((RIM_IN - 1) * math.cos(a), (RIM_IN - 1) * math.sin(a), DEPTH - 2)
    engrave(line(p0, p1, 0.3), 5 + 2 * (1 - depth), 0.28)

# The helper handle opposite: a lug 26 mm out from the rim, 48 mm across,
# with a raised lip round its top and its 7 mm thick front edge.
def lug(inset=0.0, n=30):
    x0 = -(RIM_OUT - 3)
    pts = [(x0 - (26 - inset) * math.cos(math.radians(p)), (24 - inset) * math.sin(math.radians(p)))
           for p in [-90 + 180 * i / (n - 1) for i in range(n)]]
    return pts


lug_top = [project(x, y, DEPTH + 0.5) for x, y in lug()]
engrave(outline(lug_top, 1), 12, 0.55)
engrave(outline([project(x, y, DEPTH + 1.6) for x, y in lug(3.5)], 1), 9, 0.35)
front_lug = [p for p in lug() if p[1] <= 0.5]
engrave(outline([project(x, y, DEPTH + 0.5) for x, y in front_lug] +
                [project(x, y, DEPTH - 6.5) for x, y in front_lug][::-1], 2), 8, 0.4)
for i in range(1, 4):
    x, y = front_lug[int(i * (len(front_lug) - 1) / 4)]
    engrave(line(project(x, y, DEPTH - 0.8), project(x, y, DEPTH - 5.5), 0.28), 5, 0.25)

# The light on the rim: short bright strokes along its back left, where it catches the light.
for a0, a1 in ((104, 118), (122, 131), (136, 149), (156, 161)):
    engrave(outline(ribbon(rim(RIM_OUT - 0.8, DEPTH + 0.2, a0, a1, 10), 0.4), 1), 14, 0.35)

# The handle: a flat bar 145 mm long rising 8 degrees, 32 mm wide at the rim
# narrowing to 25, a rounded end with a teardrop hole, a raised rib round its
# top, and its 9 mm thick front side.
def handle_plan(inset=0.0, n=60):
    pts = []
    x0, x1, end_r = RIM_OUT - 6, 252, 17
    for i in range(n):                      # the near (front) edge, rim to end
        x = x0 + (x1 - x0) * i / (n - 1)
        w = 16 - 3.5 * (x - x0) / (x1 - x0) - inset
        pts.append((x, -w))
    for i in range(1, 40):                  # round the end
        a = -math.pi / 2 + math.pi * i / 40
        r = end_r - inset
        pts.append((x1 + 6 + r * math.cos(a) * 1.15, r * math.sin(a)))
    for i in range(n):                      # the far edge, back to the rim
        x = x1 - (x1 - x0) * i / (n - 1)
        w = 12.5 + 3.5 * i / (n - 1) - inset
        pts.append((x, w))
    return pts


def lift(x):
    return DEPTH + 1 + (x - RIM_OUT) * math.tan(math.radians(8))


plan = handle_plan()
top = [project(x, y, lift(x)) for x, y in plan]
engrave(outline(top, 1), 12, 0.55)
rib = [project(x, y, lift(x) + 1.5) for x, y in handle_plan(3.2)]
engrave(outline(rib, 1), 9, 0.35)
front = [p for p in plan if p[1] < 0 or p[0] > 250]
side = [project(x, y, lift(x)) for x, y in front] + [project(x, y, lift(x) - 9) for x, y in front][::-1]
engrave(outline(side, 1), 8, 0.4)
for i in range(1, 9):                       # the side's shading, 14 px apart
    x, y = front[int(i * (len(front) - 1) / 9)]
    engrave(line(project(x, y, lift(x) - 1.5), project(x, y, lift(x) - 7.5), 0.28), 6, 0.25)
hole = []
for i in range(40):                         # a teardrop, its point toward the rim
    a = 2 * math.pi * i / 40
    r = 6 * (1 - 0.45 * math.cos(a)) if math.cos(a) < 0 else 6
    hole.append(project(258 + r * math.cos(a) * (1.6 if math.cos(a) < 0 else 1), r * math.sin(a), lift(258) + 1.5))
engrave(outline(hole, 1), 11, 0.45)

# ---- Nodes. ----
# The heat's discolouration: four ragged rings tempered into the iron round
# the burner, shown as the plate heats, the inner first. Only what shows over
# the front wall's line of sight is drawn.
for k, r in enumerate(HEAT_R):
    reach = math.degrees(math.asin(min(1, 46 / r)))
    pts = []
    for i in range(40):
        t = -reach + (180 + 2 * reach) * i / 39
        rr = r * (1 + 0.035 * math.sin(math.radians(3 * t) + k) + 0.025 * math.sin(math.radians(7 * t) + 2 * k))
        pts.append(project(rr * math.cos(math.radians(t)), rr * math.sin(math.radians(t)), 0))
    out.append(path(outline(ribbon(pts, 0.6), 1), "none", G(8 - k), 0.45, HEAT + k))
# The ratchet's teeth: a sawtooth ring round the track, 56 teeth (drawn; the
# real ones are 1.5 mm), the slope facing the way they push (anticlockwise
# seen from above).
teeth = []
for i in range(32):
    a0, a1 = 2 * math.pi * i / 32, 2 * math.pi * (i + 0.85) / 32
    teeth.append(project((TRACK_R - 6) * math.cos(a0), TRACK_Y + (TRACK_R - 6) * math.sin(a0), 0))
    teeth.append(project((TRACK_R + 6) * math.cos(a1), TRACK_Y + (TRACK_R + 6) * math.sin(a1), 0))
out.append(path(outline(teeth, 1), "none", G(8), 0.4, TEETH))


def flat_ring(r, n=48, squash=SIN_E):
    return [(r * math.cos(2 * math.pi * i / n), -r * squash * math.sin(2 * math.pi * i / n)) for i in range(n)]


# The water mark where the next drop lands, irregular, at the first drop's site.
site = project(0, TRACK_Y - TRACK_R, 0)
stain = [(x * (1 + 0.06 * math.sin(3 * i * 2 * math.pi / 48 + 1) + 0.04 * math.sin(7 * i * 2 * math.pi / 48)),
          y * (1 + 0.06 * math.sin(3 * i * 2 * math.pi / 48 + 1) + 0.04 * math.sin(7 * i * 2 * math.pi / 48)))
         for i, (x, y) in enumerate(flat_ring(UNIT_R))]
out.append(path(outline(stain, 1), "none", G(6), 0.45, STAIN, site))

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
    out.append(path(outline(splat, 1), "none", G(9), 0.4, MARK + j))
# Each drop's shadow on the iron: a dark ellipse, its footprint, drawn under
# everything on the drops, which stays on the iron as the vapour lifts the
# drop off it (the screen fills it).
for k in range(3):
    oval = [(UNIT_R * 0.9 * x / UNIT_R, y * 0.9) for x, y in flat_ring(UNIT_R, 32)]
    out.append(path(outline(oval, 1), "none", G(4), 0.4, SHADOW + k, site))
# The drops' rims and the newest one's reflection: 64-point outlines.
for k in range(4):
    ring = [(x, y) for x, y in flat_ring(UNIT_R, POINTS)]
    out.append(path(outline(ring, 0), "none", G(14 if k < 3 else 5), STROKE, RIMS + k if k < 3 else REFLECTION,
                    (site[0] + 60 * k, site[1])))
# Each drop's vapour layer: its bright underside, seen as a crescent of light
# along its front, between the drop and the iron.
for k in range(3):
    arc = [(UNIT_R * 0.96 * math.cos(math.radians(t)), -UNIT_R * 0.96 * SIN_E * math.sin(math.radians(t)))
           for t in [196 + 148 * i / 23 for i in range(24)]]
    out.append(path(outline(ribbon(arc, 0.55), 1), "none", G(10), 0.4, UNDERSIDE + k, site))
# An engraver's reflection under each drop: three short level strokes.
for k in range(3):
    marks = []
    for j, (w, dy) in enumerate(((0.7, 3.0), (0.48, 5.6), (0.26, 8.0))):
        marks.append(((-UNIT_R * w, dy), (UNIT_R * w, dy)))
    # Joined into one closed ribbon: out along the first, back along the third, through the middle.
    pts = [(-UNIT_R * 0.7, 3.0), (UNIT_R * 0.7, 3.0), (UNIT_R * 0.35, 8.5), (-UNIT_R * 0.35, 8.5)]
    out.append(path(outline(ribbon(pts, 0.35), 1), "none", G(4), 0.3, MIRROR + k, site))
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
    out.append(path(outline(right + left[::-1], 1), "none", G(11), 0.45, STEAM + k, site))
# Droplets round the newest drop, thrown by its brief contacts: small beads.
for k in range(SPATTERS):
    bead = [((2.6 + 0.4 * (k % 3)) * math.cos(2 * math.pi * i / 10), (2.6 + 0.4 * (k % 3)) * 0.75 * math.sin(2 * math.pi * i / 10))
            for i in range(10)]
    out.append(path(outline(bead, 1), "none", G(11), 0.45, SPATTER + k, site))
# Spray from a boiling drop, and the sparks of a floating one's brief contacts.
for k in range(SPRAYS):
    out.append(path(outline(ribbon([(-2.6, 0), (0, -0.8), (2.6, 0)], 0.7), 1), "none", G(11), 0.4, SPRAY + k, site))
for k in range(SPARKS):
    out.append(path(outline(ribbon([(0, 0), (3.5, 0), (7, 0)], 0.6), 1), "none", G(13), 0.35, SPARK + k, site))


for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')
out.append("</g>")

ids = [int(m) for m in re.findall(rf'id="{ID}_node_(\d+)"', "\n".join(out))]
assert sorted(ids) == list(range(NODES)), ("every node once", sorted(set(range(NODES)) - set(ids)))
body = "\n".join(out) + "\n"
(ROOT / "panel.svg.fragment").write_text(body)
print("wrote panel.svg.fragment,", NODES, "nodes,", static, "static paths,", len(body), "bytes")
