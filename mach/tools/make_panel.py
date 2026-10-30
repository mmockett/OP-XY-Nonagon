#!/usr/bin/env python3
"""Writes panel.svg.fragment, Mach's screen artwork, and plate.h, the
geometry display.cpp draws from: run it after changing a shape here, and
commit all three.

The whole screen is one object: a photograph after Ernst Mach and Peter
Salcher's 1887 schlieren images of a bullet in flight, its round field a
200 px circle, so its edge frames the left and right as two arcs while its
middle fills the screen. Drawn for the screen at 1:1 (480 x 222), in three
greys and few weights: the projectile (solid) and a crack's shocks
brightest and boldest (grey 15), the field's edge, the listener and the
plate (solid) and a thump's shocks a step down (grey 9 to 10), the wake and
the sound waves dimmer still. No
lines closer than about 3 px, nothing finer than a pixel, no small text. In
the field, under the SDK's two-page header:
  - the projectile, one of five a 19th-century range might fire (one for
    each source): a rifle bullet, a round-nose bullet, a Minie ball, a
    musket ball and a spitzer, each with its own shock stand-off;
  - its bow and tail shocks (the Mach cone) and its turbulent wake, the
    four reshaping outlines (which the device draws unfilled: a shock is a
    ribbon thin enough to read as one line);
  - the sound waves it has sent out, thin rings;
  - an ear trumpet on a post hung up into the field: the listener;
  - with echo, a polished plate under the listener and the shock it reflects;
  - two marks on the line of flight: where the sound heard now left.
Node numbers are display.h's."""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "mach"
FONT = 'font-family="UniversNextPro-Light"'
GREYS = ['#000000', '#0d0d0d', '#141414', '#1c1c1c', '#262626', '#333333', '#404040', '#4d4d4d',
         '#5c5c5c', '#6e6e6e', '#808080', '#999999', '#b3b3b3', '#cccccc', '#e6e6e6', '#ffffff']
G = lambda i: GREYS[i]
LINE = 0.5

# Nodes (display.h names the same numbers).
BOW, TAIL, REFLECTED, WAKE = 0, 1, 2, 3            # the reshaping outlines
HEADER = 4                                        # eight bar steps
RING, RINGS = 12, 12
PROJECTILE, PROJECTILES = 24, 5                   # organ, pad, voice, bell, siren
WIRE, GLOW, PLATE = 29, 30, 31
HEARD, HEARDS = 32, 2
NODES = HEARD + HEARDS                            # 34
POINTS = 64

# The photograph (display.cpp's plate namespace).
CENTRE, RADIUS = (240, 129), 200                  # the schlieren field's circle
BAND = (44, 216)                                  # the field's window, top and bottom
LINE_Y = 108                                      # the line of flight
RING_RADIUS = 10                                  # wave k is authored at 10 (k + 1) px, drawn at up to twice
GLOW_RADIUS = 10


def num(v, digits=4):
    s = f"{round(v, digits):.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def outline(points, digits=3):
    pts = [(num(x, digits), num(y, digits)) for x, y in points]
    assert len(set(pts)) == len(pts), "repeated point"
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def ribbon(points, w0, w1=None):
    """A closed ribbon along a polyline: a fine engraved line."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        w = (w0 if w1 is None else w0 + (w1 - w0) * i / max(1, n - 1)) / 2
        left.append((points[i][0] - dy / length * w, points[i][1] + dx / length * w))
        right.append((points[i][0] + dy / length * w, points[i][1] - dx / length * w))
    return left + right[::-1]


def ring_points(r, n, phase=0.0, wobble=0.0):
    return [(r * math.cos(2 * math.pi * (k + phase) / n) * (1 + wobble * math.sin(5 * k)),
             r * math.sin(2 * math.pi * (k + phase) / n) * (1 + wobble * math.sin(5 * k))) for k in range(n)]


def node(i, d, grey, at, width=LINE, solid=False):
    """A bound outline. Solid ones are authored with a fill, so fill-and-stroke
    paint can change it on the device; the reshaping outlines may not be."""
    return (f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})" d="{d}" '
            f'fill="{G(grey) if solid else "none"}" stroke="{G(grey)}" stroke-width="{num(width)}"/>')


def line(points, grey, w=0.2, width=0.3):
    return f'<path d="{outline(ribbon(points, w), 2)}" fill="none" stroke="{G(grey)}" stroke-width="{num(width)}"/>'


def closed(points, grey, width=0.3, digits=2):
    return f'<path d="{outline(points, digits)}" fill="none" stroke="{G(grey)}" stroke-width="{num(width)}"/>'


def text(x, y, size, grey, words):
    return (f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{G(grey)}" {FONT} '
            f'font-size="{num(size)}px">{words}</text>')


def placeholder(n, r=6.0):
    """A mutable outline's resting shape: a small ring of n distinct points."""
    return ring_points(r, n)


