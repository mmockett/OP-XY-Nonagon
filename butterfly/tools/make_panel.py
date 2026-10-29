#!/usr/bin/env python3
"""Writes panel.svg.fragment, Butterfly's screen artwork, and plate.h, the
wing's geometry display.cpp draws from, and the node count in
definition.json. Run it after changing a shape here, and commit all three.
`--png out.png` also renders the artwork through headless Chrome.

Butterfly is Moth's neighbour in the cabinet drawer: a pinned swallowtail,
wings spread, centred where Moth is (240, 124), at Moth's scale, drawn in
Moth's line and greys, with its own specimen label.

A wing is two zones. Its breadth, from the body out to the submarginal band
(0.2 to 0.64 of the margin's reach), is the chain, from its middle site to
its end: the lit band (an outline) lies at the struck site, and the
eyespots, four a wing, are the localized states. The forewing's veins are a
node of their own, so tilt can sweep the forewing back about the root; glow
draws a tail out of the hindwing, so the silhouette has none. Outside it the
margin, one outline of the root's two points and 62 margin points, is the
spectrum, read from the forewing's leading edge (the top) round to the
hindwing's tail (the bottom), each band a lobe and each gap a notch.
display.cpp keeps the margin outside the inner zone, so the static veins
never cross it. Each wing's nodes scale together about its root (spread,
wingbeat).

Three weights, drawn for the screen at 1:1: the margin, white and 2 px, the
boldest; the lit band 1 to 1.6 px, grey to near white as the strike is
harder; the veins, eyespot rings, antennae and body a step down. The body is
static (head, thorax, abdomen, the pin through the thorax); the clubbed
antennae are nodes. Every node is one closed outline of distinct points (or
a circle); lines are thin closed ribbons."""
import json
import math
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "butterfly"
FONT = 'font-family="UniversNextPro-Light"'
ARGS = sys.argv[1:]
PNG = pathlib.Path(ARGS[ARGS.index('--png') + 1]).resolve() if '--png' in ARGS else None

CX, CY = 240, 124
MARGIN = 62               # margin points per wing; with the two root points, an outline's 64
POINTS = 64
VEINS = [5, 15, 25, 38, 47, 56]   # the margin points the veins run toward: three a half wing, one to the tail
INNER = 0.66              # the inner zone's edge: the veins end here, and the margin never comes nearer
SITE_NEAR, SITE_FAR = 0.2, 0.64   # the chain's middle site and its end, as shares of the margin's reach
EYES_A_WING = 4
PIVOT = (-3, 0)           # each wing's root, from the butterfly's centre: its nodes scale about it
MIDDLE = 108              # each outline's node, from the centre: its points stay within 64 px at the widest span

# Nodes (display.h names the same numbers).
PATHS = 0                 # 0, 1: margins (left, right); 2, 3: lit bands
HEADER = 4                # eight bar nodes
VEIN = 12                 # four: each wing's veins, forewing then hindwing, each one comb-shaped outline
EYE = 16                  # eight eyespot rings: four a wing, left then right
ANTENNA = 24              # two: left, right
NODE_COUNT = ANTENNA + 2

WIDTHS = [0.5] * 8 + [0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 1.0]   # each grey's line: 0.5 to 0.8 a step, the margin's 1
GREYS = ['#000000', '#0d0d0d', '#141414', '#1c1c1c', '#262626', '#333333', '#404040', '#4d4d4d',
         '#5c5c5c', '#6e6e6e', '#808080', '#999999', '#b3b3b3', '#cccccc', '#e6e6e6', '#ffffff']


def num(v, digits=2):
    s = f"{v:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def outline(points, digits=2):
    pts = []
    for x, y in points:
        p = (num(x, digits), num(y, digits))
        if not pts or pts[-1] != p:
            pts.append(p)
    if pts[0] == pts[-1]:
        pts.pop()
    assert len(set(pts)) == len(pts), "repeated point"
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def node(i, d, fill, stroke=None, width=0.25, at=None):
    transform = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    return (f'<path id="{ID}_node_{i}"{transform} d="{d}" fill="{fill}" stroke="{stroke or fill}" '
            f'stroke-width="{num(width)}"/>')


