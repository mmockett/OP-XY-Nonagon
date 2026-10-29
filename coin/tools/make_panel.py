#!/usr/bin/env python3
"""Writes panel.svg.fragment, Coin's screen artwork, and plate.h, the face
patterns display.cpp engraves on the disk. Run it after changing a shape here
and commit all three.

The whole screen is the Euler's Disk toy: a heavy chrome-steel disk spinning
on its concave mirror, seen from 16 degrees above in mild perspective (the
same projection as display.cpp's), drawn in a few greys and line weights:
the disk's rim brightest and boldest, its edge, face and the base a step
down, the effects a step further. The base is a plain ring with its side.
The note's disk is the four reshaping outlines: its face's rim, its edge,
its reflection and what is engraved on its face. Everything else that moves
is nodes: the motion lines of the contact path, the eccentric weight, the
contact point's glint, the air squeezed from under the low edge, the ring's
ripples, wobble's ghost of the rim, wood's grain and stone's specks, and
the companion disks many adds.

Lines are closed ribbons or rings; nothing large is filled."""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "coin"
FONT = 'font-family="UniversNextPro-Light"'
STROKE = 0.7                                    # every node's line: about a pixel on the 480 x 222 screen
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# The projection (display.h: scene). X right, Y away, Z up, pixels about the
# mirror's centre; the camera 16 degrees above, 1400 px away, so the whole
# base fits below the header with its front edge in view.
CX, CY, DIST = 240, 136, 1400
SIN_E, COS_E = 4516 / 16384, 15749 / 16384      # display.h's Q14 values
BASE_R, BASE_H = 210, 8
REF_R = 77                                      # the motion lines as authored: size 1:8's 70 px, and 7
RIPPLE_R, RIPPLE_STEP = 86, 100 / 7            # the ring's ripples as authored: 16 px out from size 1:8's rim to 186
COMPANION_R, COMPANION_W = 50, 15               # a companion disk as authored
MIRRORED = 0.4                                  # the concave mirror's image, flattened

# Nodes (display.h).
DISK, EDGE, REFLECTION, FACE = 0, 1, 2, 3
HEADER = 4
TRAIL, TRAILS = 12, 24
WEIGHT, GLINT = 36, 37
GRAIN, GRAINS = 38, 5
SPECK, SPECKS = 43, 6                           # four specks and two veins
COMPANION = 49                                  # three disks: face, edge, reflection, glint
AIR, AIRS = 61, 5
RIPPLE, RIPPLES = 66, 8                         # the ring's ripples across the mirror
GHOST, GHOSTS = 74, 8                           # wobble's ghost of the rim, in dashes
NODES = 82
POINTS = 64


def project(x, y, z):
    k = DIST / (DIST + y * COS_E - z * SIN_E)
    return CX + x * k, CY - (y * SIN_E + z * COS_E) * k


def flat(x, y, z):
    """Orthographic, about the origin: for nodes display.cpp places and scales."""
    return x, -(y * SIN_E + z * COS_E)


def num(v, places=4):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
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
    if node is not None and fill != "none":
        width = None
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
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


def ring(r, z=0.0, n=64, start=0.0, to=None):
    to = to or project
    return [to(r * math.cos(start + 2 * math.pi * i / n), r * math.sin(start + 2 * math.pi * i / n), z) for i in range(n)]


def arc(r, a0, a1, z=0.0, n=8, to=None):
    to = to or project
    return [to(r * math.cos(a0 + (a1 - a0) * i / (n - 1)), r * math.sin(a0 + (a1 - a0) * i / (n - 1)), z) for i in range(n)]


def polar_point(r, degrees, z=0.0):
    a = math.radians(degrees)
    return project(r * math.cos(a), r * math.sin(a), z)


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

# ---- The base: a ring round a concave mirror, its top and its side. ----
front = [math.pi * i / 63 for i in range(64)]
top = [project(BASE_R * math.cos(-a), BASE_R * math.sin(-a), 0) for a in front]
bottom = [project(BASE_R * math.cos(-a), BASE_R * math.sin(-a), -BASE_H) for a in front]
out.append(path(outline(top + bottom[::-1], 2), "none", G(9), 0.8))
out.append(path(outline(ring(BASE_R, 0, 128), 2), "none", G(12), 1.0))