# --- the projectiles ---------------------------------------------------------
# Each points along +x, its origin at its middle; NOSE and TAILS are where its
# shocks stand (pixels ahead of the middle), STANDOFF how far the bow shock
# stands off a blunt nose.

def bullet():
    """A 19th-century rifle bullet, 24 px long and 6 wide (about 11 mm x 27 mm
    at 0.9 px a mm): flat base with a chamfer, two cannelures, tangent ogive.
    Its nose points along +x; the origin is its middle."""
    top = [(-12, 0.0), (-12, 2.4), (-11.5, 2.95)]
    x = -11.5
    for groove in (-8.0, -4.5):
        top += [(groove - 0.7, 3.0), (groove - 0.35, 2.55), (groove + 0.35, 2.55), (groove + 0.7, 3.0)]
    top.append((0.5, 3.0))
    radius = (11.5 ** 2 + 3.0 ** 2) / (2 * 3.0)       # a tangent ogive from x = 0.5 to the tip at 12
    for k in range(1, 12):
        xx = 0.5 + 11.5 * k / 12
        yy = math.sqrt(radius ** 2 - (xx - 0.5) ** 2) - (radius - 3.0)
        top.append((xx, max(yy, 0.15)))
    profile = top[1:] + [(12, 0.0)]
    lower = [(px, -py) for px, py in reversed(profile[:-1])]
    return [(-12, 0.0)] + profile[:-1] + [(12, 0.0)] + lower[:-1] + [(lower[-1][0], lower[-1][1])]



def round_nose():
    """A round-nose bullet, 22 px: flat base, one cannelure, hemispherical nose."""
    top = [(-11, 2.6), (-10.6, 3.0), (-6.7, 3.0), (-6.35, 2.6), (-5.65, 2.6), (-5.3, 3.0), (7.6, 3.0)]
    top += [(7.6 + 3.0 * math.sin(math.pi / 2 * k / 6), 3.0 * math.cos(math.pi / 2 * k / 6)) for k in range(1, 6)]
    lower = [(x, -y) for x, y in reversed(top)]
    return [(-11, 0.0)] + top + [(10.6, 0.0)] + lower


def minie():
    """A Minie ball, 20 px: a blunt cone, three grease grooves, the hollow
    base's skirt flaring a little."""
    top = [(-10, 3.4), (-8.6, 3.2), (-7.9, 2.7), (-7.2, 3.2), (-5.6, 3.15), (-4.9, 2.65), (-4.2, 3.15),
           (-2.6, 3.1), (-1.9, 2.6), (-1.2, 3.1), (1.0, 3.0)]
    top += [(1.0 + 8.4 * k / 6, 3.0 * (1 - (k / 6) ** 1.25) + 0.35 * k / 6) for k in range(1, 6)]
    top += [(9.6, 0.5)]
    lower = [(x, -y) for x, y in reversed(top)]
    # the hollow base: in from the skirt
    base = [(-8.8, -1.6), (-8.4, 0.0), (-8.8, 1.6)]
    return base + top + [(10.0, 0.0)] + lower


