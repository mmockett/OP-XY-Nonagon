#!/usr/bin/env python3
"""Writes panel.svg.fragment, Recur's screen artwork, and layout.h, the node
numbers and positions display.cpp uses. Run it after changing a shape here,
and commit all three.

The screen is a clockwork tin picture puzzle. Each map has its own print, a
bold picture named for it: the cat of Arnold and Avez (1968) for the cat map,
a loaf for the baker's, a nautilus for gold, a silver moon for pell,
scissors for the shear and a honeycomb for hex. The tiles are plain square
outlines shared by every print; a print is cut on the finest grid's cells,
one node for each piece of a stroke in a cell, and a piece rides on whichever
tile holds its cell, inside that tile's square. Every node is one closed
subpath; a stroke is a thin closed ribbon, out along one side and back along
the other.

Drawn for the screen at 1:1 (480 x 222): no text under 8 px, no lines closer
than 3 px, no dots, and strokes a good pixel wide."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "recur"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'

TRAY_X, TRAY_Y, TRAY = 172, 54, 136          # the print, 136 px square, centred
TIN = (60, 46, 420, 216)
BEZEL = 3                                    # the tray's bezel, px outside the print
PENDULUM = (114, 188)                        # the escapement's pendulum pivot (slice)
ROD = 112                                    # its rod
WEIGHTS = [16, 34, 52, 70, 88, 106]          # its weight's distance up the rod: hiss, 1/64 .. 1/4
COMB_X, COMB_Y = 338, 58                     # the music-box comb
TOOTH_NOTES = [0, 1, 3, 4, 7, 12, 19]        # its teeth: the semitones chrd's readers play
TOOTH_PITCH = 9                              # px between teeth
LEVER = (365, 188)                           # the rate lever's pivot
RULE_X0, RULE_X1, RULE_Y = 84, 396, 205      # the tape's slot
MAIN, PART = ("#9a9a9a", 0.8), ("#6a6a6a", 0.6)   # the two inks of the fixed artwork: the tin and tray, the instruments' parts
HALF = 0.5                                   # a stroke's half width, px
GAP = 1.6                                    # a small tile's inset from its cell, px
PINS, BARS = 16, 20                          # depth's pins, the tape's bars
SMALL_STROKE, LARGE_STROKE = 0.6, 0.9        # the tile squares' strokes, px (the small ones double on the 4 grid)


def fit(bits):
    """How far a tile's pieces shrink about its centre to sit inside its square, Q10:
    clear of the gutter, the square's stroke and their own, with 0.1 px to spare."""
    half = TRAY / (1 << bits) / 2
    inset, square_half = {1: (2 * GAP, LARGE_STROKE / 2), 2: (2 * GAP, SMALL_STROKE), 3: (GAP, SMALL_STROKE / 2)}[bits]
    return int((half - inset - square_half - 0.35 - 0.1) / (half + 0.05) * 1024)
MAP_NAMES = ["cat", "baker", "gold", "pell", "shear", "hex"]


def num(v, places=2):
    return f"{v:.{places}f}".rstrip("0").rstrip(".")


def outline(points, places=2):
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def node(i, d, stroke="#ffffff", width=1, fill="none", at=None):
    move = f' transform="matrix(1 0 0 1 {num(at[0], 4)} {num(at[1], 4)})"' if at else ""
    return f'<path id="{ID}_node_{i}"{move} d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'


def plain(d, stroke="#5a5a5a", width=0.5, fill="none"):
    if not isinstance(d, str):
        d = outline(d)
    return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'


def circle(cx, cy, r, stroke="#5a5a5a", width=0.5, ident=None, fill="none"):
    name = f' id="{ID}_node_{ident}"' if ident is not None else ""
    return f'<circle{name} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'


def label(x, y, text, size=7, ink="#6a6a6a"):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} font-size="{size}px">{text}</text>'


