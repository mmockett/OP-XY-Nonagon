#!/usr/bin/env python3
"""Makes docs/sand.gif, or with --sheet docs/controls-1x.png and docs/controls.png:
builds tools/frames.cpp against the SDK (the real display code at 60 frames a
second), applies each frame to panel.svg.fragment as the renderers do,
rasterises them with a headless Chrome and joins them at 20 frames a second
with ffmpeg. Not part of the machine.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/make_gif.py --sdk <open-xy> [--sheet]

Text is set in the device's face, read from --font or $XY_FONT (an OP-XY
checkout's render tools; it is stock content, so it is never copied here)."""
import argparse, copy, json, os, pathlib, subprocess, sys, tempfile, xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--chrome', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--sheet', action='store_true')
parser.add_argument('--font', default=os.environ.get('XY_FONT', ''))
args = parser.parse_args()
if not os.path.exists(args.font):
    sys.exit("set XY_FONT or --font to UniversNextPro-Light.ttf: the text mustn't fall back to another face")
S = tempfile.mkdtemp(prefix='sand-gif-')
FACE = f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}</style>'
CHROME = [args.chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files', '--virtual-time-budget=2000']
subprocess.run(['c++', '-std=c++11', '-O2', f'-I{args.sdk}/sdk/include', str(ROOT / 'tools/frames.cpp'), '-o', f'{S}/frames'], check=True)
subprocess.run([f'{S}/frames', 'sheet' if args.sheet else 'gif', f'{S}/frames.jsonl'], check=True)
frames = [json.loads(line) for line in open(f'{S}/frames.jsonl')]
base = ET.fromstring((ROOT / 'panel.svg.fragment').read_text())


def svg(data, w, h):
    root = copy.deepcopy(base)
    by = {e.get('id'): e for e in root.iter() if e.get('id')}
    palette = [by[f'sand_palette_{i}'] for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data['nodes']):
        e = by[f'sand_node_{i}']
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set('transform', f'matrix({a} {b} {-b} {a} {x / 1024} {y / 1024})')
        if paint in (1, 2):
            e.set('stroke', palette[shade].get('stroke'))
            e.set('stroke-width', palette[shade].get('stroke-width'))
            if paint == 2 and e.get('fill') not in (None, 'none'):
                e.set('fill', palette[shade].get('fill'))
    for p, pts in enumerate(data['paths']):
        by[f'sand_node_{p}'].set('d', 'M' + ' L'.join(f'{x / 1024},{y / 1024}' for x, y in pts) + ' Z')
    for t, (x, y, s, chars) in enumerate(data['texts']):
        e = by[f'sand_text_{t}']
        e.set('transform', f'matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})')
        e.text = chars
    body = ET.tostring(root, encoding='unicode').replace('ns0:', '').replace(':ns0', '')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="{w}" height="{h}" style="display:block">'
            f'<rect width="480" height="222" fill="#000"/>{body}</svg>')


if args.sheet:
    names = ['figure', 'bow', 'sand', 'grain', 'plate', 'settle', 'tilt', 'shake']
    parts = []
    for k, name in enumerate(names):
        for j, v in enumerate((0, 50, 100)):
            parts.append(f'<div class="cell">{svg(frames[3 * k + j], 480, 222)}<div class="v">{name} {v}</div></div>')
    style = ('body{margin:0;background:#1c1c1c;color:#ccc;font:14px UniversNextPro-Light}'
             '.cell{float:left;width:480px;height:246px}.v{padding:5px 8px 0}')
    html = f'{S}/sheet.html'
    open(html, 'w').write(f'<html><head>{FACE}<style>{style}</style></head><body>{"".join(parts)}</body></html>')
    for scale, name in ((1, 'controls-1x.png'), (1.5, 'controls.png')):
        out = ROOT / 'docs' / name
        subprocess.run(CHROME + [f'--force-device-scale-factor={scale}', '--window-size=1440,1968', f'--screenshot={out}', f'file://{html}'], check=True, capture_output=True)
        # Few greys: an indexed PNG keeps the sheet small.
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(out), '-vf', 'split[a][b];[a]palettegen=max_colors=64:reserve_transparent=0[p];[b][p]paletteuse=dither=none',
                        '-pix_fmt', 'pal8', f'{S}/indexed.png'], check=True)
        os.replace(f'{S}/indexed.png', out)
        print('wrote', out)
    raise SystemExit
W, H, CHUNK = 480, 222, 14
os.makedirs(f'{S}/png', exist_ok=True)
n = 0
for start in range(0, len(frames), CHUNK):
    chunk = frames[start:start + CHUNK]
    html, shot = f'{S}/chunk.html', f'{S}/chunk.png'
    open(html, 'w').write(f'<html><head>{FACE}</head><body style="margin:0;background:#000">{"".join(svg(d, W, H) for d in chunk)}</body></html>')
    subprocess.run(CHROME + ['--force-device-scale-factor=1', f'--window-size={W},{H * len(chunk)}', f'--screenshot={shot}', f'file://{html}'], check=True, capture_output=True)
    for k in range(len(chunk)):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', shot, '-vf', f'crop={W}:{H}:0:{k * H}', f'{S}/png/f{n:04d}.png'], check=True)
        n += 1
out = ROOT / 'docs/sand.gif'
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', '20', '-i', f'{S}/png/f%04d.png', '-vf',
                'split[a][b];[a]palettegen=max_colors=12:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle', '-loop', '0', str(out)], check=True)
print(n, 'frames ->', out)
