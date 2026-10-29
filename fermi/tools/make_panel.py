#!/usr/bin/env python3
"""Writes panel.svg.fragment, Fermi's screen artwork. Run it after changing a
shape here and commit both.

One object: the chain as a laboratory apparatus, engraved in greys and fine
line. Brass weights strung on coil springs between the clamps of two
retort stands, on a wooden base. Behind them stands an index board, ruled for
displacement at its ends and carrying a harmonic index along its foot: fixed
graduations at the harmonics and a vernier of pointers, one per partial. A
stroboscope on the base lights the chain once a period of its struck mode,
clocked by a regulator pendulum hung from the left stand: its bob slides up
the rod to run the clock fast, down to run it slow. A felt damper hangs on a
lever from the right stand. A flagged pin presses the bob the seed nudges
hardest. A brass plate on the base names the apparatus.

Lines are closed ribbons; the weights are the only fills (small, so they hide
the board's rules behind them). Drawn for the screen at 1:1, 480 x 222: no
part under about 3 px, no lines closer than 3 px, no lettering under 8 px."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "fermi"
FONT = 'font-family="UniversNextPro-Light"'
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# Geometry, shared with display.h (namespace plate).
LEFT_POST, RIGHT_POST = 30, 450       # the stands' rods
HOOK_L, HOOK_R, ROW_Y = 38, 442, 104  # where the chain hangs, and its rest line
BOARD = (46, 62, 434, 154)            # the index board: left, top, right, bottom
INDEX_L, INDEX_R, INDEX_Y = 54, 426, 144   # the harmonic index: k = 0 .. count
BASE_TOP, BASE_FRONT, BASE_BOTTOM = 180, 186, 204
LAMP_X, LAMP_Y = 240, 168            # the stroboscope's lens
PIVOT = (450, 72)                     # the damper's lever pivot
PENDULUM = (16, 55)                   # the regulator's knife edge
ROD = 112                             # its rod's length, px
SLIDE = (22, 104)                     # its bob's travel down the rod: fast to slow
WEIGHT = 44                           # the spacing a weight is drawn for (8 masses), px
COIL = 32                             # a coil as authored, px

# Nodes (display.h).
GHOST = 0
HEADER = 4
MASS, MASSES = 12, 32
CONE, STRAIGHT, COILS = 44, 77, 33
GRADUATION, POINTER, INDEXES = 110, 126, 16
ROD_NODE, FLASH, DAMPER, PIN, BOB = 142, 143, 144, 145, 146
NODES = 147
POINTS = 64

# Palette (display.h: shade).
PALETTE = [
    ("#000000", "#000000", 0.5),   # 0 black
    ("none", "#3a3a3a", 0.5),      # 1 the ghost of the swing
    ("none", "#ffffff", 0.75),     # 2 a hard (cubic) coil
    ("#000000", "#e6e6e6", 0.5),   # 3 a weight
    ("none", "#8c8c8c", 0.42),     # 4 a coil
    ("none", "#8c8c8c", 0.45),     # 5 a graduation
    ("none", "#9a9a9a", 0.5),      # 6 a pointer, its mode quiet
    ("none", "#cfcfcf", 0.55),     # 7 a pointer, its mode sounding
    ("none", "#ffffff", 0.6),      # 8 a pointer, its mode loud
    ("none", "#bdbdbd", 0.45),     # 9 the pendulum's rod
    ("none", "#1f1f1f", 0.5),      # 10 the flash, out
    ("none", "#5c5c5c", 0.5),      # 11 the flash, fading
    ("none", "#e6e6e6", 0.55),     # 12 the flash
    ("none", "#a6a6a6", 0.45),     # 13 the damper
    ("none", "#e6e6e6", 0.55),     # 14 the seed's pin
    ("none", "#404040", 0.4),      # 15
]


def num(v, places=4):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points, places=2):
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


def line(x0, y0, x1, y1, grey, h=0.35, width=0.3):
    return path(outline(ribbon([(x0, y0), (x1, y1)], h), 1), "none", G(grey), width)


def poly(points, grey, h=0.35, width=0.3):
    return path(outline(ribbon(points, h)), "none", G(grey), width)


def rect(x0, y0, x1, y1, grey, width=0.45, fill="none"):
    return path(outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)]), fill, G(grey), width)


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

# ---- The base: a plank seen a little from above, its top, front and grain.
out.append(path(outline([(14, BASE_TOP), (466, BASE_TOP), (468, BASE_FRONT), (12, BASE_FRONT)]), "none", G(10), 0.45))
out.append(path(outline([(12, BASE_FRONT), (468, BASE_FRONT), (468, BASE_BOTTOM), (12, BASE_BOTTOM)]), "none", G(9), 0.45))
for i, (y, amp, phase, grey) in enumerate(((191.5, 1.0, 0.0, 6), (198.5, 1.1, 1.3, 6))):
    pts = [(16 + 448 * t, y + amp * math.sin(2 * math.pi * (1.3 + 0.4 * i) * t + phase)
            + 0.5 * math.sin(2 * math.pi * 5.1 * t + i)) for t in [k / 27 for k in range(28)]]
    out.append(poly(pts, grey, 0.28, 0.28))
# The felt feet.
for fx in (24, 456):
    out.append(rect(fx - 8, BASE_BOTTOM, fx + 8, BASE_BOTTOM + 3, 6, 0.35))
# The maker's plate, lettered large enough to read.
out.append(rect(334, 188.5, 406, 201.5, 9, 0.4, fill="#000000"))   # solid, so the grain stops at its edge
out.append(text(342, 198.2, 8, G(11), "FPUT 1955"))

# ---- The stands: rods on round feet, each with a boss clamp holding a hook.
for x in (LEFT_POST, RIGHT_POST):
    out.append(path(outline([(x - 1.6, 50), (x + 1.6, 50), (x + 1.6, BASE_TOP + 1), (x - 1.6, BASE_TOP + 1)]), "none", G(11), 0.4))
    out.append(circle(x, 50, 1.6, "none", G(11), 0.4))
    out.append(path(outline([(x - 9, BASE_TOP + 1.5), (x + 9, BASE_TOP + 1.5), (x + 7, BASE_TOP - 2), (x - 7, BASE_TOP - 2)]), "none", G(10), 0.4))
    # The boss: a block round the rod at the chain's height.
    out.append(rect(x - 5, ROW_Y - 5, x + 5, ROW_Y + 5, 12, 0.45))
# The hooks the chain hangs from.
out.append(poly([(LEFT_POST + 5, ROW_Y), (HOOK_L - 1.5, ROW_Y), (HOOK_L, ROW_Y - 1.2), (HOOK_L + 1.2, ROW_Y)], 12, 0.35, 0.4))
out.append(poly([(RIGHT_POST - 5, ROW_Y), (HOOK_R + 1.5, ROW_Y), (HOOK_R, ROW_Y - 1.2), (HOOK_R - 1.2, ROW_Y)], 12, 0.35, 0.4))
# ---- The index board, behind the chain.
x0, y0, x1, y1 = BOARD
out.append(rect(x0, y0, x1, y1, 7, 0.45))
# The rest line, in fine dashes behind the chain.
for i in range(15):
    xa = x0 + 18 + i * 24.4
    out.append(line(xa, ROW_Y, xa + 7, ROW_Y, 6, 0.2, 0.22))
# The harmonic index: a strip along the board's foot; graduations and
# pointers are nodes.
out.append(rect(INDEX_L - 4, INDEX_Y - 5, INDEX_R + 4, INDEX_Y + 5, 6, 0.35))
out.append(line(INDEX_L, INDEX_Y, INDEX_R, INDEX_Y, 6, 0.2, 0.25))
grad_d = outline(ribbon([(0, -6), (0, 0)], 0.3), 1)
for i in range(INDEXES):
    out.append(path(grad_d, "none", G(9), 0.4, node=GRADUATION + i, at=(640, 320)))
pointer_d = outline([(0, 0.4), (1.8, 3.4), (0.35, 3.4), (0.35, 9.5), (-0.35, 9.5), (-0.35, 3.4), (-1.8, 3.4)], 1)
for i in range(INDEXES):
    out.append(path(pointer_d, "none", G(9), 0.4, node=POINTER + i, at=(640, 320)))

# ---- The swing's ghost: four reshaping outlines, a quarter of the chain each.
for q in range(4):
    pts = [(-50 + 100 * i / 31, -0.3) for i in range(32)] + [(50 - 100 * i / 31, 0.3) for i in range(32)]
    cx = HOOK_L + (HOOK_R - HOOK_L) * (q + 0.5) / 4
    out.append(path(outline(pts, 1), "none", PALETTE[1][1], PALETTE[1][2], node=GHOST + q, at=(cx, ROW_Y)))

# ---- The coils. A conical spring (quadratic: it stiffens one way) and a
# straight, close-wound one (cubic: alike both ways), about the origin.
def coil(radius_at, turns):
    pts = [(-COIL / 2, 0)]
    for i in range(1, 2 * turns):
        t = i / (2 * turns)
        x = -COIL / 2 + 3 + (COIL - 6) * t
        pts.append((x, (-1 if i % 2 else 1) * radius_at(t)))
    pts += [(COIL / 2, 0)]
    return outline(ribbon(pts, 0.3), 1)


cone_d = coil(lambda t: 0.8 + 4.4 * t, 3)
straight_d = coil(lambda t: 4.6, 4)
for i in range(COILS):
    out.append(path(cone_d, "none", G(11), 0.42, node=CONE + i, at=(640, 320)))
for i in range(COILS):
    out.append(path(straight_d, "none", G(13), 0.6, node=STRAIGHT + i, at=(640, 320)))

# ---- The weights: turned brass bobs threaded on the chain, plain rings (a
# highlight inside would be under a pixel at the sizes they are drawn).
R = 5.6
bob = [(R * math.cos(a), R * math.sin(a)) for a in [2 * math.pi * i / 20 for i in range(20)]]
for i in range(MASSES):
    out.append(path(outline(bob, 1), "#000000", G(14), 0.45, node=MASS + i, at=(640, 320)))

# ---- The stroboscope on the base: a lamp housing and its lens.
out.append(rect(LAMP_X - 16, LAMP_Y - 12, LAMP_X + 16, BASE_TOP - 0.5, 11, 0.45))
out.append(path(outline(ring(LAMP_X, LAMP_Y + 1, 5, 28)), "none", G(12), 0.45))
out.append(path(outline(ring(0, 0, 8.8, 36)), "none", G(3), 0.5, node=FLASH, at=(LAMP_X, LAMP_Y + 1)))

# ---- The regulator: a pendulum on a knife edge, on a bracket from the left
# stand's top. Its rod (a node, so it can swing) is engraved FAST to SLOW; the
# bob (a node) slides along it.
px, py = PENDULUM
out.append(poly([(LEFT_POST, 50), (px - 3, 50)], 11, 0.5, 0.4))
out.append(path(outline([(px - 3.5, 49), (px + 3.5, 49), (px + 3.5, 52), (px, py), (px - 3.5, 52)]), "none", G(11), 0.4))
rod = [(-0.45, 0), (0.45, 0)]
for i in range(3):   # down the right side, notched at fast, middle and slow
    y = SLIDE[0] + (SLIDE[1] - SLIDE[0]) * i / 2
    w = 3.5
    rod += [(0.45, y - 0.2), (0.45 + w, y - 0.2), (0.45 + w, y + 0.2), (0.45, y + 0.2)]
rod += [(0.45, ROD), (-0.45, ROD)]
out.append(path(outline(rod, 1), "none", G(11), 0.45, node=ROD_NODE, at=PENDULUM))
bob_d = outline([(7.2 * math.cos(a), 4.6 * math.sin(a)) for a in [2 * math.pi * i / 24 for i in range(24)]] +
                [(7.2, 0), (-7.2, 0)], 1)
out.append(path(bob_d, "#000000", G(14), 0.5, node=BOB, at=(px, py + SLIDE[1])))

# ---- The damper: a long lever from the right stand to a felt pad over the
# chain's middle, about its pivot; damping lowers the pad onto the chain.
out.append(circle(PIVOT[0], PIVOT[1], 2.6, "none", G(12), 0.45))
out.append(rect(PIVOT[0] - 3.5, PIVOT[1] - 3.5, PIVOT[0] + 3.5, PIVOT[1] + 3.5, 9, 0.35))
lever = [(0, -0.9), (-178, -0.7), (-178, -3.6), (-196, -3.6), (-196, 4.2), (-178, 4.2), (-178, 0.7), (0, 0.9)]
out.append(path(outline(lever, 1), "none", G(12), 0.5, node=DAMPER, at=PIVOT))
# The seed's pin: a long pin with a round head and a paper flag, its point
# pressing the bob the seed nudges hardest.
head = [(3.4 * math.sin(t), -36.6 + 3.4 * math.cos(t)) for t in [0.1 + (2 * math.pi - 0.2) * i / 19 for i in range(20)]]
pin = [(0, 0), (0.35, -3), (0.35, -22), (9, -23), (9, -30), (0.35, -31), (0.35, -33.2)] + head + [(-0.35, -33.2), (-0.35, -3)]
out.append(path(outline(pin, 1), "none", G(13), 0.5, node=PIN, at=(640, 320)))

# The palette, parked off screen.
for i, (fill, stroke, width) in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="700" cy="0" r="1" fill="{fill if fill != "none" else stroke}" '
               f'stroke="{stroke}" stroke-width="{num(width)}"/>')
out.append("</g>")
(ROOT / "panel.svg.fragment").write_text("\n".join(out) + "\n")
static = sum(1 for s in out if s.startswith(("<path", "<circle")) and "_node_" not in s and "_palette_" not in s)
print("wrote panel.svg.fragment:", static, "static paths")