def ribbon(points, half=HALF):
    """A polyline as a thin closed ribbon: out along one side, back along the other."""
    out, back = [], []
    for i, p in enumerate(points):
        a, b = points[max(i - 1, 0)], points[min(i + 1, len(points) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        n = (-dy / length, dx / length)
        out.append((p[0] + n[0] * half, p[1] + n[1] * half))
        back.append((p[0] - n[0] * half, p[1] - n[1] * half))
    return out + back[::-1]


def rounded(x0, y0, x1, y1, r, n=4):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return outline(pts)


# --- The prints (1024 units a side, y up), as centre lines ------------------------

def arc(cx, cy, rx, ry, a0, a1, n, tilt=0.0):
    pts = []
    for i in range(n):
        a = math.radians(a0 + (a1 - a0) * i / (n - 1))
        x, y = rx * math.cos(a), ry * math.sin(a)
        t = math.radians(tilt)
        pts.append((cx + x * math.cos(t) - y * math.sin(t), cy + x * math.sin(t) + y * math.cos(t)))
    return pts


def ring(cx, cy, rx, ry, n, tilt=0.0, start=0.0):
    return arc(cx, cy, rx, ry, start, start + 360 * (n - 1) / n, n, tilt)


def bend(a, b, n, sag):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    return [(a[0] + dx * (i + 1) / (n + 1) + nx * sag * 4 * (i + 1) / (n + 1) * (1 - (i + 1) / (n + 1)),
             a[1] + dy * (i + 1) / (n + 1) + ny * sag * 4 * (i + 1) / (n + 1) * (1 - (i + 1) / (n + 1))) for i in range(n)]


def scaled(pts, k, centre=(512, 512)):
    return [(centre[0] + (x - 512) * k, centre[1] + (y - 512) * k) for x, y in pts]


def cat():
    jaw = arc(512, 470, 322, 300, 180, 360, 15)
    right_cheek, left_cheek = jaw[-1], jaw[0]
    r_base, r_tip, r_inner = (806, 656), (770, 912), (634, 752)
    l_base, l_tip, l_inner = (1024 - 806, 656), (1024 - 770, 912), (1024 - 634, 752)
    head = jaw[:]
    head += bend(right_cheek, r_base, 1, -14)
    head += [r_base, r_tip, r_inner]
    head += bend(r_inner, l_inner, 3, -26)
    head += [l_inner, l_tip, l_base]
    head += bend(l_base, left_cheek, 1, -14)

    def almond(cx, tilt):
        pts = []
        for i in range(12):
            r = math.radians(-90 + 360 * i / 12)
            x, y = math.cos(r) * 84, math.sin(r) * 50 * (1 - 0.25 * abs(math.cos(r)) ** 3)
            t = math.radians(tilt)
            pts.append((cx + x * math.cos(t) - y * math.sin(t), 520 + x * math.sin(t) + y * math.cos(t)))
        return pts
    nose = [(512, 370), (452, 440), (572, 440)]
    k = CAT_SIZE
    head, eyes = scaled(head, k), [scaled(almond(366, 10), k), scaled(almond(658, -10), k)]
    return [head] + eyes + [scaled(nose, k)], []


def loaf():
    # A tin loaf, side on: straight sides from the tin, a crown risen over
    # them, two slashes across the crown.
    crust = [(330, 250), (694, 250), (694, 470)] + arc(512, 520, 270, 230, -10, 190, 13) + [(330, 470)]
    cuts = [[(448, 570), (500, 650)], [(576, 570), (628, 650)]]
    return [crust], cuts


def nautilus():
    # A golden spiral: its radius grows by the golden ratio each quarter turn.
    b = math.log((1 + 5 ** 0.5) / 2) / (math.pi / 2)
    centre, start, end_t = (530, 500), 19.0, 3.3 * math.pi

    def at(t):
        r = start * math.exp(b * t)
        return (centre[0] + r * math.cos(t + 2.6), centre[1] + r * math.sin(t + 2.6))
    spiral = [at(end_t * i / 27) for i in range(28)]
    mouth = [spiral[-1], at(end_t - 2 * math.pi)]
    return [], [spiral, mouth]


def moon():
    # A silver moon (pell's ratio is the silver one, as gold's is the golden):
    # a crescent opening to the right, and a five-pointed star in its arms.
    (ox, oy), outer = MOON_OUTER
    (ix, iy), inner = MOON_INNER
    # Where the two circles cross: the crescent's horns.
    dx, dy = ix - ox, iy - oy
    d = math.hypot(dx, dy)
    a = (outer ** 2 - inner ** 2 + d ** 2) / (2 * d)
    h = (outer ** 2 - a ** 2) ** 0.5
    mx, my = ox + a * dx / d, oy + a * dy / d
    horns = [(mx - h * dy / d, my + h * dx / d), (mx + h * dy / d, my - h * dx / d)]
    angle = lambda c, p: math.degrees(math.atan2(p[1] - c[1], p[0] - c[0]))
    o0, o1 = angle((ox, oy), horns[0]), angle((ox, oy), horns[1])
    i0, i1 = angle((ix, iy), horns[1]), angle((ix, iy), horns[0])
    if o1 < o0: o1 += 360
    if i1 > i0: i1 -= 360
    crescent = arc(ox, oy, outer, outer, o0, o1, 15)[:-1] + arc(ix, iy, inner, inner, i0, i1, 11)[:-1]
    sx, sy, r = MOON_STAR
    star = [(sx + (r if i % 2 == 0 else r * 0.42) * math.sin(math.radians(36 * i)),
             sy + (r if i % 2 == 0 else r * 0.42) * math.cos(math.radians(36 * i))) for i in range(10)]
    return [crescent, star], []


MOON_OUTER, MOON_INNER, MOON_STAR = ((448, 512), 256), ((578, 551), 220), (704, 570, 66)   # the star fills one cell, so it stays whole
def scissors():
    # Two blades crossed at the pivot, each running back to its ring.
    rings = [ring(370, 280, 80, 80, 12), ring(654, 280, 80, 80, 12)]
    blades = [[(420, 350), (770, 820)], [(604, 350), (254, 820)]]
    return rings, blades


def honeycomb():
    # A honeycomb cell: a hexagon, and the walls of its neighbours leaving each corner.
    # Centred on a cell, so each corner and its wall's stub fall inside one cell.
    def at(r, i):
        return (576 + r * math.cos(math.radians(60 * i)), 576 + r * math.sin(math.radians(60 * i)))
    # The stubs are strokes of their own: traced out and back inside the
    # hexagon's outline they would turn straight back, which the OP-XY draws
    # as a streak.
    return [[at(256, i) for i in range(6)]], [[at(256, i), at(316, i)] for i in range(6)]


CAT_SIZE = 0.72
PRINTS = [cat(), loaf(), nautilus(), moon(), scissors(), honeycomb()]


def to_screen(p):
    return (TRAY_X + p[0] * TRAY / 1024, TRAY_Y + TRAY - p[1] * TRAY / 1024)


# --- Cutting a print on the cells ---------------------------------------------------

def clip_segment(p, q, box):
    """Liang-Barsky: the part of p..q inside box, as parameters t0, t1, or None."""
    x0, y0, x1, y1 = box
    t0, t1 = 0.0, 1.0
    dx, dy = q[0] - p[0], q[1] - p[1]
    for d, e in ((-dx, p[0] - x0), (dx, x1 - p[0]), (-dy, p[1] - y0), (dy, y1 - p[1])):
        if d == 0:
            if e < 0:
                return None
            continue
        t = e / d
        if d < 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t0 > t1:
            return None
    return t0, t1


def pieces(line, box, loop):
    """The parts of a polyline inside box: (points, closed)."""
    pts = line[:]
    inside = lambda p: box[0] <= p[0] <= box[2] and box[1] <= p[1] <= box[3]
    if loop:
        if all(inside(p) for p in pts):
            return [(pts, True)]
        k = next((i for i, p in enumerate(pts) if not inside(p)), 0)
        pts = pts[k:] + pts[:k + 1]
    out, current = [], None
    for i in range(len(pts) - 1):
        c = clip_segment(pts[i], pts[i + 1], box)
        if c is None or c[1] - c[0] < 1e-9:
            if current:
                out.append(current); current = None
            continue
        p, q = pts[i], pts[i + 1]
        a = (p[0] + (q[0] - p[0]) * c[0], p[1] + (q[1] - p[1]) * c[0])
        z = (p[0] + (q[0] - p[0]) * c[1], p[1] + (q[1] - p[1]) * c[1])
        if current is None:
            current = [a, z]
        else:
            current.append(z)
        if c[1] < 1:
            out.append(current); current = None
    if current:
        out.append(current)
    return [(o, False) for o in out]


def length(pts):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:]))


