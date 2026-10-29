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
FRONT_Y, BACK_Y = 198.0, 150.0          # the stage floor: footlights' edge, back wall
FRONT_HALF, BACK_HALF = 170.0, 108.0    # its half widths there

# Greys that hold at 1:1 on the device: nothing dimmer than FAINT.
LINE, FAINT, BRIGHT, DIM = "#9a9a9a", "#5e5e5e", "#c8c8c8", "#787878"


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


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in distinct(points)) + " Z"


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
    left, right = [], []
    n = len(line)
    for i, (x, y) in enumerate(line):
        a, b = line[max(i - 1, 0)], line[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        nx, ny = -dy / length, dx / length
        w = width / 2 * (max(0.2, math.sin(math.pi * i / (n - 1))) if taper else 1)
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
# Three weights, used throughout: the apparition (drawn by display.cpp) the
# brightest and boldest; the stage's frame and apron a step down (FRAME);
# everything else on the stage another step down (SET).
def frame_line(points):
    return line(points, 0.8, LINE, w=0.5)


def set_line(points, taper=False):
    return line(points, 0.6, FAINT, taper, w=0.5)


# --- The proscenium: one arch on two plain piers, outlined twice.
art.append(frame_line([(44, 206), (44, 72)] + bezier((44, 72), (44, 50), (436, 50), (436, 72), 32)[1:] + [(436, 206)]))
art.append(set_line([(58, 198), (58, 76)] + bezier((58, 76), (58, 62), (422, 62), (422, 76), 32)[1:] + [(422, 198)]))
# --- The curtains: a drape each side, tied back.
for side in (False, True):
    drape = bezier((60, 78), (100, 104), (64, 146), (86, 172), 16) + bezier((86, 172), (76, 182), (66, 190), (62, 197), 6)[1:]
    art.append(set_line(mirror(drape) if side else drape))
# --- The stage: the apron's edge (the chalk path marks the floor).
art.append(frame_line([(58, FRONT_Y), (422, FRONT_Y)]))
# --- The glass: the great pane leaning over the stage, and two glints on it.
glass_top, glass_bottom = 88.0, FRONT_Y - 2
art.append(set_line([(240 - 156, glass_bottom), (240 - 112, glass_top), (240 + 112, glass_top), (240 + 156, glass_bottom)]))
for x0, y0, x1, y1 in ((142, 100, 130, 124), (344, 104, 334, 124)):
    art.append(set_line([(x0, y0), (x1, y1)], taper=True))

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
    f'<path id="{ID}_node_3" transform="matrix(1 0 0 1 240 174)" d="{outline(ring)}" fill="none" stroke="#555555" stroke-width="0.6"/>',
]
# Halos (12..15 the apparition's, 18..21 its twin's): ellipses round the upper
# body, drawn at their origin.
halo = ellipse(0, 0, 36, 46, 40)
dynamic += [f'<path id="{ID}_node_{n}" d="{outline(halo)}" fill="none" stroke="#444444" stroke-width="0.5"/>' for n in (12, 13, 14, 15, 18, 19, 20, 21)]
dynamic += [
    # The ghost's twin first, behind it, and its mouth; the ghost; its mouth (paths 1, 0, 2).
    f'<path id="{ID}_node_1" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed)}" fill="none" stroke="#555555" stroke-width="0.6"/>',
    # The twin's mouth (node 22): an oval at its origin, sized to the vowel.
    f'<path id="{ID}_node_22" d="{outline(ellipse(0, 0, 3, 3.5, 24))}" fill="none" stroke="#555555" stroke-width="0.5"/>',
    f'<path id="{ID}_node_0" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed)}" fill="none" stroke="#999999" stroke-width="0.6"/>',
    f'<path id="{ID}_node_2" transform="matrix(1 0 0 1 240 136)" d="{outline(ring)}" fill="none" stroke="#999999" stroke-width="0.5"/>',
]
# Two more apparitions for a second and third held note: the bowed figure, smaller.
small = outline([(x * 0.8, y * 0.8) for x, y in bowed])
dynamic += [f'<path id="{ID}_node_{n}" d="{small}" fill="none" stroke="#555555" stroke-width="0.5"/>' for n in (16, 17)]

palette = []
for i in range(16):
    grey = "#" + f"{i * 17:02x}" * 3
    # The weight follows the shade: the apparition (12 and up) boldest, its
    # halos and the chalk path (under 9) the lightest.
    width = 1.1 if i >= 12 else 0.8 if i >= 9 else 0.6
    palette.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{width}"/>')

out += art + dynamic + palette + ["</g>", ""]
(ROOT / "panel.svg.fragment").write_text("\n".join(out))
print(len("\n".join(out)), "bytes,", count[0], "static outlines")
