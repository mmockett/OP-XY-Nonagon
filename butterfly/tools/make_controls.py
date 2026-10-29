#!/usr/bin/env python3
"""Makes docs/controls.png, the contact sheet: one row a control, at 0, 25,
50, 75 and 100 with the others at Specimen's, each 0.6 s into a held note, as
the display code draws it (tests/screen.cpp's "controls" scenario). Each
cell is the screen at 1:1, 480 x 220 pixels, in the device's face.

    python tools/make_controls.py --sdk ../open-xy [--chrome PATH]

Needs a host C++ compiler and Google Chrome."""
import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render  # noqa: E402

CONTROLS = ['flux', 'coupling', 'spread', 'strike', 'glow', 'site', 'tilt', 'wingbeat']
VALUES = [0, 25, 50, 75, 100]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--chrome', default=render.CHROME)
parser.add_argument('--out', default=str(render.ROOT / 'docs' / 'controls.png'))
args = parser.parse_args()
S = tempfile.mkdtemp(prefix='butterfly-controls-')
frames = render.frames(args.sdk, S, 'controls')
assert len(frames) == len(CONTROLS) * len(VALUES), len(frames)

W, H, LABEL, GAP, TOP = 480, 220, 96, 12, 28
style = ('body{margin:0;background:#111;color:#aaa;font:13px Helvetica,Arial,sans-serif}'
         '.row{display:flex;align-items:center;margin-bottom:%dpx}.name{width:%dpx;text-align:right;padding-right:12px;color:#ddd}'
         '.cell{margin-right:%dpx}.head{display:flex;height:%dpx;align-items:end;padding-bottom:4px}'
         '.head div{width:%dpx;margin-right:%dpx;text-align:center}' % (GAP, LABEL - 12, GAP, TOP - 4, W, GAP))
rows = ['<div class="head"><div style="width:%dpx;margin:0"></div>%s</div>' % (LABEL, ''.join(f'<div>{v}</div>' for v in VALUES))]
for c, name in enumerate(CONTROLS):
    cells = ''.join(f'<div class="cell">{render.svg(frames[c * len(VALUES) + v], W, H)}</div>' for v in range(len(VALUES)))
    rows.append(f'<div class="row"><div class="name">{name}</div>{cells}</div>')
html = f'{S}/sheet.html'
open(html, 'w').write(f'<html>{render.head()}<style>{style}</style><body>{"".join(rows)}</body></html>')
width = LABEL + len(VALUES) * (W + GAP)
height = TOP + len(CONTROLS) * (H + GAP)
render.screenshot(html, args.out, width, height, args.chrome)
print('->', args.out, f'({width} x {height})')