def distinct(points, places=1, apart=0.3):
    """Drops points within apart px of the one before, as written, and at the end
    of the one it closes on. The OP-XY merges points within an eighth of a pixel
    as it draws, and a piece is drawn at 0.72 of its size at the least."""
    out = []
    for p in points:
        q = (round(p[0], places), round(p[1], places))
        if not out or math.hypot(q[0] - out[-1][0], q[1] - out[-1][1]) >= apart:
            out.append(q)
    while len(out) > 2 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < apart:
        out.pop()
    return out


# --- The artwork -------------------------------------------------------------------
out = [f'<g id="{ID}_panel">',
       '<path d="M0,0 L480,0 L480,220 L0,220 Z" fill="#000000"/>',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = []
for c in range(4):
    texts.append((5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10))
for c in range(4):
    texts.append((65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20))
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
               f'font-size="{size}px">-</text>')

# The tin and the tray's bezel, in the main ink; nothing else on the body.
x0, y0, x1, y1 = TIN
out.append(plain(rounded(x0, y0, x1, y1, 7), *MAIN))
out.append(plain(rect(TRAY_X - BEZEL, TRAY_Y - BEZEL, TRAY_X + TRAY + BEZEL, TRAY_Y + TRAY + BEZEL), *MAIN))

# The escapement: a pendulum standing on its pivot, a notch for each of its
# weight's places; the higher the weight, the longer the slice.
px, py = PENDULUM
out.append(plain(outline([(px - 10, py + 6), (px + 10, py + 6), (px + 4, py - 2), (px - 4, py - 2)]), *PART))
for d in WEIGHTS:
    out.append(plain(ribbon([(px + 10, py - d), (px + 16, py - d)], 0.5), *PART))