def musket_ball():
    """A musket ball, 9 px across, with the stub of its casting sprue."""
    pts = [(4.5 * math.cos(2 * math.pi * k / 28), 4.5 * math.sin(2 * math.pi * k / 28)) for k in range(28)
           if not (12 <= k <= 16)]
    sprue = [(-4.5, 0.9), (-5.2, 0.7), (-5.2, -0.7), (-4.5, -0.9)]
    i = next(j for j, p in enumerate(pts) if p[0] < -4.3 and p[1] < 0) if False else 12
    return pts[:12] + [(-4.4, 1.0)] + sprue[1:3] + [(-4.4, -1.0)] + pts[12:]


def spitzer():
    """A spitzer, 28 px: a long secant ogive to a sharp point and a boat tail."""
    top = [(-14, 1.9), (-12, 2.25), (-8.5, 2.75), (-7.6, 2.75), (-7.25, 2.35), (-6.55, 2.35), (-6.2, 2.75), (-1.0, 2.75)]
    top += [(-1.0 + 15.0 * k / 8, 2.75 * (1 - (k / 8) ** 1.6) + 0.12) for k in range(1, 8)]
    lower = [(x, -y) for x, y in reversed(top)]
    return [(-14, 0.0)] + top + [(14, 0.0)] + lower


# Drawn at twice the sizes above, so each reads at a glance on the device.
SIZE = 2
SHAPES = [[(SIZE * x, SIZE * y) for x, y in shape] for shape in (bullet(), round_nose(), minie(), musket_ball(), spitzer())]
NOSE = [SIZE * v for v in (12, 10.6, 10.0, 4.5, 14)]
TAILS = [SIZE * v for v in (-12, -11, -10, -4.5, -14)]
STANDOFF = [SIZE * v for v in (1.2, 3.0, 2.2, 4.5, 0.5)]

# --- the card and its photograph ---------------------------------------------
HIDDEN = (640, 320)
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

# The photograph's round field: its edge where it shows, a solid 2 px band
# on either side, tapering to a point at each end.
cx, cy = CENTRE
for side in (-1, 1):
    left, right = [], []
    for k in range(33):
        y = BAND[0] - 4 + (BAND[1] - BAND[0] + 8) * k / 32
        x = cx + side * math.sqrt(RADIUS ** 2 - (y - cy) ** 2)
        w = 1.0 * math.sin(math.pi * k / 32) ** 0.35        # half the band's width
        left.append((x - side * w, y))
        right.append((x + side * w, y))
    out.append(f'<path d="{outline(left + right[::-1][1:-1], 2)}" fill="{G(10)}" stroke="{G(10)}" stroke-width="0.4"/>')

# --- the nodes ---------------------------------------------------------------
out.append(node(BOW, outline(placeholder(POINTS)), 15, HIDDEN))
out.append(node(TAIL, outline(placeholder(POINTS)), 12, HIDDEN))
out.append(node(REFLECTED, outline(placeholder(POINTS)), 10, HIDDEN))
out.append(node(WAKE, outline(placeholder(POINTS)), 7, HIDDEN))
for i in range(RINGS):
    out.append(f'<circle id="{ID}_node_{RING + i}" transform="matrix(1 0 0 1 {HIDDEN[0]} {HIDDEN[1]})" cx="0" cy="0" '
               f'r="{RING_RADIUS * (i + 1)}" fill="none" stroke="{G(10)}" stroke-width="{num(LINE)}"/>')
for i, shape in enumerate(SHAPES):
    out.append(node(PROJECTILE + i, outline(shape), 15, (CENTRE[0], LINE_Y) if i == 0 else HIDDEN, solid=True))
