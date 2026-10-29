#!/usr/bin/env python3
"""Applies a frame written by tools/frames.cpp to panel.svg.fragment, as the
renderers do, and writes a standalone SVG of the screen:
    XY_FONT=<path to UniversNextPro-Light.ttf> frame2svg.py frame.json out.svg [scale]"""
import json
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
# The device's face, UniversNextPro-Light.ttf, from an OP-XY checkout; stock
# content, never copied here. XY_FONT gives its path.
_font = os.environ.get("XY_FONT")
if not _font or not pathlib.Path(_font).is_file():
    sys.exit("frame2svg.py: set XY_FONT to the path of UniversNextPro-Light.ttf "
             "(the OP-XY's face, from an OP-XY checkout)" + (f"; {_font} is not a file" if _font else ""))
FONT = pathlib.Path(_font).resolve().as_uri()
frame = json.loads(pathlib.Path(sys.argv[1]).read_text())
scale = float(sys.argv[3]) if len(sys.argv) > 3 else 2
svg = (ROOT / "panel.svg.fragment").read_text()
palette = {int(m.group(1)): (m.group(2), m.group(3)) for m in
           re.finditer(r'id="ghost_palette_(\d+)"[^>]*fill="(#[0-9a-fA-F]{6})"[^>]*stroke-width="([\d.]+)"', svg)}


def q(v):
    return v / 1024


def node(match):
    tag = match.group(0)
    i = int(match.group(1))
    x, y, s, c, sn, shade, paint = frame["nodes"][i]
    k = q(s)
    transform = f'matrix({k * q(c):.4f} {k * q(sn):.4f} {-k * q(sn):.4f} {k * q(c):.4f} {q(x):.3f} {q(y):.3f})'
    tag = re.sub(r' transform="[^"]*"', "", tag)
    tag = tag.replace(f'id="ghost_node_{i}"', f'id="ghost_node_{i}" transform="{transform}"')
    if i < len(frame["paths"]):
        d = "M" + " L".join(f"{q(px):.3f},{q(py):.3f}" for px, py in frame["paths"][i]) + " Z"
        tag = re.sub(r' d="[^"]*"', f' d="{d}"', tag)
    if paint:
        colour, width = palette[shade]
        tag = re.sub(r' stroke="[^"]*"', f' stroke="{colour}"', tag)
        tag = re.sub(r' stroke-width="[^"]*"', f' stroke-width="{width}"', tag)
        if paint == 2 and 'fill="none"' not in tag:
            tag = re.sub(r' fill="[^"]*"', f' fill="{colour}"', tag)
    return tag


def text(match):
    i = int(match.group(1))
    x, y, s, chars = frame["texts"][i]
    body = match.group(0)
    body = re.sub(r' transform="[^"]*"', f' transform="matrix({q(s):.4f} 0 0 {q(s):.4f} {q(x):.3f} {q(y):.3f})"', body)
    return re.sub(r'>[^<]*</text>', f'>{chars}</text>', body)


svg = re.sub(r'<(?:path|circle) id="ghost_node_(\d+)"[^>]*/>', node, svg)
svg = re.sub(r'<text id="ghost_text_(\d+)"[^>]*>[^<]*</text>', text, svg)
out = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{480 * scale:.0f}" height="{220 * scale:.0f}" viewBox="0 0 480 220">'
       f'<style>@font-face {{ font-family: "UniversNextPro-Light"; src: url("{FONT}"); }} '
       f'text {{ font-family: "UniversNextPro-Light"; }}</style>{svg}</svg>')
pathlib.Path(sys.argv[2]).write_text(out)
