#!/usr/bin/env python3
"""Applies frames printed by tests/screen.cpp to panel.svg.fragment and writes
one standalone SVG (and an HTML page that shows it at 3x) per frame, as the
OP-XY would draw it: a measuring and viewing tool, not part of the machine
(after Mach's).
  screen session 60 3.2 4.5 | python tools/frame2svg.py out/prefix"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
fragment = (ROOT / "panel.svg.fragment").read_text()
palette = {}
for m in re.finditer(r'id="drop_palette_(\d+)"[^>]*fill="(#[0-9a-fA-F]{6})"[^>]*stroke-width="([\d.]+)"', fragment):
    palette[int(m.group(1))] = (m.group(2), m.group(3))


def apply(frame, font=None):
    """The frame as a standalone SVG; `font`, a path to the device's face
    (UniversNextPro-Light), loads it for the header's text."""
    svg = fragment
    for i, (x, y, scale, c, s, shade, paint) in enumerate(frame["nodes"]):
        k = scale / 1024
        matrix = f'matrix({k * c / 1024:.4f} {k * s / 1024:.4f} {-k * s / 1024:.4f} {k * c / 1024:.4f} {x / 1024:.3f} {y / 1024:.3f})'
        def repl(m):
            tag = m.group(0)
            tag = re.sub(r'transform="[^"]*"', f'transform="{matrix}"', tag)
            if 'transform=' not in tag:
                tag = tag.replace(f'id="drop_node_{i}"', f'id="drop_node_{i}" transform="{matrix}"')
            if paint and 3 <= i < 11:
                return tag   # the header's bar keeps its own fill
            if paint:
                colour, width = palette[shade]
                tag = re.sub(r'stroke="[^"]*"', f'stroke="{colour}"', tag)
                tag = re.sub(r'stroke-width="[^"]*"', f'stroke-width="{width}"', tag)
                if paint == 2:
                    tag = re.sub(r'fill="[^"]*"', f'fill="{colour}"', tag)
            return tag
        svg = re.sub(rf'<(path|circle) id="drop_node_{i}"[^>]*>', repl, svg)
    for p, pts in enumerate(frame["paths"]):
        d = "M" + " L".join(f"{pts[j] / 1024:.3f},{pts[j + 1] / 1024:.3f}" for j in range(0, len(pts), 2)) + " Z"
        svg = re.sub(rf'(<path id="drop_node_{p}"[^>]*?) d="[^"]*"', rf'\1 d="{d}"', svg)
    for t, (x, y, scale, chars) in enumerate(frame["texts"]):
        k = scale / 1024
        svg = re.sub(rf'(<text id="drop_text_{t}" transform=")[^"]*("[^>]*>)[^<]*(</text>)',
                     lambda m: f'{m.group(1)}matrix({k:.4f} 0 0 {k:.4f} {x / 1024:.3f} {y / 1024:.3f}){m.group(2)}{chars}{m.group(3)}', svg)
    face = (f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("{pathlib.Path(font).resolve().as_uri()}")}}</style>'
            if font else "")
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220">' + face +
            '<rect width="480" height="220" fill="#000"/>' + svg + '</svg>')


def main():
    prefix = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "frame")
    for line in sys.stdin:
        line = line.strip()
        if not line.startswith("{"):
            continue
        frame = json.loads(line)
        name = f"{prefix}-{frame['t']:.2f}"
        svg = apply(frame)
        pathlib.Path(name + ".svg").write_text(svg)
        pathlib.Path(name + ".html").write_text(
            '<html><body style="margin:0;background:#000"><div style="transform:scale(3);transform-origin:0 0">'
            + svg + '</div></body></html>')
        print(name)


if __name__ == "__main__":
    main()