# The music-box comb's base; its teeth are nodes (chrd plucks them).
out.append(plain(rect(COMB_X - 5, COMB_Y - 5, COMB_X + (len(TOOTH_NOTES) - 1) * TOOTH_PITCH + 5, COMB_Y), *PART))

# The rate lever's quadrant: an arc and eight notches from off to the fastest.
lx, ly = LEVER
for i in range(8):
    a = math.radians(-60 + 120 * i / 7)
    out.append(plain(ribbon([(lx + math.sin(a) * 41, ly - math.cos(a) * 41), (lx + math.sin(a) * 47, ly - math.cos(a) * 47)], 0.5), *PART))
out.append(plain(ribbon([(lx + math.sin(math.radians(a)) * 38, ly - math.cos(math.radians(a)) * 38) for a in range(-64, 65, 8)], 0.3), *PART))
out.append(circle(lx, ly, 3, *PART))

# The tape: the whole sample as a strip of peaks in a slot along the tin's foot
# (its bars are nodes), the window that pos picks lit and held in the caliper.
out.append(plain(rounded(RULE_X0 - 3, RULE_Y - 7, RULE_X1 + 3, RULE_Y + 7, 2.5), *PART))

# With no sample: a cover over the stage (shown then), how to load one, and
# a cover over that (shown once a sample is loaded). Drawn before every other
# node, so a loaded puzzle draws over the hint's cover.
n = 8
hint = [label(177, 112, "load a sample", 20, "#CBCBCB"), label(204, 146, "shift +", 14, "#7D7D7D")]
KEY_SIZE, KEY_X, KEY_Y = 22, 254, 130
KS = KEY_SIZE / 472
def key_xy(x, y):
    return KEY_X + (x - 12) * KS, KEY_Y + (y - 12) * KS
corner, kx1, ky1 = 24 * KS, KEY_X + KEY_SIZE, KEY_Y + KEY_SIZE
hint.append(plain(outline([(KEY_X + corner, KEY_Y), (kx1 - corner, KEY_Y), (kx1, KEY_Y + corner), (kx1, ky1 - corner), (kx1 - corner, ky1),
                           (KEY_X + corner, ky1), (KEY_X, ky1 - corner), (KEY_X, KEY_Y + corner)]), "#7D7D7D", 0.8))
hcx, hcy = key_xy(248, 246)
hint.append(circle(hcx, hcy, 153 * KS, "#7D7D7D", 0.6))
for bx_, top, bottom in ((181, 236, 258), (205, 236, 258), (230, 185, 308), (254, 212, 283), (279, 236, 258), (304, 236, 258), (328, 236, 258)):
    bx, by0 = key_xy(bx_, top)
    _, by1 = key_xy(bx_, bottom)
    hint.append(plain(rect(bx - 0.35, by0, bx + 0.35, by1), "#7D7D7D", 0.25, "#7D7D7D"))

