"""Shared by make_gif.py and make_controls.py: builds tests/screen.cpp, plays a
scenario through the real display code, and applies its frames to
panel.svg.fragment as the renderers do. A viewing tool, not part of the
machine."""
import copy
import json
import os
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "butterfly"
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
# The device's face (UniversNextPro-Light.ttf, stock content, not in this
# repository), from the path in XY_FONT. Without it the browser falls back
# to a serif.
FONT = pathlib.Path(os.environ['XY_FONT']) if os.environ.get('XY_FONT') else None
if FONT is None:
    print("render.py: XY_FONT is not set, so the label is drawn in the browser's serif; "
          "set it to the path of UniversNextPro-Light.ttf for the device's face", file=sys.stderr)
elif not FONT.exists():
    raise SystemExit(f'render.py: XY_FONT is {FONT}, which does not exist')


def head():
    """A page's <head>: the device's face for the artwork's text, when it is found."""
    face = f"@font-face{{font-family:'UniversNextPro-Light';src:url('{FONT.as_uri()}')}}" if FONT else ''
    return f'<head><meta charset="utf-8"><style>{face}</style></head>'


def frames(sdk, folder, *args):
    """The frames a screen scenario prints, as dicts."""
    subprocess.run(['c++', '-std=c++11', '-O2', f'-I{sdk}/sdk/include', str(ROOT / 'tests' / 'screen.cpp'), '-o', f'{folder}/screen'],
                   check=True)
    lines = subprocess.run([f'{folder}/screen', *args], check=True, capture_output=True, text=True).stdout.splitlines()
    return [json.loads(line) for line in lines if line.startswith('{')]


_base = None


def svg(data, width, height):
    """One frame as a standalone SVG element, drawn at width x height."""
    global _base
    if _base is None:
        _base = ET.fromstring((ROOT / 'panel.svg.fragment').read_text())
    root = copy.deepcopy(_base)
    by = {e.get('id'): e for e in root.iter() if e.get('id')}
    pal = [(by[f'{ID}_palette_{i}'].get('fill'), by[f'{ID}_palette_{i}'].get('stroke'), by[f'{ID}_palette_{i}'].get('stroke-width'))
           for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data['nodes']):
        e = by.get(f'{ID}_node_{i}')
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
        by[f'{ID}_node_{p}'].set('d', 'M' + ' L'.join(f'{pts[j] / 1024},{pts[j + 1] / 1024}' for j in range(0, len(pts), 2)) + ' Z')
    for t, (x, y, s, chars) in enumerate(data['texts']):
        e = by[f'{ID}_text_{t}']
        e.set('transform', f'matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})')
        e.text = chars
    body = ET.tostring(root, encoding='unicode').replace('ns0:', '').replace(':ns0', '')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="{width}" height="{height}" style="display:block">'
            f'<rect width="480" height="220" fill="#000"/>{body}</svg>')


def screenshot(html, png, width, height, chrome=CHROME):
    """Rasterises an HTML file with headless Chrome."""
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files',
                    '--virtual-time-budget=3000', '--force-device-scale-factor=1', f'--window-size={width},{height}',
                    f'--screenshot={png}', f'file://{html}'], check=True, capture_output=True)