def art(d, fill, stroke=None, width=0.25):
    return f'<path d="{d}" fill="{fill}" stroke="{stroke or fill}" stroke-width="{num(width)}"/>'


def ribbon(points, w0, w1=None):
    """A closed ribbon along a polyline, as Moth draws a line."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        w = (w0 if w1 is None else w0 + (w1 - w0) * i / max(1, n - 1)) / 2
        left.append((points[i][0] - dy / length * w, points[i][1] + dx / length * w))
        right.append((points[i][0] + dy / length * w, points[i][1] - dx / length * w))
    return left + right[::-1]


def ellipse(cx, cy, rx, ry, n):
    return [(cx + rx * math.cos(2 * math.pi * s / n), cy + ry * math.sin(2 * math.pi * s / n)) for s in range(n)]


# The left wing's resting silhouette, round its root (Q0 px from the
# butterfly's centre): a swallowtail's broad forewing, apex up and out, and
# under it the hindwing, scalloped and drawn out into its tail. A Catmull-Rom
# curve through these points, sampled evenly along the margin.
P = [(-4, -12), (-22, -38), (-58, -66), (-102, -82), (-142, -86), (-160, -80), (-160, -64), (-148, -42),
     (-134, -22), (-122, -8), (-126, 8), (-122, 24), (-112, 40), (-100, 52), (-90, 60), (-80, 62),
     (-68, 62), (-54, 58), (-38, 52), (-22, 40), (-10, 25), (-4, 12)]
TAIL_AT = 15              # the tail grows from the hindwing here (a point of P)
P = [(x * 1.28, y * 0.88) if i not in (0, len(P) - 1) else (x, y) for i, (x, y) in enumerate(P)]   # Moth's span at its usual reach (0.86)


def margin():
    fine = []
    for i in range(len(P) - 1):
        p0, p1, p2, p3 = P[max(0, i - 1)], P[i], P[i + 1], P[min(len(P) - 1, i + 2)]
        for j in range(24):
            t = j / 24
            t2, t3 = t * t, t * t * t
            fine.append(tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                                     (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in (0, 1)))
    fine.append(P[-1])
    # Sampled by length, three times as densely about the tail's root, so the
    # tail is a narrow strap of a few points.
    tail = TAIL_AT * 24
    length = [0]
    for i in range(1, len(fine)):
        dense = 1 + 2.2 * math.exp(-((i - tail) / 30) ** 2)
        length.append(length[-1] + dense * math.hypot(fine[i][0] - fine[i - 1][0], fine[i][1] - fine[i - 1][1]))
    total = length[-1]
    out, j = [], 0
    for i in range(MARGIN):
        want = total * i / (MARGIN - 1)
        while j < len(fine) - 2 and length[j + 1] < want:
            j += 1
        w = min(1, (want - length[j]) / ((length[j + 1] - length[j]) or 1))
        out.append((fine[j][0] + (fine[j + 1][0] - fine[j][0]) * w, fine[j][1] + (fine[j + 1][1] - fine[j][1]) * w))
    return out


WING = margin()


def at(points, u):
    f = u * (len(points) - 1)
    i = min(len(points) - 2, int(f))
    w = f - i
    return (points[i][0] + (points[i + 1][0] - points[i][0]) * w, points[i][1] + (points[i + 1][1] - points[i][1]) * w)


def mirror(points):
    return [(-x, y) for x, y in points]


def nearest(x, y):
    return min(range(MARGIN), key=lambda i: math.hypot(WING[i][0] - x, WING[i][1] - y))


TAIL_TIP = nearest(*P[TAIL_AT])
JUNCTION = min(range(10, MARGIN - 10), key=lambda i: abs(WING[i][1] - 2))   # where the forewing meets the hindwing
APEX = min(range(MARGIN), key=lambda i: WING[i][0])   # the forewing's tip
TAIL_ANGLE = 0.3          # the tail points down and out, radians from straight down
TAIL_PROFILE = [1.0, 1.0, 0.94, 0.74, 0.44, 0.16]   # how far each point of the strap moves, by its distance from the tip


def vein_points(u):
    tip = at([(x * INNER, y * INNER) for x, y in WING], u)
    base = (-3, (u - 0.5) * 12)
    root = (base[0] + (tip[0] - base[0]) * 0.2, base[1] + (tip[1] - base[1]) * 0.2)   # short of the body, so they never merge
    return ribbon([root, (root[0] + (tip[0] - root[0]) * 0.55 + (tip[1] - root[1]) * 0.03,
                          root[1] + (tip[1] - root[1]) * 0.55 - (tip[0] - root[0]) * 0.03), tip], 1.2, 0.8)


def comb(fore):
    """A half wing's veins as one closed outline: out along each vein's one
    side and back along the other, vein by vein across the root."""
    pts = []
    for i in VEINS:
        if (i < JUNCTION) != fore:
            continue
        u = i / (MARGIN - 1)
        r = vein_points(u)
        pts += r[:3] + r[3:]
    return pts


def forewing(i):
    """How much margin point i turns with the forewing (1) rather than stays
    with the hindwing (0): all of it to just past the apex, then less and
    less down its outer edge and over the hindwing's shoulder, so a forewing
    swept back folds the edge rather than crossing it (no outline crosses
    itself at any flux or coupling, to 21 degrees)."""
    a, b = APEX + 4, JUNCTION + 10
    return 1.0 if i <= a else 0.0 if i >= b else 0.5 + 0.5 * math.cos(math.pi * (i - a) / (b - a))


def tail(i):
    """How far margin point i moves with the tail, as a share of its length."""
    d = abs(i - TAIL_TIP)
    return TAIL_PROFILE[d] if d < len(TAIL_PROFILE) else 0.0


def build():
    out = [f'<g id="{ID}_panel">']
    header = (ROOT / "tools" / "header.svg.in").read_text().replace("@ID@", ID)
    for i in range(8):
        header = header.replace(f"@NODE_{i}@", str(HEADER + i))
    out.append(header.rstrip())

    # The edges: the wing's outline at rest (display.cpp places them at
    # scale 2, from the wing's middle, so the 0.5 px stroke draws a 1 px line).
    for p in range(4):
        side = p & 1
        mid = MIDDLE * (1 if side else -1)
        # The margin (or the lit band, at 0.4 of its reach), closed through two
        # root points on the line between its ends, as display.cpp draws it: it
        # runs straight on through the root, never doubling back.
        reach = 0.9 if p < 2 else 0.4
        margin = [(x * reach, y * reach) for x, y in WING]
        first, last = margin[0], margin[-1]
        between = lambda t: (last[0] + (first[0] - last[0]) * t, last[1] + (first[1] - last[1]) * t)
        pts = [between(0.6)] + margin + [between(0.4)]
        if side:
            pts = mirror(pts)
        rel = [((x - mid) / 2, y / 2) for x, y in pts]
        d = "M" + " L".join(f"{num(x, 4)},{num(y, 4)}" for x, y in rel) + " Z"
        assert len(rel) == POINTS and len(set(rel)) == POINTS
        out.append(f'<path id="{ID}_node_{PATHS + p}" transform="matrix(1 0 0 1 {CX + mid} {CY})" d="{d}" fill="none" '
                   f'stroke="{GREYS[15] if p < 2 else GREYS[9]}" stroke-width="{WIDTHS[15] if p < 2 else WIDTHS[9]}" '
                   f'stroke-linejoin="round"/>')

    # Veins, from each wing's root (display.cpp scales them about it): one
    # comb-shaped outline a half wing, ending at the inner zone's edge.
    statics = 0
    for side in range(2):
        sx = -1 if side else 1
        pivot = (CX + sx * PIVOT[0], CY + PIVOT[1])
        rel = lambda pts: [((x - PIVOT[0]) * sx, y - PIVOT[1]) for x, y in pts]
        for half, fore in enumerate((True, False)):
            out.append(node(VEIN + 2 * side + half, outline(rel(comb(fore))), GREYS[6], width=0.1, at=pivot))

    # Eyespots: a ring (a circle, stroked), parked off screen until
    # display.cpp places it.
    for e in range(2 * EYES_A_WING):
        out.append(f'<circle id="{ID}_node_{EYE + e}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" r="4.5" '
                   f'fill="none" stroke="{GREYS[12]}" stroke-width="{WIDTHS[12]}"/>')

    # The antennae: thin, clubbed at their tips, nodes so a strike can flick them.
    for side in range(2):
        s = 1 if side else -1
        stalk = [(s * 1.6, -14), (s * 4, -26), (s * 10, -42), (s * 18, -56), (s * 23, -64)]
        shape = ribbon(stalk, 1.3, 1.0)
        # One outline: up the stalk's one side, round the club's far side from
        # where that side meets it to where the other leaves, down the other:
        # it never turns back on itself (the OP-XY draws such a join as a spike).
        half = len(shape) // 2
        cx, cy, rx, ry = s * 24.2, -66.2, 2.2, 3.4
        up, down = shape[half - 1], shape[half]
        start = math.atan2((up[1] - cy) / ry, (up[0] - cx) / rx)
        end = math.atan2((down[1] - cy) / ry, (down[0] - cx) / rx)
        sweep = (end - start) % (2 * math.pi)
        if sweep < math.pi:   # the long way round, over the club's top
            sweep -= 2 * math.pi
        arc = [(cx + rx * math.cos(start + sweep * (k + 1) / 11), cy + ry * math.sin(start + sweep * (k + 1) / 11)) for k in range(10)]
        pts = shape[:half] + arc + shape[half:]
        out.append(node(ANTENNA + side, outline(pts), GREYS[10], width=0.1, at=(CX, CY)))

    # The body: head, the thorax with the pin through it, the abdomen. Static.
    body = [
        (ellipse(CX, CY - 15.5, 3.8, 3.4, 24), GREYS[9], 0.1),            # head
        (ellipse(CX, CY - 3, 5.2, 11, 32), GREYS[7], 0.1),                # thorax
        (ellipse(CX, CY - 4, 1.9, 1.9, 16), GREYS[13], 0.1),              # the pin's head
    ]
    for pts, ink, w in body:
        out.append(art(outline(pts), ink, width=w))
        statics += 1
    abdomen = ellipse(CX, CY + 28, 3.8, 20, 36)
    out.append(art(outline(abdomen), GREYS[6], GREYS[8], 0.25))
    statics += 1

    # The specimen label, in the corner where Moth's is: static artwork.
    out.append(art(outline([(374, 199), (474, 199), (474, 216), (374, 216)]), "none", GREYS[7], 0.8))
    out.append(f'<text transform="matrix(1 0 0 1 378.5 210.8)" fill="{GREYS[11]}" {FONT} font-size="9px">Papilio hofstadteri</text>')
    statics += 1

    # Palette: Moth's sixteen greys, each its own fill and stroke; the
    # brighter the grey the heavier its line, so a node lit brighter is drawn
    # bolder (the margin and lit band are drawn at scale 2: 1 to 2 px).
    for i, grey in enumerate(GREYS):
        out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{WIDTHS[i]}"/>')
    out.append("</g>")
    return "\n".join(out) + "\n", statics


def q10(v):
    return int(round(v * 1024))


def plate_h():
    lines = ["// Generated by tools/make_panel.py from the artwork. Edit the script, not this file.",
             "#pragma once",
             "#include <opxy/types.hpp>",
             "",
             "namespace butterfly {",
             "namespace plate {",
             "using opxy::i32;",
             "using opxy::u32;",
             "// Nodes: the four outlines (left and right margins, then lit bands), the header's bars,",
             "// each wing's veins (forewing, hindwing; left then right), eight eyespot rings (four a",
             "// wing, left then right), the antennae.",
             f"const u32 edges = {PATHS}, header = {HEADER}, veins = {VEIN}, eyes = {EYE}, eyes_a_wing = {EYES_A_WING}, "
             f"antennae = {ANTENNA}, nodes = {NODE_COUNT};",
             f"const i32 centre_x = {CX}, centre_y = {CY}, wing_middle = {MIDDLE}, pivot_x = {PIVOT[0]};",
             "// The wing's zones, Q15 of the margin's reach: the chain's middle site and its end, and the",
             "// inner zone's edge, which the margin never comes inside.",
             f"const i32 site_near = {round(SITE_NEAR * 32768)}, site_far = {round(SITE_FAR * 32768)}, inner = {round(INNER * 32768)};",
             "// The left wing's margin at rest, Q10 px from the butterfly's centre (the right mirrors x),",
             "// from the forewing's leading edge round to the hindwing's tail and in to the root.",
             f"const u32 margin = {MARGIN};",
             f"const i32 wing_x[{MARGIN}] = {{{', '.join(str(q10(x)) for x, _ in WING)}}};",
             f"const i32 wing_y[{MARGIN}] = {{{', '.join(str(q10(v)) for _, v in WING)}}};",
             "// How much each margin point turns with the forewing, and moves with the tail, Q15.",
             f"const i32 forewing[{MARGIN}] = {{{', '.join(str(round(forewing(i) * 32767)) for i in range(MARGIN))}}};",
             f"const i32 tail[{MARGIN}] = {{{', '.join(str(round(tail(i) * 32767)) for i in range(MARGIN))}}};",
             "// The tail's direction (the left wing's; the right mirrors x), Q10, and its tip.",
             f"const i32 tail_dx = {q10(-math.sin(TAIL_ANGLE))}, tail_dy = {q10(math.cos(TAIL_ANGLE))};",
             f"const u32 tail_tip = {TAIL_TIP};",
             f"// The antennae's pivots, Q10 px from the centre (the right mirrors x).",
             f"const i32 antenna_x = {q10(1.6)}, antenna_y = {q10(-14)};",
             "}",
             "}",
             ""]
    return "\n".join(lines)


def main():
    svg, statics = build()
    (ROOT / "panel.svg.fragment").write_text(svg)
    (ROOT / "plate.h").write_text(plate_h())
    d = json.loads((ROOT / "definition.json").read_text())
    d["resources"]["display"].update({"paths": 4, "points": 64, "nodes": NODE_COUNT, "texts": 16, "palette": 16})
    (ROOT / "definition.json").write_text(json.dumps(d, indent=2) + "\n")
    print(f"panel.svg.fragment: {len(svg.encode())} bytes, {statics} static paths; {NODE_COUNT} nodes")
    if PNG:
        page = ('<html><body style="margin:0;background:#000"><svg xmlns="http://www.w3.org/2000/svg" width="1440" '
                'height="660" viewBox="0 0 480 220"><rect width="480" height="220" fill="#000"/>' + svg + '</svg></body></html>')
        with tempfile.TemporaryDirectory() as tmp:
            html = pathlib.Path(tmp) / "plate.html"
            html.write_text(page)
            chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
            subprocess.run([chrome, "--headless", "--disable-gpu", f"--screenshot={PNG}", "--window-size=1440,660",
                            "--hide-scrollbars", html.as_uri()], capture_output=True, timeout=60)
        print(f"rendered {PNG}")


main()
