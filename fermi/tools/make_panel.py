#!/usr/bin/env python3
"""Writes panel.svg.fragment, Fermi's screen artwork. Run it after changing a
shape here and commit both.

One object: the chain as a laboratory apparatus. Brass bobs strung on coil
springs between the clamps of two retort stands, on a plank. A stroboscope on
the plank lights the chain once a period of its struck mode, clocked by a
regulator pendulum hung from the left stand: its bob slides up the rod to run
the clock fast, down to run it slow. A felt damper hangs on a lever from the
right stand. A flagged pin presses the bob the seed nudges hardest.

Drawn for the screen at 1:1, 480 x 222, in two weights of line and three
greys: the chain brightest, the apparatus around it a step down, the swing's
ghost faint. Lines are closed ribbons; the bobs are the only fills."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "fermi"
FONT = 'font-family="UniversNextPro-Light"'
CHAIN, FRAME, GHOST_GREY = "#e6e6e6", "#a6a6a6", "#3a3a3a"   # the three greys
BOLD, FINE = 0.55, 0.45                                         # and two weights

# Geometry, shared with display.h (namespace plate).
LEFT_POST, RIGHT_POST, POST_TOP = 60, 420, 56   # the stands' rods
HOOK_L, HOOK_R, ROW_Y = 66, 414, 112            # where the chain hangs, and its rest line
BASE_TOP, BASE_BOTTOM, BASE_L, BASE_R = 186, 198, 36, 444
LAMP_X, LAMP_Y = 240, 174                       # the stroboscope's lens
PIVOT = (420, 74)                               # the damper's lever pivot
PENDULUM = (38, 62)                             # the regulator's knife edge
ROD = 108                                       # its rod's length, px
SLIDE = (20, 100)                               # its bob's travel down the rod: fast to slow
COIL = 32                                       # a coil as authored, px

# Nodes (display.h).
GHOST = 0
HEADER = 4
MASS, MASSES = 12, 32
CONE, STRAIGHT, COILS = 44, 77, 33
ROD_NODE, FLASH, DAMPER, PIN, BOB = 110, 111, 112, 113, 114
NODES = 115
POINTS = 64

# Palette (display.h: shade).
PALETTE = [
    ("#000000", "#000000", 0.5),   # 0 black
    ("none", GHOST_GREY, 0.5),     # 1 the ghost of the swing
    ("none", "#ffffff", 0.7),      # 2 a hard (cubic) coil
    ("#000000", CHAIN, BOLD),      # 3 a bob
    ("none", FRAME, FINE),         # 4 a coil
    ("none", FRAME, FINE),         # 5 the pendulum's rod
    ("none", "#000000", 0.5),      # 6 the flash, out
    ("none", "#7a7a7a", BOLD),     # 7 the flash, fading
    ("none", CHAIN, 0.6),          # 8 the flash
    ("none", FRAME, FINE),         # 9 the damper
    ("none", CHAIN, BOLD),         # 10 the seed's pin
    ("#000000", FRAME, FINE),      # 11 the pendulum's bob
    ("none", FRAME, FINE),         # 12
    ("none", FRAME, FINE),         # 13
    ("none", FRAME, FINE),         # 14
    ("none", FRAME, FINE),         # 15
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


def text(x, y, size, fill, words):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" {FONT} font-size="{num(size)}px">{words}</text>'


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


def line(x0, y0, x1, y1, ink=FRAME, h=0.5, width=FINE):
    return path(outline(ribbon([(x0, y0), (x1, y1)], h), 1), "none", ink, width)


def poly(points, ink=FRAME, h=0.5, width=FINE):
    return path(outline(ribbon(points, h)), "none", ink, width)


def rect(x0, y0, x1, y1, ink=FRAME, width=FINE, fill="none"):
    return path(outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)]), fill, ink, width)


def arc(cx, cy, r, a0, a1, n=12):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / (n - 1)), cy + r * math.sin(a0 + (a1 - a0) * i / (n - 1))) for i in range(n)]


def ring(cx, cy, r, n=40):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


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

# ---- The plank, and the stands: rods on footed bases, each with a boss
# clamp holding a hook at the chain's height.
out.append(rect(BASE_L, BASE_TOP, BASE_R, BASE_BOTTOM))
for x in (LEFT_POST, RIGHT_POST):
    out.append(line(x, POST_TOP, x, BASE_TOP - 4, h=1.0))
    out.append(path(outline([(x - 11, BASE_TOP), (x + 11, BASE_TOP), (x + 8, BASE_TOP - 4), (x - 8, BASE_TOP - 4)]), "none", FRAME, FINE))
    out.append(rect(x - 5, ROW_Y - 5, x + 5, ROW_Y + 5, fill="#000000"))
out.append(line(LEFT_POST + 5, ROW_Y, HOOK_L, ROW_Y))
out.append(line(HOOK_R, ROW_Y, RIGHT_POST - 5, ROW_Y))

# ---- The swing's ghost: four reshaping outlines, a quarter of the chain each.
for q in range(4):
    pts = [(-44 + 88 * i / 31, -0.3) for i in range(32)] + [(44 - 88 * i / 31, 0.3) for i in range(32)]
    cx = HOOK_L + (HOOK_R - HOOK_L) * (q + 0.5) / 4
    out.append(path(outline(pts, 1), "none", GHOST_GREY, 0.5, node=GHOST + q, at=(cx, ROW_Y)))

# ---- The coils. A conical spring (quadratic: it stiffens one way) and a
# straight, tall one (cubic: alike both ways), about the origin, each with a
# straight lead at either end. Their ribbon is a pixel wide, and the leads
# end it square, so its ends stay a fifth of a pixel across at the smallest a
# coil is drawn (6.4 px long, a fifth of its size here) at any turn:
# narrower, the device would merge them into a streak.
def coil(radius_at, turns):
    pts = [(-COIL / 2, 0), (-COIL / 2 + 3, 0)]
    for i in range(1, 2 * turns):
        t = i / (2 * turns)
        x = -COIL / 2 + 3 + (COIL - 6) * t
        pts.append((x, (-1 if i % 2 else 1) * radius_at(t)))
    pts += [(COIL / 2 - 3, 0), (COIL / 2, 0)]
    return outline(ribbon(pts, 1.0), 1)


cone_d = coil(lambda t: 0.8 + 4.4 * t, 3)
straight_d = coil(lambda t: 4.6, 4)
for i in range(COILS):
    out.append(path(cone_d, "none", FRAME, FINE, node=CONE + i, at=(640, 320)))
for i in range(COILS):
    out.append(path(straight_d, "none", "#ffffff", 0.7, node=STRAIGHT + i, at=(640, 320)))

# ---- The bobs: plain rings, filled so the coils' ends hide behind them.
R = 5.6
bob = [(R * math.cos(a), R * math.sin(a)) for a in [2 * math.pi * i / 20 for i in range(20)]]
for i in range(MASSES):
    out.append(path(outline(bob, 1), "#000000", CHAIN, BOLD, node=MASS + i, at=(640, 320)))

# ---- The stroboscope on the plank: a lamp housing and its lens; the flash
# is a ring about the lens.
out.append(rect(LAMP_X - 14, LAMP_Y - 12, LAMP_X + 14, BASE_TOP))
out.append(path(outline(ring(LAMP_X, LAMP_Y, 5, 28)), "none", FRAME, FINE))
out.append(path(outline(ring(0, 0, 8.6, 36)), "none", "#000000", 0.5, node=FLASH, at=(LAMP_X, LAMP_Y)))

# ---- The regulator: a pendulum on a knife edge, on a bracket from the left
# stand's top. Its rod (a node, so it can swing) and its bob (a node) slide.
px, py = PENDULUM
out.append(line(LEFT_POST, POST_TOP + 2, px - 3, POST_TOP + 2))
out.append(path(outline([(px - 4, POST_TOP), (px + 4, POST_TOP), (px + 4, POST_TOP + 3), (px, py), (px - 4, POST_TOP + 3)]), "none", FRAME, FINE))
out.append(path(outline([(-0.5, 0), (0.5, 0), (0.5, ROD), (-0.5, ROD)], 1), "none", FRAME, FINE, node=ROD_NODE, at=PENDULUM))
bob_d = outline([(7.2 * math.cos(a), 4.6 * math.sin(a)) for a in [2 * math.pi * i / 24 for i in range(24)]], 1)
out.append(path(bob_d, "#000000", FRAME, FINE, node=BOB, at=(px, py + SLIDE[1])))

# ---- The damper: a long lever from the right stand to a felt pad over the
# chain's middle, about its pivot; damping lowers the pad onto the chain.
out.append(path(outline(ring(PIVOT[0], PIVOT[1], 3, 20)), "#000000", FRAME, FINE))
lever = [(-3, -0.8), (-168, -0.8), (-168, -3.6), (-186, -3.6), (-186, 4.2), (-168, 4.2), (-168, 0.8), (-3, 0.8)]
out.append(path(outline(lever, 1), "none", FRAME, FINE, node=DAMPER, at=PIVOT))

# ---- The seed's pin: a pin with a round head and a paper flag, its point
# pressing the bob the seed nudges hardest.
head = [(3.4 * math.sin(t), -27 + 3.4 * math.cos(t)) for t in [0.12 + (2 * math.pi - 0.24) * i / 19 for i in range(20)]]
pin = [(0, 0), (0.4, -3), (0.4, -12), (9, -13), (9, -20), (0.4, -21)] + head + [(-0.4, -3)]   # the shaft runs into the head
out.append(path(outline(pin, 1), "none", CHAIN, BOLD, node=PIN, at=(640, 320)))

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
