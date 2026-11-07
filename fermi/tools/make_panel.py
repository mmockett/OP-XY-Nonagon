#!/usr/bin/env python3
"""Writes panel.svg.fragment, Fermi's screen artwork. Run it after changing a
shape here and commit both.

One object: the chain as a laboratory apparatus. Bobs strung on coil springs
between the clamps of two retort stands, on a plank. A regulator pendulum hangs
from a bracket on the right stand: its bob slides up the rod to run the clock
fast, down to run it slow. A felt damper hangs on a lever from the left stand.
A flagged pin presses the bob the seed nudges hardest.

Drawn in the stock look's grey code, each part in the grey of the encoder that
moves it, left to right in encoder order: the chain, hollow bobs on coiled
springs in one CDCDCD line (encoder 1, energy and dispersion; springs shapes
its coils), the stands dark solids (2, masses, which moves them apart), the
felt mid grey (3, damping), the pendulum and the seed's flag light (4, time
and seed). The plank belongs to no encoder. The chain is four reshaping
outlines redrawn whole every frame; the stands, plank, felt and pendulum bob
are the fills."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "fermi"
FONT = 'font-family="UniversNextPro-Light"'
G1, G2, G3, G4 = "#191919", "#4B4B4B", "#969696", "#CDCDCD"   # the grey code, encoders 1..4
PLANK = "#262626"                                               # no encoder
BOLD, FINE = 0.55, 0.45                                         # two weights of line

# Geometry, shared with display.h (namespace plate).
CENTRE, ROW_Y = 230, 124                        # the chain's middle and rest line
SPAN, SPAN_STEP = 210, 25                       # hook to hook at 8 masses, and a step of masses more
STAND_OUT = 8                                   # a stand's rod outside its hook
POST_TOP, FOOT = 72, 196                        # the rods' tops and the feet's tops
PLANK_Y, PLANK_OUT, PLANK_HALF = 200, 30, 212   # the plank under the feet, past the hooks, each half's length
PENDULUM_OUT, PENDULUM_Y = 34, 72               # the regulator's knife edge, right of the right hook
ROD = 96                                        # its rod's length, px
SLIDE = (24, 90)                                # its bob's travel down the rod: fast to slow
PIVOT_Y = 74                                    # the damper's lever pivot, on the left stand's top
LEVER, PAD_W, PAD_H = 170, 20, 8                # the lever's reach to its felt pad's middle, and the pad

# Nodes (display.h).
CHAIN = 0
HEADER = 4
ROD_NODE, STAND_L, DAMPER, PIN, BOB, STAND_R, PLANK_L, PLANK_R = 12, 13, 14, 15, 16, 17, 18, 19
NODES = 20
POINTS = 64

# Palette (display.h: shade).
PALETTE = [
    ("#000000", "#000000", 0.5),   # 0 black
    ("none", G4, BOLD),            # 1 the chain: hollow bobs on coiled springs
    ("none", G3, FINE),            # 2
    ("none", G3, FINE),            # 3
    ("none", G3, FINE),            # 4
    ("none", G4, FINE),            # 5 the pendulum's rod
    (G2, G2, 0.5),                 # 6 a stand
    (PLANK, PLANK, 0.5),           # 7 the plank
    ("none", G3, FINE),            # 8
    (G3, G3, FINE),                # 9 the felt damper
    ("none", G4, BOLD),            # 10 the seed's pin
    (G4, G4, FINE),                # 11 the pendulum's bob
    ("none", G3, FINE),            # 12
    ("none", G3, FINE),            # 13
    ("none", G3, FINE),            # 14
    ("none", G3, FINE),            # 15
]


def num(v, places=4):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points, places=2):
    """A closed outline, started where its longest edge closes it: the device
    draws a short closing edge as a sliver."""
    n = len(points)
    longest = max(range(n), key=lambda i: math.hypot(points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1]))
    points = points[longest:] + points[:longest]
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


def path(d, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<path{ident}{move} d="{d}" fill="{fill}" stroke="{stroke}"{w}/>'


def circle(cx, cy, r, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}"{w}/>'


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

# ---- The plank, in two overlapping halves that slide apart with the stands
# (masses): each half hangs from its outer end.
out.append(path(outline([(0, 0), (PLANK_HALF, 0), (PLANK_HALF, 5), (0, 5)]), PLANK, PLANK, 0.5, node=PLANK_L,
                at=(CENTRE - SPAN / 2 - PLANK_OUT, PLANK_Y)))
out.append(path(outline([(-PLANK_HALF, 0), (0, 0), (0, 5), (-PLANK_HALF, 5)]), PLANK, PLANK, 0.5, node=PLANK_R,
                at=(CENTRE + SPAN / 2 + PLANK_OUT, PLANK_Y)))

# ---- The stands: each one dark solid about its rod at the chain's height, a
# rod on a foot with a boss clamp and a short lead to its hook. The left one
# carries the damper's pivot boss on its top, the right one the pendulum's
# bracket. Masses slides them apart.
top, foot = POST_TOP - ROW_Y, FOOT - ROW_Y
left_stand = [(-3, top - 1), (3, top - 1), (3, top + 5), (2, top + 5), (2, -4), (4, -4), (4, -0.7), (STAND_OUT, -0.7),
              (STAND_OUT, 0.7), (4, 0.7), (4, 4), (2, 4), (2, foot), (14, foot), (14, foot + 4), (-14, foot + 4),
              (-14, foot), (-2, foot), (-2, 4), (-4, 4), (-4, -4), (-2, -4), (-2, top + 5), (-3, top + 5)]
bracket = PENDULUM_OUT - STAND_OUT          # the knife edge, right of the right rod
right_stand = [(-2, top - 3), (bracket + 6, top - 3), (bracket + 6, top + 1), (2, top + 1), (2, -4), (4, -4), (4, 4),
               (2, 4), (2, foot), (14, foot), (14, foot + 4), (-14, foot + 4), (-14, foot), (-2, foot), (-2, 4), (-4, 4),
               (-4, 0.7), (-STAND_OUT, 0.7), (-STAND_OUT, -0.7), (-4, -0.7), (-4, -4), (-2, -4)]
out.append(path(outline(left_stand, 1), G2, G2, 0.5, node=STAND_L, at=(CENTRE - SPAN / 2 - STAND_OUT, ROW_Y)))
out.append(path(outline(right_stand, 1), G2, G2, 0.5, node=STAND_R, at=(CENTRE + SPAN / 2 + STAND_OUT, ROW_Y)))

# ---- The chain: four reshaping outlines, a quarter of its springs each,
# redrawn whole every frame by display.cpp (a ribbon out over the bobs and the
# coils' turns, back under them). Authored as flat ribbons.
for q in range(4):
    quarter = SPAN / 4
    pts = [(-quarter / 2 + quarter * i / 31, -0.3) for i in range(32)] + [(quarter / 2 - quarter * i / 31, 0.3) for i in range(32)]
    cx = CENTRE - SPAN / 2 + quarter * (q + 0.5)
    out.append(path(outline(pts, 1), "none", G4, BOLD, node=CHAIN + q, at=(cx, ROW_Y)))

# ---- The regulator: a pendulum on a knife edge in the right stand's bracket.
# Its rod (a node, so it can swing) and its bob (a node) slide.
px, py = CENTRE + SPAN / 2 + PENDULUM_OUT, PENDULUM_Y
out.append(path(outline([(-0.5, 0), (0.5, 0), (0.5, ROD), (-0.5, ROD)], 1), "none", G4, FINE, node=ROD_NODE, at=(px, py)))
bob_d = outline([(5 * math.cos(a), 8 * math.sin(a)) for a in [2 * math.pi * i / 24 for i in range(24)]], 1)
out.append(path(bob_d, G4, G4, FINE, node=BOB, at=(px, py + SLIDE[1])))

# ---- The damper: a lever from the left stand's top to a felt pad over the
# chain, about its pivot; the pad hangs from the lever's end, and damping
# lowers it onto the chain.
lever = [(3, -0.6), (LEVER + PAD_W / 2, -0.6), (LEVER + PAD_W / 2, PAD_H - 0.6), (LEVER - PAD_W / 2, PAD_H - 0.6),
         (LEVER - PAD_W / 2, 0.6), (3, 0.6)]
out.append(path(outline(lever, 1), G3, G3, FINE, node=DAMPER, at=(CENTRE - SPAN / 2 - STAND_OUT, PIVOT_Y)))

# ---- The seed's pin: a pin with a round head and a paper flag, its point
# pressing the bob the seed nudges hardest.
head = [(3.4 * math.sin(t), -27 + 3.4 * math.cos(t)) for t in [0.12 + (2 * math.pi - 0.24) * i / 19 for i in range(20)]]
pin = [(0, 0), (0.4, -3), (0.4, -12), (9, -13), (9, -20), (0.4, -21)] + head + [(-0.4, -3)]   # the shaft runs into the head
out.append(path(outline(pin, 1), "none", G4, BOLD, node=PIN, at=(640, 320)))

# The palette, parked off screen.
for i, (fill, stroke, width) in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="700" cy="0" r="1" fill="{fill if fill != "none" else stroke}" '
               f'stroke="{stroke}" stroke-width="{num(width)}"/>')
out.append("</g>")
(ROOT / "panel.svg.fragment").write_text("\n".join(out) + "\n")
static = sum(1 for s in out if s.startswith(("<path", "<circle")) and "_node_" not in s and "_palette_" not in s)
print("wrote panel.svg.fragment:", static, "static paths")


# ---- Each outline as the device draws it. It merges neighbouring points
# within an eighth of a pixel, draws a closing edge under a quarter of a pixel
# as a sliver, and streaks an outline with no area (traced out and back, or
# turning straight back at a point). The static outlines are checked here;
# tests/outlines.h gives tests/screen.cpp the nodes' outlines, which it
# checks as every frame places them.
def points_of(element):
    d = element.split(' d="', 1)[1].split('"', 1)[0]
    pts = []
    for token in d.replace("M", " ").replace("L", " ").replace("Z", " ").split():
        x, y = token.split(",")
        pts.append((float(x), float(y)))
    return pts


def faults(pts):
    """The device's three faults in an outline, px; [] if none."""
    found = []
    n = len(pts)
    for i in range(n - 1):
        if abs(pts[i + 1][0] - pts[i][0]) < 0.125 and abs(pts[i + 1][1] - pts[i][1]) < 0.125:
            found.append(f"points {i} and {i + 1} merge")
            break
    if n > 2 and max(abs(pts[0][0] - pts[-1][0]), abs(pts[0][1] - pts[-1][1])) < 0.25:
        found.append("its closing edge is under a quarter of a pixel")
    area = 0.5 * abs(sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n)))
    if area < 0.05:
        found.append(f"it has no area ({area:.3f} px^2)")
    for i in range(n):
        a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
        u, v = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
        lu, lv = math.hypot(*u), math.hypot(*v)
        if lu and lv and (u[0] * v[0] + u[1] * v[1]) / (lu * lv) < -0.995:
            found.append(f"it turns straight back at point {i}")
            break
    return found


