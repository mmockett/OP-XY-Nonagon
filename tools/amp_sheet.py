#!/usr/bin/env python3
"""A machine's screen under a short amp envelope against a long one, at 1:1:
two rows (short above, long below) of the frames at 0.1, 0.4 and 0.9 s into a
note held for a second, and 0.1, 0.5, 1.5 and 3 s after its release. Builds
tools/amp_sheet.cpp against the SDK with the machine's real display code,
applies each frame to its panel.svg.fragment as the renderers do, and
rasterises the sheet with a headless Chrome. Not part of any machine.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/amp_sheet.py --sdk <open-xy> \\
        --machine sand --controls 0.2083,0.6,0.6,0.3,0.1667,0.5,0,0 --out <png>

Text is set in the device's face, read from --font or $XY_FONT (stock
content, so it is never copied here)."""
import argparse, copy, json, os, pathlib, shutil, subprocess, sys, tempfile, xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', required=True)
parser.add_argument('--machine', required=True)
parser.add_argument('--controls', required=True, help='eight values 0..1, comma-separated')
parser.add_argument('--page', type=int, default=0)
parser.add_argument('--out', required=True)
parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--chrome', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
parser.add_argument('--font', default=os.environ.get('XY_FONT', ''))
args = parser.parse_args()
if not os.path.exists(args.font):
    sys.exit("set XY_FONT or --font to UniversNextPro-Light.ttf: the text mustn't fall back to another face")
m = args.machine
folder = ROOT / m
controls = [float(v) for v in args.controls.split(',')]
assert len(controls) == 8
S = tempfile.mkdtemp(prefix=f'{m}-amp-')
subprocess.run([args.cxx, '-std=c++11', '-O2', f'-I{args.sdk}/sdk/include', f'-I{folder}', f'-DMACHINE={m}',
                str(ROOT / 'tools/amp_sheet.cpp'), '-o', f'{S}/frames'], check=True)
subprocess.run([f'{S}/frames', f'{S}/frames.jsonl', *map(str, controls), str(args.page)], check=True)
frames = [json.loads(line) for line in open(f'{S}/frames.jsonl')]
base = ET.fromstring((folder / 'panel.svg.fragment').read_text())
FACE = f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}} text{{font-family:"UniversNextPro-Light"}}</style>'


def svg(data):
    root = copy.deepcopy(base)
    by = {e.get('id'): e for e in root.iter() if e.get('id')}
    palette = [by.get(f'{m}_palette_{i}') for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data['nodes']):
        e = by.get(f'{m}_node_{i}')
        if e is None:
            continue
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set('transform', f'matrix({a} {b} {-b} {a} {x / 1024} {y / 1024})')
        if paint in (1, 2) and palette[shade] is not None:
            e.set('stroke', palette[shade].get('stroke'))
            e.set('stroke-width', palette[shade].get('stroke-width'))
            # As the device: only a node authored with a fill is filled.
            if paint == 2 and e.get('fill') not in (None, 'none'):
                e.set('fill', palette[shade].get('fill'))
    for p, pts in enumerate(data['paths']):
        by[f'{m}_node_{p}'].set('d', 'M' + ' L'.join(f'{x / 1024},{y / 1024}' for x, y in pts) + ' Z')
    for t, (x, y, s, chars) in enumerate(data['texts']):
        e = by.get(f'{m}_text_{t}')
        if e is None:
            continue
        e.set('transform', f'matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})')
        e.text = chars
    body = ET.tostring(root, encoding='unicode').replace('ns0:', '').replace(':ns0', '')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="480" height="222" style="display:block">'
            f'<rect width="480" height="222" fill="#000"/>{body}</svg>')


rows = {'short': [], 'long': []}
for f in frames:
    rows[f['row']].append(f)
labels = {'short': 'short envelope: attack 0, sustain 100, release ~40 ms',
          'long': 'long envelope: attack 1.25 s, sustain 75, release 3.4 s'}
parts = []
for row in ('short', 'long'):
    parts.append(f'<div class="row"><div class="h">{m} · {labels[row]}</div>')
    for f in rows[row]:
        when = f'{f["t"]:.1f} s, held' if f['t'] < 1 else f'{f["t"] - 1:.1f} s after release'
        parts.append(f'<div class="cell">{svg(f)}<div class="v">{when}</div></div>')
    parts.append('</div>')
style = ('body{margin:0;background:#1c1c1c;color:#ccc;font:14px UniversNextPro-Light}'
         '.row{clear:both;overflow:hidden}.h{padding:8px 8px 4px}.cell{float:left;width:480px;height:246px;margin-right:4px}.v{padding:5px 8px 0}')
html = f'{S}/sheet.html'
columns = max(len(r) for r in rows.values())
width, height = columns * 484, 2 * (246 + 30)
open(html, 'w').write(f'<html><head><meta charset="utf-8">{FACE}<style>{style}</style></head><body>{"".join(parts)}</body></html>')
out = pathlib.Path(args.out)
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([args.chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files',
                '--virtual-time-budget=2000', '--force-device-scale-factor=1', f'--window-size={width},{height}',
                f'--screenshot={out}', f'file://{html}'], check=True, capture_output=True)
shutil.rmtree(S)
print('wrote', out)