# The listener: an ear trumpet on a post, hung up into the field from below
# the card; its mouth (the origin) is where the listener is. The post runs
# 112 px, past the screen's edge from any height.
mouth = [(-5.5 + 11 * k / 6, -0.6 * math.sin(math.pi * k / 6)) for k in range(7)]
horn = [(5.5, 0.0), (4.2, 1.6), (2.4, 3.6), (1.2, 5.8), (0.75, 7.5), (0.75, 112.0),
        (-0.75, 112.0), (-0.75, 7.5), (-1.2, 5.8), (-2.4, 3.6), (-4.2, 1.6)]
out.append(node(WIRE, outline(mouth[:-1] + horn), 10, (CENTRE[0], LINE_Y + 40), solid=True))
out.append(f'<circle id="{ID}_node_{GLOW}" transform="matrix(1 0 0 1 {HIDDEN[0]} {HIDDEN[1]})" cx="0" cy="0" '
           f'r="{GLOW_RADIUS}" fill="none" stroke="{G(14)}" stroke-width="{num(LINE)}"/>')
# The plate: a polished bar seen edge on, 300 px and 4 thick, bevelled.
plate = [(-150, -2.0), (150, -2.0), (152, 0.0), (150, 2.0), (-150, 2.0), (-152, 0.0)]
out.append(node(PLATE, outline(plate), 10, HIDDEN, solid=True))
heard = ring_points(4.0, 24)
for i in range(HEARDS):
    out.append(node(HEARD + i, outline(heard), 13, HIDDEN))

for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" '
               f'stroke-width="{num(LINE)}"/>')
out.append("</g>")
body = "\n".join(out) + "\n"
# The device's rules for every artwork path: one subpath, at most 64 points,
# neighbours at least 1/8 px apart, a closing edge of at least 1/4 px.
for d in re.findall(r' d="([^"]+)"', body):
    assert d.count("M") == 1, d[:60]
    shape = [(float(x), float(y)) for x, y in re.findall(r"(-?[\d.]+),(-?[\d.]+)", d)]
    assert 3 <= len(shape) <= 64, (len(shape), d[:60])
    for i, a in enumerate(shape):
        b = shape[(i + 1) % len(shape)]
        assert abs(a[0] - b[0]) + abs(a[1] - b[1]) >= (0.25 if i == len(shape) - 1 else 0.125), (a, b)
assert len(body) <= 64 * 1024, len(body)
(ROOT / "panel.svg.fragment").write_text(body)

plate_h = ["// Written by tools/make_panel.py: do not edit.",
           "#pragma once",
           "#include <opxy/types.hpp>",
           "",
           "namespace mach {",
           "namespace plate {",
           "// The photograph's round field (pixels) and the card's window it shows through.",
           f"const opxy::i32 centre_x = {CENTRE[0]}, centre_y = {CENTRE[1]}, radius = {RADIUS};",
           f"const opxy::i32 top = {BAND[0]}, bottom = {BAND[1]};",
           "// The line of flight; the rings' authored radii (wave k's is (k + 1) times it) and the glow's.",
           f"const opxy::i32 line_y = {LINE_Y}, ring_radius = {RING_RADIUS}, glow_radius = {GLOW_RADIUS};",
           "// Each projectile's nose and tail (Q10 pixels ahead of its middle) and its bow shock's stand-off.",
           "const opxy::i32 nose[5] = {" + ", ".join(str(round(v * 1024)) for v in NOSE) + "};",
           "const opxy::i32 tail[5] = {" + ", ".join(str(round(v * 1024)) for v in TAILS) + "};",
           "const opxy::i32 standoff[5] = {" + ", ".join(str(round(v * 16)) for v in STANDOFF) + "};   // Q4",
           "}",
           "}",
           ""]
(ROOT / "plate.h").write_text("\n".join(plate_h))
size = (ROOT / "panel.svg.fragment").stat().st_size
print(f"{NODES} nodes; artwork {size} bytes")
