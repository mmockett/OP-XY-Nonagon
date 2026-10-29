#!/usr/bin/env python3
"""Writes panel.svg.fragment, Ghost's screen artwork, and figure.h, the
apparition's two poses, with the node numbers display.h names: run it after
changing a shape here, and commit all three.

The screen (README, "Screen"): Pepper's ghost (Dircks and Pepper, London
1862), a theatre's stage in front elevation: the proscenium, its curtains,
the floor and the great pane of glass leaning over the stage, in which the
apparition stands. Every outline is one closed subpath of distinct points,
so every outline encloses area: lines are thin closed ribbons."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "ghost"
FONT = 'font-family="UniversNextPro-Light"'

# Geometry shared with display.h (keep in step).
FRONT_Y = 198.0                         # the apron's edge; the figures' feet stay above the footlights on it

LINE = "#9a9a9a"   # the default stroke


def num(v):
    text = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def distinct(points):
    kept = []
    for x, y in points:
        p = (float(num(x)), float(num(y)))
        if not kept or p != kept[-1]:
            kept.append(p)
    if len(kept) > 1 and kept[0] == kept[-1]:
        kept.pop()
    return kept


# The OP-XY's drawing rules, beyond the SDK's (MNMD, docs/SCREEN.md): one
# subpath of at most 64 points; neighbouring points an eighth of a pixel or
# more apart on some axis (nearer, they merge); a closing edge a quarter of a
# pixel or more (shorter, it draws as a long sliver); some area, and no turn
# straight back (either draws as streaks).
APART = 1 / 8


def span(a, b):
    return max(abs(b[0] - a[0]), abs(b[1] - a[1]))


def drawable(points, exact=False):
    """The outline with near points dropped (unless exact: a reshaping
    outline's 64 points stay as they are), started where both its edges are
    longest; exits if it still breaks a rule."""
    kept = []
    for p in distinct(points):
        if exact or not kept or span(kept[-1], p) >= APART:
            kept.append(p)
    while not exact and len(kept) > 2 and span(kept[-1], kept[0]) < APART:
        kept.pop()
    m = len(kept)
    start = 0 if exact else max(range(m), key=lambda i: min(span(kept[i - 1], kept[i]), span(kept[i], kept[(i + 1) % m])))
    kept = kept[start:] + kept[:start]
    area, fault = 0.0, None
    for i in range(m):
        a, b, c = kept[i - 1], kept[i], kept[(i + 1) % m]
        ux, uy, vx, vy = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
        if span(a, b) < APART:
            fault = f"points {i - 1} and {i} merge"
        if ux * vx + uy * vy < 0 and abs(ux * vy - uy * vx) < 0.02 * math.hypot(ux, uy) * math.hypot(vx, vy):
            fault = f"turns straight back at point {i}"
        area += (b[0] * c[1] - c[0] * b[1]) / 2
    if m > 64:
        fault = f"{m} points"
    elif span(kept[-1], kept[0]) < 2 * APART:
        fault = "closes over less than a quarter of a pixel"
    elif abs(area) < 0.05:
        fault = "has no area"
    if fault:
        sys.exit(f"make_panel.py: an outline near ({kept[0][0]:.1f}, {kept[0][1]:.1f}) {fault}")
    return kept


def outline(points, exact=False):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in drawable(points, exact)) + " Z"


count = [0]


def path(points, stroke=LINE, width=0.5, fill="none", node=None, closed=True):
    width = max(width, 0.5)   # thinner strokes vanish at 1:1
    if node is None:
        count[0] += 1
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    return f'<path{ident} d="{outline(points)}" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>'


def ellipse(cx, cy, rx, ry, n=32, a0=0.0, a1=360.0, closed=True):
    m = n if closed else n + 1
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(m)]


def bezier(p0, p1, p2, p3, n=16):
    out = []
    for i in range(n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def ribbon(line, width, taper=False):
    # At most 31 points along it, so the ribbon keeps under 64; a taper keeps
    # its ends 0.3 px wide, so they close over more than a quarter of a pixel.
    if len(line) > 31:
        line = [line[round(i * (len(line) - 1) / 30)] for i in range(31)]
    left, right = [], []
    n = len(line)
    for i, (x, y) in enumerate(line):
        a, b = line[max(i - 1, 0)], line[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        nx, ny = -dy / length, dx / length
        w = max(0.15, width / 2 * math.sin(math.pi * i / (n - 1))) if taper else width / 2
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
    return left + right[::-1]


def line(points, width=0.5, stroke=LINE, taper=False, w=0.4):
    return path(ribbon(points, max(width, 0.6), taper), stroke, w)


def mirror(points):
    return [(480 - x, y) for x, y in points]


out = [f'<g id="{ID}_panel">',
       '<path d="M0,0 L480,0 L480,220 L0,220 Z" fill="#000000"/>',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{4 + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} font-size="{size}px">-</text>')

art = []
# Depth in grey: the proscenium, apron and footlights nearest and brightest
# (FRONT), the curtains a step behind (DRAPE), the glass in the middle
# (GLASS), the floor and back wall farthest and dimmest (BACK). The
# apparition (display.cpp) is brighter and bolder than all of them. No two
# outlines here cross (checked below); the figures, filled black, hide what
# stands behind them.
FRONT, DRAPE, GLASS, BACK = "#a8a8a8", "#8c8c8c", "#6e6e6e", "#5a5a5a"
outlines = []   # (name, points) of every line drawn, for the crossing check


def ink(points, colour, width, w=0.5, taper=False, name=""):
    outlines.append((name, points))
    return line(points, width, colour, taper, w)


def shape(points, colour, width=0.5, name=""):
    outlines.append((name, points + points[:1]))
    return path(points, colour, width)


# --- Background: the floor receding to the back wall, and the wall's corners.
BACK_Y, BACK_L, BACK_R = 145.0, 128.0, 352.0
art.append(ink([(BACK_L, BACK_Y), (BACK_R, BACK_Y)], BACK, 0.6, name="floor back"))
for side in (False, True):
    edge = [(70, FRONT_Y), (BACK_L, BACK_Y)]
    art.append(ink(mirror(edge) if side else edge, BACK, 0.6, name="floor side"))
    corner = [(BACK_L, BACK_Y), (BACK_L, 112)]
    art.append(ink(mirror(corner) if side else corner, BACK, 0.6, name="wall corner"))
# --- Middle ground: the glass's top edge, and two glints on the pane.
art.append(ink([(118, 104), (362, 104)], GLASS, 0.6, name="glass"))
for x0, y0, x1, y1 in ((152, 112, 143, 130), (337, 112, 328, 130)):
    art.append(ink([(x0, y0), (x1, y1)], GLASS, 0.8, taper=True, name="glint"))
# --- The curtains: two heavy swags meeting under the crest, with a tassel
# there; a drape each side, tied back with a tasselled cord.
for side in (False, True):
    top = bezier((60, 78), (120, 84), (180, 84), (238, 80), 14)
    low = bezier((238, 80), (190, 104), (110, 104), (60, 78), 14)[1:-1]
    swag = top + low
    art.append(shape(mirror(swag) if side else swag, DRAPE, 0.8, name="swag"))
    under = bezier((60, 78), (110, 104), (190, 104), (238, 80), 14)
    fold = [((p[0] + q[0]) / 2, (p[1] + q[1]) / 2) for p, q in zip(top, under)][3:-3]   # midway down the swag
    art.append(ink(mirror(fold) if side else fold, DRAPE, 0.6, taper=True, name="swag fold"))
    drape = bezier((60, 82), (104, 100), (98, 132), (96, 150), 12) + bezier((96, 150), (78, 166), (66, 184), (62, 197), 10)[1:]
    art.append(ink(mirror(drape) if side else drape, DRAPE, 0.8, name="drape"))
    cord = [(96, 150), (99, 157)]
    art.append(ink(mirror(cord) if side else cord, DRAPE, 0.6, name="cord"))
    tassel = [(99, 157), (102.5, 163), (101.5, 167), (96.5, 167), (95.5, 163)]
    art.append(shape(mirror(tassel) if side else tassel, DRAPE, 0.6, name="tassel"))
art.append(ink([(240, 82), (240, 88)], DRAPE, 0.6, name="centre cord"))
art.append(shape([(240, 88), (243.5, 94), (242.5, 98), (237.5, 98), (236.5, 94)], DRAPE, 0.6, name="centre tassel"))
# --- Foreground: two pilasters with capitals and bases, an arch springing
# from them with a cartouche at its head, the apron and its footlights.
for side in (False, True):
    pil = [(32, 210), (58, 210), (58, 204), (54, 204), (54, 78), (58, 78), (58, 72), (32, 72), (32, 78), (36, 78), (36, 204), (32, 204)]
    art.append(shape(mirror(pil) if side else pil, FRONT, 0.6, name="pilaster"))
    flute = [(45, 86), (45, 196)]
    art.append(ink(mirror(flute) if side else flute, DRAPE, 0.6, name="flute"))
CART = (240.0, 49.0, 15.0, 8.0)   # the cartouche: centre, half width, half height
cx, cy, rx, ry = CART
outer = [p for p in bezier((32, 70), (40, 40), (440, 40), (448, 70), 64) if ((p[0] - cx) / (rx + 3)) ** 2 + ((p[1] - cy) / (ry + 3)) ** 2 > 1]
half = len(outer) // 2
art.append(ink(outer[:half], FRONT, 0.9, name="arch outer"))
art.append(ink(outer[half:], FRONT, 0.9, name="arch outer"))
art.append(ink(bezier((58, 74), (66, 56), (414, 56), (422, 74), 48), FRONT, 0.9, name="arch inner"))
art.append(shape(ellipse(cx, cy, rx, ry, 28), FRONT, 0.7, name="cartouche"))
art.append(shape([(cx, cy - 5), (cx + 3.5, cy), (cx, cy + 5), (cx - 3.5, cy)], FRONT, 0.6, name="lozenge"))
art.append(ink([(58, FRONT_Y), (422, FRONT_Y)], FRONT, 1.0, name="apron"))
for k in range(7):
    x = 96 + 48 * k
    hood = ellipse(x, FRONT_Y - 0.6, 5, 3.6, 12, 180, 360, False)
    art.append(shape(hood, FRONT, 0.6, name="footlight"))


def crossings(items):
    """Pairs of outlines whose segments cross (touching ends allowed)."""
    def segs(points):
        return list(zip(points, points[1:]))
    def cross(a, b, c, d):
        def side(p, q, r):
            return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
        d1, d2, d3, d4 = side(c, d, a), side(c, d, b), side(a, b, c), side(a, b, d)
        return d1 * d2 < -1e-6 and d3 * d4 < -1e-6
    found = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if any(cross(a, b, c, d) for a, b in segs(items[i][1]) for c, d in segs(items[j][1])):
                found.append((items[i][0], items[j][0]))
    return found


bad = crossings(outlines)
if bad:
    sys.exit("make_panel.py: outlines cross: " + ", ".join(f"{a} / {b}" for a, b in bad))

# --- With no sample, Grains' hint, on one line under the apron, where nothing
# on the stage ever reaches:
# "load a sample", "shift +" and the Sample key's icon, in Grains' face and
# greys. Node 21, a black cover, hides it once a sample is loaded.
# Laid out by the face's own advance widths (tests/header_font.h), each
# widened by 15%, as the OP-XY draws artwork text wider than its advances,
# with a 10 px gap between the parts even then.
import re
_font = (ROOT / "tests/header_font.h").read_text()
_units = int(re.search(r"header_font_units = (\d+)", _font).group(1))
_advance = [int(v) for v in re.search(r"header_font_advance\[95\] = \{([^}]*)\}", _font).group(1).replace("\n", " ").split(",") if v.strip()]


def text_width(text, px):
    return sum(_advance[ord(c) - 32] for c in text) * px / _units * 1.15


BASE, ICON, GAP = 215.0, 15.0, 10.0
load_w, shift_w = text_width("load a sample", 15), text_width("shift +", 13)
LOAD_X = 240 - (load_w + GAP + shift_w + GAP + ICON) / 2
SHIFT_X = LOAD_X + load_w + GAP
ICON_X = SHIFT_X + shift_w + GAP
HINT = (LOAD_X - 4, ICON_X + ICON + 4, 200.0, 220.0)   # the cover: x0, x1, y0, y1
hint = [f'<text transform="matrix(1 0 0 1 {num(LOAD_X)} {num(BASE)})" fill="#CBCBCB" {FONT} font-size="15px">load a sample</text>',
        f'<text transform="matrix(1 0 0 1 {num(SHIFT_X)} {num(BASE)})" fill="#7D7D7D" {FONT} font-size="13px">shift +</text>']
# Grains' icon (a 28 px key at 256, 129), scaled to ICON.
k, KX, KY = ICON / 28, ICON_X, BASE - 12.5
icon = lambda x, y: (KX + (x - 256) * k, KY + (y - 129) * k)
hint.append(path([icon(x, y) for x, y in ((257.4237, 129), (282.5763, 129), (284, 130.4237), (284, 155.5763), (282.5763, 157),
                                         (257.4237, 157), (256, 155.5763), (256, 130.4237))], "#7D7D7D", 0.8))
hint.append(path([icon(x, y) for x, y in ellipse(270, 142.8814, 9.0763, 9.0763, 20)], "#7D7D7D", 0.6))
for x, top, bottom in ((265.6254, 142.2881, 143.5932), (267.0492, 142.2881, 143.5932), (268.5322, 139.2627, 146.5593), (269.9559, 140.8644, 145.0763),
                       (271.439, 142.2881, 143.5932), (272.922, 142.2881, 143.5932), (274.3458, 142.2881, 143.5932)):
    hint.append(path([icon(x, top), icon(x + 0.8, top), icon(x + 0.8, bottom), icon(x, bottom)], "#7D7D7D", 0.5, "#7D7D7D"))
x0, x1, y0, y1 = HINT
cover = f'<path id="{ID}_node_21" d="{outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])}" fill="#000000" stroke="#000000" stroke-width="0.5"/>'

# --- The apparition's poses: a robed figure, feet at the origin, 64 points.
# Each pose lists the same keypoints; the outline runs from the hem's left
# corner, round the hem, up the right side, over the hood and down the left.
BOWED = [(-17, 0), (-10, 0.8), (-3, -0.6), (4, 0.9), (11, -0.4), (17, 0),
         (15, -12), (13, -24), (12, -32), (14, -38), (13, -44), (9, -48), (6, -51), (5, -56), (2, -60), (-2, -60.5),
         (-5, -57), (-6.5, -52), (-9, -48), (-13, -44), (-14, -38), (-12, -32), (-13, -24), (-15, -12)]
SINGING = [(-18, 0), (-11, 1), (-3, -0.8), (4, 1), (11, -0.6), (18, 0),
           (15, -12), (12, -26), (13, -34), (20, -37), (21, -48), (9, -49), (6, -52), (5, -58), (2, -63), (-2, -63.5),
           (-5, -59), (-6, -52), (-9, -49), (-21, -48), (-20, -37), (-13, -34), (-12, -26), (-15, -12)]
HEM = 6   # the first six keypoints are the hem, which ripples


def catmull(points, n=64):
    """A closed Catmull-Rom curve through points, n samples, the hem's
    keypoints landing on whole samples so its ripple stays put."""
    k = len(points)
    per = n // k
    extra = n - per * k
    out, owner = [], []
    for i in range(k):
        p0, p1, p2, p3 = points[i - 1], points[i], points[(i + 1) % k], points[(i + 2) % k]
        m = per + (1 if i < extra else 0)
        for j in range(m):
            t = j / m
            t2, t3 = t * t, t * t * t
            x = 0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, y))
            owner.append(i)
    return out, owner


bowed, owner = catmull(BOWED)
singing, _ = catmull(SINGING)
hem_points = [i for i, o in enumerate(owner) if o < HEM - 1]
figure = ["#pragma once", "// Generated by tools/make_panel.py; edit the generator, not this file.",
          "#include <opxy/types.hpp>", "", "namespace ghost {",
          "// The apparition's outline, feet at the origin, in quarter pixels: bowed",
          "// (breath) and singing (ee); the voice moves it between them.",
          "const opxy::i16 figure_bowed[128] = {" + ", ".join(f"{round(x * 4)}, {round(y * 4)}" for x, y in bowed) + "};",
          "const opxy::i16 figure_singing[128] = {" + ", ".join(f"{round(x * 4)}, {round(y * 4)}" for x, y in singing) + "};",
          f"// Points 0..{hem_points[-1]} are the hem, which ripples.",
          f"const opxy::u32 hem_points = {hem_points[-1] + 1};",
          "// The mouth's place on the face, quarter pixels (bowed, singing).",
          "const opxy::i16 mouth_bowed[2] = {0, -212}, mouth_singing[2] = {0, -224};",
          "}", ""]
(ROOT / "figure.h").write_text("\n".join(figure))


ring = [(6 * math.cos(2 * math.pi * i / 64), 6 * math.sin(2 * math.pi * i / 64)) for i in range(64)]
dynamic = [
    # The chalk path on the floor (path 3), under everything: where the apparition walks.
    f'<path id="{ID}_node_3" transform="matrix(1 0 0 1 240 174)" d="{outline(ring, True)}" fill="none" stroke="#555555" stroke-width="0.6"/>',
]
# Halos (12..14 the apparition's, 17..19 its twin's): a nimbus round each
# head, rings drawn at their origin, filled black and drawn largest first, so
# each ring shows whole and hides the stage behind it; the figure, drawn
# after, hides the rings behind it.
halo = ellipse(0, 0, 15, 15, 40)   # a radius of 10 to 19 px by display.cpp's scale
dynamic += [f'<path id="{ID}_node_{n}" d="{outline(halo)}" fill="#000000" stroke="#444444" stroke-width="0.5"/>' for n in (14, 13, 12, 19, 18, 17)]
# Under each figure a black silhouette (nodes 22 and 23, the bowed figure a
# little narrower, so it sits inside every pose) hides the stage, the chalk
# path, the halos and the twin behind it.
mask = outline([(x * 0.95, y) for x, y in bowed])
dynamic += [
    # The ghost's twin first, behind it, and its mouth; the ghost; its mouth (paths 1, 0, 2).
    f'<path id="{ID}_node_23" transform="matrix(1 0 0 1 240 190)" d="{mask}" fill="#000000" stroke="#000000" stroke-width="0.5"/>',
    f'<path id="{ID}_node_1" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed, True)}" fill="none" stroke="#555555" stroke-width="0.6"/>',
    # The twin's mouth (node 20): an oval at its origin, sized to the vowel.
    f'<path id="{ID}_node_20" d="{outline(ellipse(0, 0, 3, 3.5, 24))}" fill="none" stroke="#555555" stroke-width="0.5"/>',
    f'<path id="{ID}_node_22" transform="matrix(1 0 0 1 240 190)" d="{mask}" fill="#000000" stroke="#000000" stroke-width="0.5"/>',
    f'<path id="{ID}_node_0" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed, True)}" fill="none" stroke="#999999" stroke-width="0.6"/>',
    f'<path id="{ID}_node_2" transform="matrix(1 0 0 1 240 136)" d="{outline(ring, True)}" fill="none" stroke="#999999" stroke-width="0.5"/>',
]
# Two more apparitions for a second and third held note: the bowed figure, smaller.
small = outline([(x * 0.8, y * 0.8) for x, y in bowed])
dynamic += [f'<path id="{ID}_node_{n}" d="{small}" fill="#000000" stroke="#555555" stroke-width="0.5"/>' for n in (15, 16)]

palette = []
for i in range(16):
    grey = "#" + f"{i * 17:02x}" * 3
    # The weight follows the shade: the apparition (12 and up) boldest, its
    # halos and the chalk path (under 9) the lightest.
    width = 1.1 if i >= 12 else 0.8 if i >= 9 else 0.6
    palette.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{width}"/>')

out += art + hint + [cover] + dynamic + palette + ["</g>", ""]
(ROOT / "panel.svg.fragment").write_text("\n".join(out))
print(len("\n".join(out)), "bytes,", count[0], "static outlines")
