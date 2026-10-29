#!/usr/bin/env python3
"""Makes docs/butterfly.gif: builds tests/screen.cpp against the SDK, plays its
"gif" scenario through the real display code at 60 frames a second, applies
every fifth frame to panel.svg.fragment as the renderers do, rasterises them
with a headless Chrome at 720 x 330 and joins them at 12 frames a second with
ffmpeg.

    python tools/make_gif.py --sdk ../open-xy [--chrome PATH]

Needs a host C++ compiler, Google Chrome and ffmpeg. The frames are the
display's own; fonts are the browser's."""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--chrome', default=render.CHROME)
parser.add_argument('--out', default=str(render.ROOT / 'docs' / 'butterfly.gif'))
args = parser.parse_args()
S = tempfile.mkdtemp(prefix='butterfly-gif-')
frames = render.frames(args.sdk, S, 'gif', '60', '5')
W, H, CH = 720, 330, 12

os.makedirs(f'{S}/png', exist_ok=True)
n = 0
for start in range(0, len(frames), CH):
    chunk = frames[start:start + CH]
    html = f'{S}/chunk.html'
    open(html, 'w').write(f'<html>{render.head()}<body style="margin:0;background:#000">' + ''.join(render.svg(d, W, H) for d in chunk) + '</body></html>')
    shot = f'{S}/chunk.png'
    render.screenshot(html, shot, W, H * len(chunk), args.chrome)
    for k in range(len(chunk)):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', shot, '-vf', f'crop={W}:{H}:0:{k * H}', f'{S}/png/f{n:03d}.png'], check=True)
        n += 1
os.makedirs(os.path.dirname(args.out), exist_ok=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', '12', '-i', f'{S}/png/f%03d.png', '-vf',
                'split[a][b];[a]palettegen=max_colors=32:reserve_transparent=0[p];[b][p]paletteuse=dither=none', '-loop', '0', args.out],
               check=True)
print(n, 'frames ->', args.out, f'(stills in {S}/png)')
