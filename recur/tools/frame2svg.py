#!/usr/bin/env python3
"""Applies a frame written by tools/screen.cpp to panel.svg.fragment, as the
renderers do, and writes a standalone SVG:
    [XY_FONT=<path to UniversNextPro-Light.ttf>] python frame2svg.py frame.json out.svg
With XY_FONT set to the device's face (UniversNextPro-Light.ttf, from an
OP-XY checkout; it is never copied here), the SVG names it in an @font-face,
so Chrome draws text as the screen does (given --allow-file-access-from-files)."""
import json
import os
import pathlib
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
_font = os.environ.get("XY_FONT")
if _font and not pathlib.Path(_font).is_file():
    sys.exit(f"frame2svg.py: XY_FONT should be the path of UniversNextPro-Light.ttf "
             f"(the OP-XY's face, from an OP-XY checkout); {_font} is not a file")
if len(sys.argv) != 3:
    sys.exit("usage: [XY_FONT=<UniversNextPro-Light.ttf>] python frame2svg.py frame.json out.svg")
frame = json.loads(pathlib.Path(sys.argv[1]).read_text())
svg = ET.fromstring('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="1440" height="660">'
                    + (ROOT / "panel.svg.fragment").read_text() + "</svg>")
ns = "{http://www.w3.org/2000/svg}"
by_id = {e.get("id"): e for e in svg.iter() if e.get("id")}


def px(v):
    return f"{v / 1024:.4f}"


for i, (x, y, scale, cos, sin, shade, paint) in enumerate(frame["nodes"]):
    e = by_id[f"recur_node_{i}"]
    s = scale / 1024
    c, n = cos / 1024 * s, sin / 1024 * s
    e.set("transform", f"matrix({c:.4f} {n:.4f} {-n:.4f} {c:.4f} {px(x)} {px(y)})")
    grey = f"#{shade * 17:02x}{shade * 17:02x}{shade * 17:02x}"
    if paint >= 1:
        e.set("stroke", grey)
    if paint == 2:
        e.set("fill", grey)
for p, points in enumerate(frame.get("paths", [])):
    by_id[f"recur_node_{p}"].set("d", "M" + " L".join(f"{px(x)},{px(y)}" for x, y in points) + " Z")
for t, (x, y, scale, chars) in enumerate(frame["texts"]):
    e = by_id[f"recur_text_{t}"]
    e.set("transform", f"matrix({scale / 1024:.4f} 0 0 {scale / 1024:.4f} {px(x)} {px(y)})")
    e.text = chars
ET.register_namespace("", "http://www.w3.org/2000/svg")
text = ET.tostring(svg, encoding="unicode")
if _font:
    face = pathlib.Path(_font).resolve()
    text = text.replace(">", f'><style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{face}")}}</style>', 1)
pathlib.Path(sys.argv[2]).write_text(text)
