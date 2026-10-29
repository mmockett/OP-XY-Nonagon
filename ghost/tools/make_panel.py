#!/usr/bin/env python3
"""Writes panel.svg.fragment, Ghost's screen artwork, and figure.h, the
apparition's two poses, with the node numbers display.h names: run it after
changing a shape here, and commit all three.

The screen (README, "Screen"): Pepper's ghost (Dircks and Pepper, London
1862) as an engraved plate, a theatre's stage in front elevation: the
proscenium, the curtains, a painted crypt on the back wall, the floor and
footlights, and the great pane of glass leaning over the stage, in which the
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
DOOR = (201.0, 281.0, 121.0, 149.0)      # the crypt's doorway, where the hint shows: x0, x1, y0, y1

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
# --- The backdrop: a painted crypt, three gothic arches on columns, faint.
def gothic(cx, base, half, spring, peak):
    left = [(cx - half, base), (cx - half, spring)] + bezier((cx - half, spring), (cx - half, spring - (spring - peak) * 0.55), (cx - half * 0.35, peak + 2), (cx, peak), 8)[1:]
    right = [(2 * cx - x, y) for x, y in reversed(left)]
    return left + right[1:]


for cx, half, peak, spring in ((168, 24, 104, 124), (240, 42, 92, 118), (312, 24, 104, 124)):
    arch = gothic(cx, BACK_Y, half, spring, peak)
    art.append(line(arch, 0.8, FAINT, w=0.4))
for x in (138, 196, 284, 342):
    art.append(path([(x - 3, BACK_Y), (x + 3, BACK_Y), (x + 3, 112), (x - 3, 112)], FAINT, 0.35))
    art.append(path([(x - 5, 112), (x + 5, 112), (x + 4, 108), (x - 4, 108)], FAINT, 0.35))
# Masonry courses on the back wall, beside the arches.
for y in (116, 128, 140):
    for x0, x1 in ((110, 134), (346, 370)):
        art.append(line([(x0, y), (x1, y)], 0.4, FAINT, w=0.3))
# --- The stage floor: boards receding to the back wall.
art.append(line([(240 - BACK_HALF, BACK_Y), (240 + BACK_HALF, BACK_Y)], 0.6, DIM, w=0.35))
for k in range(-6, 7):
    f = k / 6
    art.append(line([(240 + f * BACK_HALF, BACK_Y), (240 + f * FRONT_HALF, FRONT_Y)], 0.4, FAINT, w=0.3))
for y in (158, 170, 184):
    t = (y - BACK_Y) / (FRONT_Y - BACK_Y)
    half = BACK_HALF + (FRONT_HALF - BACK_HALF) * t
    art.append(line([(240 - half, y), (240 + half, y)], 0.35, FAINT, w=0.25))
# The apron's edge and the footlights' hoods.
art.append(path([(240 - FRONT_HALF - 6, FRONT_Y), (240 + FRONT_HALF + 6, FRONT_Y), (240 + FRONT_HALF + 6, FRONT_Y + 2.4), (240 - FRONT_HALF - 6, FRONT_Y + 2.4)], LINE, 0.5))
for k in range(9):
    x = 240 - 144 + k * 36
    art.append(path(ellipse(x, FRONT_Y - 0.4, 5, 3.4, 16, 180, 360, False), LINE, 0.5))
# --- The glass: a great pane leaning toward the house, its frame's edges and a few glints.
glass_top, glass_bottom = 92.0, FRONT_Y - 3
glass = [(240 - 120, glass_top), (240 + 120, glass_top), (240 + 166, glass_bottom), (240 - 166, glass_bottom)]
art.append(line([glass[0], glass[1]], 0.5, DIM, w=0.3))
art.append(line([glass[0], glass[3]], 0.5, DIM, w=0.3))
art.append(line([glass[1], glass[2]], 0.5, DIM, w=0.3))
for x0, y0, x1, y1 in ((128, 100, 140, 120), (136, 98, 142, 108), (346, 102, 338, 118), (360, 140, 354, 152)):
    art.append(line([(x0, y0), (x1, y1)], 0.6, DIM, taper=True, w=0.3))
# The ropes the pane hangs by.
for x in (240 - 108, 240 + 108):
    art.append(line([(x, 80), (x + (4 if x < 240 else -4), glass_top)], 0.4, FAINT, w=0.3))
# --- The proscenium: an arch on pilasters, mouldings, a cartouche with a mask.
arch_outer = bezier((20, 70), (36, 34), (444, 34), (460, 70), 32)
arch_inner = bezier((60, 74), (80, 44), (400, 44), (420, 74), 32)
art.append(line(arch_outer, 0.8, LINE, w=0.45))
art.append(line(arch_inner, 0.7, LINE, w=0.45))
# Voussoir joints between the arch's two lines.
for t in (0.1, 0.2, 0.3, 0.4, 0.6, 0.7, 0.8, 0.9):
    o = bezier((20, 70), (36, 34), (444, 34), (460, 70), 100)[round(t * 100)]
    i = bezier((60, 74), (80, 44), (400, 44), (420, 74), 100)[round(t * 100)]
    art.append(line([o, i], 0.35, FAINT, w=0.25))
for side in (False, True):
    pil = [(20, 70), (60, 74), (60, FRONT_Y + 8), (20, FRONT_Y + 8)]
    art.append(path(mirror(pil) if side else pil, LINE, 0.5))
    cap = [(16, 80), (64, 80), (64, 83), (16, 83)]   # the capital's moulding
    art.append(path(mirror(cap) if side else cap, LINE, 0.5))
    base = [(15, FRONT_Y + 2), (65, FRONT_Y + 2), (65, FRONT_Y + 5), (15, FRONT_Y + 5)]
    art.append(path(mirror(base) if side else base, LINE, 0.45))
    for x in (30, 40, 50):   # fluting
        flute = [(x - 0.7, 90), (x + 0.7, 90), (x + 0.7, FRONT_Y), (x - 0.7, FRONT_Y)]
        art.append(path(mirror(flute) if side else flute, FAINT, 0.3))
cart = ellipse(240, 47, 16, 9.5, 28)
art.append(path(cart, LINE, 0.5))
# A tragic mask in the cartouche: face, eyes, downturned mouth.
art.append(path(ellipse(240, 47, 7, 6.6, 20), LINE, 0.5))
for dx in (-3, 3):
    art.append(path(ellipse(240 + dx, 45.2, 1.8, 1.2, 10), LINE, 0.5))
art.append(line(bezier((236.5, 51.4), (238, 49.4), (242, 49.4), (243.5, 51.4), 6), 0.6, LINE, w=0.4))
# Acanthus scrolls either side of the cartouche.
for side in (False, True):
    scroll = bezier((223, 48), (211, 53), (202, 46), (208, 41), 10) + bezier((208, 41), (213, 38), (217, 43), (212, 45), 6)[1:]
    art.append(line(mirror(scroll) if side else scroll, 0.8, DIM, taper=True, w=0.4))
# --- The curtains: a swagged valance and tied-back drapes, with folds.
val = bezier((60, 74), (110, 96), (190, 76), (240, 90), 14)[:-1] + bezier((240, 90), (290, 76), (370, 96), (420, 74), 14)
art.append(line(val, 0.7, LINE, w=0.4))
# The swags' folds: curves following each swag, gathered at its ends.
for lift in (7.0,):
    for x0, x1, xm in ((64, 236, 150), (244, 416, 330)):
        depth = 90 - lift - (8 if xm < 240 else 8)
        fold = bezier((x0, 76 - lift * 0.2), (x0 + 40, depth + 8), (x1 - 40, depth + 8), (x1, 80 - lift * 0.3), 14)
        art.append(line(fold, 0.6, DIM, taper=True, w=0.4))
for side in (False, True):
    edge = bezier((60, 76), (92, 112), (68, 140), (82, 176), 16)
    art.append(line(mirror(edge) if side else edge, 0.6, LINE, w=0.4))
    for k in (1, 3):
        fold = bezier((61 + 4 * k, 78), (62 + 6 * k, 110), (61 + 3 * k, 142), (64 + 3 * k, 192), 12)
        art.append(line(mirror(fold) if side else fold, 0.6, FAINT, taper=True, w=0.4))
    tie = ellipse(84, 176, 4, 2.2, 14)
    art.append(path(mirror(tie) if side else tie, LINE, 0.4))
    tail = [(82, 178), (80, 186), (84, 196)]
    art.append(line(mirror(tail) if side else tail, 0.5, LINE, w=0.35))
# --- The hint in the crypt's doorway (display.cpp shows it when bed asks for
# a sample and none is loaded): "load a sample", "shift +" and the Sample
# key's icon, as Grains shows it; node 18, a black cover, hides it otherwise.
x0, x1, y0, y1 = DOOR
hint = [f'<text transform="matrix(1 0 0 1 {num(x0 + 3)} {num(y0 + 11)})" fill="#CBCBCB" {FONT} font-size="12px">load a sample</text>',
        f'<text transform="matrix(1 0 0 1 {num(x0 + 12)} {num(y1 - 4)})" fill="#9a9a9a" {FONT} font-size="10px">shift +</text>']
KEY, KX, KY = 11.0, x0 + 46, y1 - 13
k = KEY / 472
corner = 24 * k
hint.append(path([(KX + corner, KY), (KX + KEY - corner, KY), (KX + KEY, KY + corner), (KX + KEY, KY + KEY - corner),
                  (KX + KEY - corner, KY + KEY), (KX + corner, KY + KEY), (KX, KY + KEY - corner), (KX, KY + corner)], "#9a9a9a", 0.6))
hint.append(path(ellipse(KX + (248 - 12) * k, KY + (246 - 12) * k, 153 * k, 153 * k, 16), "#9a9a9a", 0.5))
for bx, top, bottom in ((181, 236, 258), (205, 236, 258), (230, 185, 308), (254, 212, 283), (279, 236, 258), (304, 236, 258), (328, 236, 258)):
    X = KX + (bx - 12) * k
    hint.append(path([(X - 0.25, KY + (top - 12) * k), (X + 0.25, KY + (top - 12) * k), (X + 0.25, KY + (bottom - 12) * k), (X - 0.25, KY + (bottom - 12) * k)], "#9a9a9a", 0.3))
cover = f'<path id="{ID}_node_18" d="{outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])}" fill="#000000" stroke="#000000" stroke-width="0.5"/>'

# No legend: the plate's 8 px caption doesn't survive 1:1.
legend = []

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
# Halos (12..15 the apparition's, 19..22 its twin's): ellipses round the upper
# body, drawn at their origin.
halo = ellipse(0, 0, 36, 46, 40)
dynamic += [f'<path id="{ID}_node_{n}" d="{outline(halo)}" fill="none" stroke="#444444" stroke-width="0.5"/>' for n in (12, 13, 14, 15, 19, 20, 21, 22)]
dynamic += [
    # The ghost's twin first, behind it, and its mouth; the ghost; its mouth (paths 1, 0, 2).
    f'<path id="{ID}_node_1" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed)}" fill="none" stroke="#555555" stroke-width="0.6"/>',
    # The twin's mouth (node 23): an oval at its origin, sized to the vowel.
    f'<path id="{ID}_node_23" d="{outline(ellipse(0, 0, 3, 3.5, 24))}" fill="none" stroke="#555555" stroke-width="0.5"/>',
    f'<path id="{ID}_node_0" transform="matrix(1 0 0 1 240 190)" d="{outline(bowed)}" fill="none" stroke="#999999" stroke-width="0.6"/>',
    f'<path id="{ID}_node_2" transform="matrix(1 0 0 1 240 136)" d="{outline(ring)}" fill="none" stroke="#999999" stroke-width="0.5"/>',
]
# Two more apparitions for a second and third held note: the bowed figure, smaller.
small = outline([(x * 0.8, y * 0.8) for x, y in bowed])
dynamic += [f'<path id="{ID}_node_{n}" d="{small}" fill="none" stroke="#555555" stroke-width="0.5"/>' for n in (16, 17)]

palette = []
for i in range(16):
    grey = "#" + f"{i * 17:02x}" * 3
    palette.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="0.8"/>')

out += art + hint + [cover] + legend + dynamic + palette + ["</g>", ""]
(ROOT / "panel.svg.fragment").write_text("\n".join(out))
print(len("\n".join(out)), "bytes,", count[0], "static outlines")
