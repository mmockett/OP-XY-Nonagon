#!/usr/bin/env python3
"""Makes docs/cocoa.gif: builds tests/screen.cpp against the SDK (the real
display code playing its scripted scene at 60 frames a second, every fifth
frame written as JSON), applies each frame to panel.svg.fragment as the
renderers do, rasterises the frames with a headless Chrome and joins them at
12 frames a second with ffmpeg.

    python tools/make_gif.py --sdk <open-xy> [--font TTF] [--chrome PATH] [--from S --to S]
    python tools/make_gif.py --sdk <open-xy> --png 2.5 --out frame.png   # one frame, 3x

Needs a host C++ compiler, Google Chrome and ffmpeg. The frames are the
display's own; text is set in the device's face, UniversNextPro-Light, read
from --font or $XY_FONT (an OP-XY checkout's render tools; it is stock
content, so it is never copied here). The contact sheet is drawn at 1:1
(docs/controls-1x.png, one screen pixel a pixel, how the device shows it)
and at 2x (docs/controls.png)."""
import argparse, json, subprocess, os, copy, tempfile, pathlib, xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--chrome', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--from', dest='start', type=float, default=1.5, help='seconds from the start, idle (1.5 s) included')
parser.add_argument('--to', dest='stop', type=float, default=13.0)
parser.add_argument('--png', type=float, help='write one frame at this many seconds into the scene instead')
parser.add_argument('--out')
parser.add_argument('--sheet', action='store_true', help='write docs/controls-1x.png and docs/controls.png: each control at 0, 50 and 100')
parser.add_argument('--font', default=os.environ.get('XY_FONT', ''),
                    help="the device's face, UniversNextPro-Light.ttf (default: $XY_FONT)")
args = parser.parse_args()
S = tempfile.mkdtemp(prefix='cocoa-gif-')
FACE = (f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}</style>'
        if os.path.exists(args.font) else '')
if not FACE:
    print('warning: no font at ' + repr(args.font) + ': pass --font or set XY_FONT to UniversNextPro-Light.ttf;'
          ' text falls back to the browser face')
CHROME = [args.chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files',
          '--virtual-time-budget=2000']
subprocess.run(['c++', '-std=c++11', '-O2', f'-I{args.sdk}/sdk/include', str(ROOT / 'tests/screen.cpp'), '-o', f'{S}/screen'], check=True)
subprocess.run([f'{S}/screen', f'{S}/frames.jsonl'] + (['sheet'] if args.sheet else []), check=True, capture_output=True)
frames = [json.loads(l) for l in open(f'{S}/frames.jsonl')]
base = ET.fromstring(open(ROOT / 'panel.svg.fragment').read())


def svg(data, w, h):
    root = copy.deepcopy(base)
    by = {e.get('id'): e for e in root.iter() if e.get('id')}
    pal = [(by[f'cocoa_palette_{i}'].get('fill'), by[f'cocoa_palette_{i}'].get('stroke'), by[f'cocoa_palette_{i}'].get('stroke-width')) for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data['nodes']):
        e = by.get(f'cocoa_node_{i}')
        if e is None:
            continue
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set('transform', f'matrix({a} {b} {-b} {a} {x / 1024} {y / 1024})')
        if paint in (1, 2):
            e.set('stroke', pal[shade][1])
            e.set('stroke-width', pal[shade][2])
            if paint == 2 and e.get('fill') not in (None, 'none'):
                e.set('fill', pal[shade][0])
    for p, pts in enumerate(data['paths']):
        by[f'cocoa_node_{p}'].set('d', 'M' + ' L'.join(f'{x / 1024},{y / 1024}' for x, y in pts) + ' Z')
    for t, (x, y, s, chars) in enumerate(data['texts']):
        e = by[f'cocoa_text_{t}']
        e.set('transform', f'matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})')
        e.text = chars
    body = ET.tostring(root, encoding='unicode').replace('ns0:', '').replace(':ns0', '')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="{w}" height="{h}" style="display:block">'
            f'<rect width="480" height="220" fill="#000"/>{body}</svg>')


def shoot(chunk, w, h, png):
    html = f'{S}/chunk.html'
    open(html, 'w').write('<html><head>' + FACE + '</head><body style="margin:0;background:#000">' + ''.join(svg(d, w, h) for d in chunk) + '</body></html>')
    subprocess.run(CHROME + ['--force-device-scale-factor=1', f'--window-size={w},{h * len(chunk)}',
                    f'--screenshot={png}', f'file://{html}'], check=True, capture_output=True)


IDLE = 18   # the scene's 1.5 s idle, in dumped frames (12 a second)
if args.sheet:
    names = ['powder', 'taps', 'fizz', 'cup', 'fill', 'spoon', 'pops', 'stir']
    W, H = 480, 220
    cells = []
    for k, name in enumerate(names):
        row = ''.join(f'<div style="display:inline-block;margin:0 6px 6px 0">{svg(frames[3 * k + j], W, H)}</div>' for j in range(3))
        cells.append(f'<div style="white-space:nowrap"><div style="display:inline-block;width:70px;color:#ccc;font:16px sans-serif;vertical-align:top;padding-top:100px">{name}</div>{row}</div>')
    head = '<div style="color:#999;font:14px sans-serif;margin:6px 0 6px 76px">' + ''.join(f'<span style="display:inline-block;width:486px">{v}</span>' for v in ('0', '50', '100')) + '</div>'
    html = f'{S}/sheet.html'
    open(html, 'w').write('<html><head>' + FACE + '</head><body style="margin:8px;background:#111">' + head + ''.join(cells) + '</body></html>')
    for scale, name in ((1, 'controls-1x.png'), (2, 'controls.png')):
        out = str(ROOT / 'docs' / name)
        subprocess.run(CHROME + [f'--force-device-scale-factor={scale}', '--window-size=1550,1830',
                        f'--screenshot={out}', f'file://{html}'], check=True, capture_output=True)
        print('sheet ->', out)
    raise SystemExit
if args.png is not None:
    shoot([frames[IDLE + round(args.png * 12)]], 1440, 660, args.out or 'frame.png')
    raise SystemExit
W, H, CH = 720, 330, 14
chosen = frames[round(args.start * 12):round(args.stop * 12)]
os.makedirs(f'{S}/png', exist_ok=True)
n = 0
for start in range(0, len(chosen), CH):
    chunk = chosen[start:start + CH]
    shot = f'{S}/chunk.png'
    shoot(chunk, W, H, shot)
    for k in range(len(chunk)):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', shot, '-vf', f'crop={W}:{H}:0:{k * H}', f'{S}/png/f{n:03d}.png'], check=True)
        n += 1
out = args.out or str(ROOT / 'docs/cocoa.gif')
os.makedirs(os.path.dirname(out), exist_ok=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', '12', '-i', f'{S}/png/f%03d.png', '-vf',
                'split[a][b];[a]palettegen=max_colors=32:reserve_transparent=0[p];[b][p]paletteuse=dither=none', '-loop', '0', out], check=True)
print(n, 'frames ->', out)