# ---- Each base's own marks, in the front of the base's mirror ring (glass
# has none: it shows the disk's reflection). ----
# Wood: a flat-sawn top, its grain running across the base in front of the
# disk, long wavy lines that stop at the ring.
for i, y in enumerate((-104, -122, -140, -158, -176)):
    span = math.sqrt(max(0.0, 188 ** 2 - y * y))
    xs = [-span + 2 * span * k / 23 for k in range(24)]
    pts = [project(x, y + 3.5 * math.sin(x / 23 + i * 1.7) + 1.5 * math.sin(x / 9 + i), 0) for x in xs]
    out.append(path(outline(ribbon(pts, 0.6), 2), "none", G(9), STROKE, GRAIN + i))
# Stone: four specks and two veins.
specks = [(172, 222), (158, 262), (150, 306), (166, 340)]
for i, (r, at) in enumerate(specks):
    px, py = polar_point(r, at)
    out.append(circle(px, py, 2.2, G(9), G(9), STROKE, SPECK + i))
for i, (r, a0, a1) in enumerate(((170, 214, 268), (160, 284, 336))):
    pts = [polar_point(r + 5 * math.sin(k * 1.3), a0 + (a1 - a0) * k / 15) for k in range(16)]
    out.append(path(outline(ribbon(pts, 0.6), 2), "none", G(9), STROKE, SPECK + 4 + i))

# ---- The note's disk: four reshaping outlines, nodes 0-3, placed at the
# mirror's centre at twice their size. Authored flat at size 1:8.
def disk_points(r, z):
    return [((x - CX) / 2, (y - CY) / 2) for x, y in ring(r, z, POINTS)]


R, W = 70, 21
out.append(path(outline(disk_points(R, -0.01)), "none", G(6), STROKE, REFLECTION, (CX, CY)))
low, edge = disk_points(R, 0), disk_points(R, W)
out.append(path(outline(low[32:] + edge[32:][::-1]), "none", G(11), STROKE, EDGE, (CX, CY)))
out.append(path(outline(disk_points(R, W)), "none", G(15), STROKE, DISK, (CX, CY)))
out.append(path(outline(disk_points(R * 0.6, W + 0.01)), "none", G(10), STROKE, FACE, (CX, CY)))

# The contact path's 24 motion lines, about the mirror's centre for size 1:8
# (display.cpp scales them with the disk; the disk hides the back).
for i in range(TRAILS):
    a0, a1 = math.radians(15 * i + 1.5), math.radians(15 * i + 13.5)
    pts = [(x - CX, y - CY) for x, y in arc(REF_R, a0, a1, 0, 5)]
    out.append(path(outline(ribbon(pts, 0.5), 3), "none", G(9), STROKE, TRAIL + i, (CX, CY)))
# The eccentric weight: a lead balance weight clamped to the rim, a tab 12
# by 9.
tab = [(-6, -4.5), (0, -5), (6, -4.5), (6, 4.5), (0, 5), (-6, 4.5)]
out.append(path(outline(tab), "none", G(12), STROKE, WEIGHT, (CX, CY)))
out.append(circle(0, 0, 2.6, "none", G(14), STROKE, GLINT, (CX, CY + REF_R * SIN_E)))

# ---- Three companion disks, for many: each authored flat about the point of
# the base under its centre, size 50 px and 15 thick (display.cpp places,
# scales, rolls and lifts them).
cr, cw = COMPANION_R, COMPANION_W
for k in range(3):
    base = COMPANION + 4 * k
    face = ring(cr, cw, POINTS, 0, flat)
    lo = ring(cr, 0, POINTS, 0, flat)
    mirror = ring(cr, -cw * MIRRORED, POINTS, 0, flat)
    out.append(path(outline(face, 3), "none", G(13), STROKE, base, (60 + 120 * k, 260)))
    out.append(path(outline(lo[32:] + face[32:][::-1], 3), "none", G(9), STROKE, base + 1, (60 + 120 * k, 260)))
    out.append(path(outline(lo[32:] + mirror[32:][::-1], 3), "none", G(5), STROKE, base + 2, (60 + 120 * k, 260)))
    out.append(circle(0, 0, 2.4, "none", G(13), STROKE, base + 3, (60 + 120 * k, 260)))

# ---- The air squeezed from under the low edge: fine curved streaks, each
# along +x about its start (display.cpp turns them outward from the contact).
for i in range(AIRS):
    length = 34 + 6 * (i % 3)
    pts = [(length * k / 9, 3.2 * math.sin(math.pi * k / 9) * (1 if i % 2 else -1)) for k in range(10)]
    out.append(path(outline(ribbon(pts, 0.7), 3), "none", G(9), STROKE, AIR + i, (CX, CY)))