# Nodes. The tiles: 64 small squares, a cell each (the first 16 serve the
# 4 grid at twice the size), and the 2 grid's 4 large ones. All are authored
# at the origin and placed by display.cpp.
tile_nodes = []
small_first = n
cell = TRAY / 8
for i in range(64):
    h = cell / 2 - GAP
    tile_nodes.append(node(n, rect(-h, -h, h, h), "#cfcfcf", SMALL_STROKE))
    n += 1
large_first = n
for i in range(4):
    h = TRAY / 4 - 2 * GAP   # the 2 grid's gutter: the 4 grid's, as its squares are the small ones doubled
    tile_nodes.append(node(n, rect(-h, -h, h, h), "#cfcfcf", LARGE_STROKE))
    n += 1
# The prints, cut: a node for each piece of a stroke in a cell, relative to
# the cell's centre. A stroke stops half its width short of the cell's edge,
# so no piece reaches past its cell; display.cpp then shrinks each tile's
# pieces about the tile's centre by the grid's fit (below), which brings
# them inside the tile's square, its gutter and its stroke, while pieces in
# one tile still meet.
print_first, print_count, piece_cells = [], [], []
for loops, lines in PRINTS:
    print_first.append(n)
    strokes = [([to_screen(p) for p in s], True) for s in loops] + [([to_screen(p) for p in s], False) for s in lines]
    for c in range(64):
        bx, by = TRAY_X + (c % 8) * cell, TRAY_Y + (c // 8) * cell
        box = (bx + HALF, by + HALF, bx + cell - HALF, by + cell - HALF)
        centre = (bx + cell / 2, by + cell / 2)
        for pts, loop in strokes:
            for piece, closed in pieces(pts, box, loop):
                if not closed and length(piece) < 1.5:
                    continue
                shape = piece if closed else ribbon(piece)
                rel = [(x - centre[0], y - centre[1]) for x, y in shape]
                tile_nodes.append(node(n, outline(distinct(rel), 1), "#cfcfcf", 0.6))
                piece_cells.append(c)
                n += 1
    print_count.append(n - print_first[-1])
# Pins: depth's, a large one at the corner of each tile it holds still on the
# 2 and 4 grids (on the 8 grid the held tiles are dark without).
pin_first = n
for i in range(PINS):
    tile_nodes.append(f'<circle id="{ID}_node_{n}" cx="0" cy="0" r="3.2" fill="none" stroke="#bdbdbd" stroke-width="0.8"/>')
    n += 1
# The tape's bars: one for each of the overview's peaks, placed and sized by
# display.cpp (so authored at the origin).
bar_first = n
for i in range(BARS):
    tile_nodes.append(node(n, outline(ribbon([(0, -5.4), (0, 5.4)], 1.5), 1), "#cfcfcf", 0.5))
    n += 1
# The rate lever.
lever = n
tile_nodes.append(node(n, outline([(-2.4, 1.5), (-1, -3), (-0.8, -33), (-2.2, -35), (0, -42), (2.2, -35), (0.8, -33), (1, -3), (2.4, 1.5), (0, 4)]),
                       "#e6e6e6", 0.6, at=LEVER))
n += 1
# The comb's teeth, longest lowest; chrd plucks its readers'.
teeth_first = n
for i in range(len(TOOTH_NOTES)):
    x = COMB_X + i * TOOTH_PITCH
    tile_nodes.append(node(n, outline(ribbon([(0, 0), (0, 34 - i * 2.6)], 1.6)), "#6a6a6a", 0.6, at=(x, COMB_Y)))
    n += 1
# The pendulum's rod and its sliding weight, both standing up from the pivot.
rod = n
tile_nodes.append(node(n, outline([(-0.8, 0), (-0.8, -ROD + 2), (0, -ROD), (0.8, -ROD + 2), (0.8, 0)]), "#cfcfcf", 0.6, at=PENDULUM))
n += 1
weight = n
tile_nodes.append(node(n, outline([(-7, -3.5), (7, -3.5), (5, 3.5), (-5, 3.5)]), "#e6e6e6", 0.8, at=(PENDULUM[0], PENDULUM[1] - WEIGHTS[3])))
n += 1
# The caliper's jaws on the tape: the window's start and end, standing tall.
jaw_first = n
tile_nodes.append(node(n, outline([(0, -10), (3.5, -10), (3.5, -8.8), (1.2, -8.8), (1.2, 7), (3.5, 7), (3.5, 8.2), (0, 8.2)]), "#e6e6e6", 0.6,
                       at=(RULE_X0, RULE_Y)))
n += 1
tile_nodes.append(node(n, outline([(0, -10), (-3.5, -10), (-3.5, -8.8), (-1.2, -8.8), (-1.2, 7), (-3.5, 7), (-3.5, 8.2), (0, 8.2)]), "#e6e6e6", 0.6,
                       at=(RULE_X0 + 6, RULE_Y)))
n += 1
cover_stage = n
cover_hint = n + 1
n += 2
out.append(node(cover_stage, rect(0, 39, 480, 220), "#000000", 0.5, "#000000", at=(0, 0)))
out += hint
out.append(node(cover_hint, rect(TRAY_X + 1, 88, TRAY_X + TRAY - 1, 160), "#000000", 0.5, "#000000", at=(0, 0)))
out += tile_nodes
node_count = n
for i in range(16):
    grey = f"#{i * 17:02x}{i * 17:02x}{i * 17:02x}"
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="1"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if ID == "recur" else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")

if ID == "recur":
    def row(values):
        return ", ".join(map(str, values))
    lines = ["#pragma once", "// Generated by tools/make_panel.py; edit the generator, not this file.",
             "#include <opxy/types.hpp>", "", "namespace recur {",
             "// The screen's nodes: the header's bar (0..7), the tiles' squares, each map's",
             "// print in pieces, depth's pins, the tape's bars, then the moving parts.",
             f"const opxy::u32 small_first = {small_first}, large_first = {large_first};",
             "// The share of its size a tile's print pieces take, Q10, by grid (2, 4, 8): inside its square.",
             f"const opxy::i32 tile_fit[3] = {{{fit(1)}, {fit(2)}, {fit(3)}}};",
             f"const opxy::u32 print_first[6] = {{{row(print_first)}}}, print_count[6] = {{{row(print_count)}}};",
             f"const opxy::u32 pin_first = {pin_first}, pin_count = {PINS}, bar_first = {bar_first}, bar_count = {BARS},",
             f"                lever_node = {lever}, teeth_first = {teeth_first}, teeth = {len(TOOTH_NOTES)},",
             f"                rod_node = {rod}, weight_node = {weight}, jaw_first = {jaw_first},",
             f"                cover_stage = {cover_stage}, cover_hint = {cover_hint}, node_total = {node_count};",
             "// The cell (of the 8 grid) each print piece sits in, from print_first[0] on.",
             "const opxy::u8 piece_cell[" + str(len(piece_cells)) + "] = {" + row(piece_cells) + "};",
             "// The semitone each comb tooth stands for.",
             "const opxy::u8 tooth_notes[" + str(len(TOOTH_NOTES)) + "] = {" + row(TOOTH_NOTES) + "};",
             "// Where things sit, px: the print's corner and side, the lever, the comb, the pendulum, the rule.",
             f"const opxy::i32 tray_x = {TRAY_X}, tray_y = {TRAY_Y}, tray_side = {TRAY};",
             f"const opxy::i32 lever_x = {LEVER[0]}, lever_y = {LEVER[1]};",
             f"const opxy::i32 comb_x = {COMB_X}, comb_pitch = {TOOTH_PITCH}, comb_y = {COMB_Y};",
             f"const opxy::i32 pendulum_x = {PENDULUM[0]}, pendulum_y = {PENDULUM[1]};",
             "// The weight's distance up the rod for each slice, px.",
             "const opxy::i32 weight_heights[6] = {" + row(WEIGHTS) + "};",
             f"const opxy::i32 rule_x0 = {RULE_X0}, rule_x1 = {RULE_X1}, rule_y = {RULE_Y};",
             "}", ""]
    ROOT.joinpath("layout.h").write_text("\n".join(lines))
print("wrote", target, "nodes", node_count, "bytes", len("\n".join(out)), "pieces", print_count)