bad = 0
for element in out:
    if element.startswith("<path") and "_node_" not in element and "_palette_" not in element:
        for f in faults(points_of(element)):
            print("static outline:", f, element[:80])
            bad += 1
nodes = {}
for element in out:
    if element.startswith("<path") and "_node_" in element:
        nodes[int(element.split("_node_")[1].split('"')[0])] = points_of(element)
flat, index = [], []
for node in sorted(nodes):
    index.append((node, len(flat) // 2, len(nodes[node])))
    for x, y in nodes[node]:
        flat += [x, y]
header = ["// Written by tools/make_panel.py: every node's outline in panel.svg.fragment, px,",
          "// for tests/screen.cpp's check of the frames as the device draws them.",
          "#pragma once", "namespace outlines {",
          "struct Outline { unsigned node, first, count; };",
          f"const Outline nodes[{len(index)}] = {{" + ", ".join(f"{{{n}, {a}, {c}}}" for n, a, c in index) + "};",
          f"const double points[{len(flat)}] = {{"]
for i in range(0, len(flat), 12):
    header.append("    " + ", ".join(num(v) for v in flat[i:i + 12]) + ",")
header += ["};", "}", ""]
(ROOT / "tests" / "outlines.h").write_text("\n".join(header))
print("wrote tests/outlines.h:", len(index), "node outlines;", bad, "static outlines break the device's rules")
if bad:
    raise SystemExit(1)