# ---- The ring's ripples across the mirror in front of the disk, each an arc
# of the base about the mirror's centre, from 16 px beyond size 1:8's rim out
# to 186 px (display.cpp scales them with the disk and shows as many as
# ring sets).
for i in range(RIPPLES):
    r = RIPPLE_R + RIPPLE_STEP * i
    pts = [(x - CX, y - CY) for x, y in arc(r, math.radians(200), math.radians(340), 0, 32)]
    out.append(path(outline(ribbon(pts, 0.7), 3), "none", G(8), STROKE, RIPPLE + i, (CX, CY)))

# ---- Wobble's ghost: eight dashes, each 24 px along +x about its middle
# (display.cpp lays them along the rim of the disk rocked further over).
for i in range(GHOSTS):
    out.append(path(outline(ribbon([(-12, 0), (12, 0)], 0.5), 3), "none", G(9), STROKE, GHOST + i, (CX, CY)))

for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')
out.append("</g>")

body = "\n".join(out)
ids = [int(m) for m in re.findall(rf'id="{ID}_node_(\d+)"', body)]
assert sorted(ids) == list(range(NODES)), ("every node once", sorted(set(range(NODES)) - set(ids)))
static_paths = len(re.findall(r"<(?:path|circle)(?![^>]*id=)", body))
(ROOT / "panel.svg.fragment").write_text(body + "\n")
print("wrote panel.svg.fragment,", NODES, "nodes,", static_paths, "static paths,", len(body), "bytes")

# ---- plate.h: the face patterns, as polar points (radius as a share of the
# disk's, Q10, and angle in tenths of a degree), 64 each.
def polar(points):
    return [(round(r * 1024), round(math.degrees(a) * 10) % 3600) for r, a in points]


def resample(vertices, n):
    """n points along a closed polygon of (r, angle) vertices, evenly by edge."""
    per = n // len(vertices)
    pts = []
    for i, (r0, a0) in enumerate(vertices):
        r1, a1 = vertices[(i + 1) % len(vertices)]
        x0, y0, x1, y1 = r0 * math.cos(a0), r0 * math.sin(a0), r1 * math.cos(a1), r1 * math.sin(a1)
        for j in range(per):
            t = j / per
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            pts.append((math.hypot(x, y), math.atan2(y, x)))
    return pts


rose = [(0.78 if i % 2 == 0 else 0.34, math.pi * i / 8) for i in range(16)]
milled = []
for i in range(16):
    a = 2 * math.pi * i / 16
    milled += [(0.9, a), (0.9, a + 0.16), (0.68, a + 0.23), (0.68, a + 0.36)]
bevel = [(0.84, 2 * math.pi * i / 64) for i in range(64)]   # glass: display.cpp draws the underside's rim instead
# Wood: an end-grain growth ring, wavy and a little off centre: one closed
# line with area, as the device needs (a line traced out and back merges into
# a streak when the face is seen at a low angle).
grain = []
for i in range(POINTS):
    t = 2 * math.pi * i / POINTS
    r = 0.52 + 0.05 * math.sin(3 * t + 0.7) + 0.025 * math.sin(7 * t + 1.9)
    x, y = 0.1 + r * math.cos(t), 0.05 + r * math.sin(t)
    grain.append((math.hypot(x, y), math.atan2(y, x)))
patterns = [polar(resample(rose, POINTS)), polar(milled), polar(bevel), polar(grain)]
for p in patterns:
    assert len(p) == POINTS and len(set(p)) == POINTS
lines = ["#pragma once",
         "// Written by tools/make_panel.py: edit that and run it again.",
         "#include <opxy/types.hpp>",
         "",
         "namespace coin {",
         "namespace plate {",
         "// What is engraved on each disk's face, as 64 polar points: radius as a",
         "// share of the disk's (Q10) and angle (tenths of a degree). Steel: an",
         "// eight-point rose. Brass: a milled ring. Glass: its bevel. Wood: a growth ring.",
         "const opxy::u16 face_radius[4][64] = {"]
lines += ["    {" + ", ".join(str(r) for r, _ in p) + "}," for p in patterns]
lines += ["};", "const opxy::u16 face_angle[4][64] = {"]
lines += ["    {" + ", ".join(str(a) for _, a in p) + "}," for p in patterns]
lines += ["};", "}", "}"]
(ROOT / "plate.h").write_text("\n".join(lines) + "\n")
print("wrote plate.h")
