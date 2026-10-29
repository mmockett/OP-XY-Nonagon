#!/usr/bin/env python3
"""Lays frames written by tools/screen.cpp (sheet and prints modes) out as
contact sheets and shoots them with headless Chrome, in the device's face:
  XY_FONT=<path to UniversNextPro-Light.ttf> python tools/sheet.py <frames folder> <docs folder>
writes controls-1x.png (every frame at 1:1, 480 x 220, as the screen shows
it), controls.png (the same at 2x, to read the detail) and prints.png (each
map's print at rest and scrambled on each grid, at 1:1). The face is the
device's own, from an OP-XY checkout, loaded from where XY_FONT says it
lives; it is never copied here."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
_font = os.environ.get("XY_FONT")
if not _font or not pathlib.Path(_font).is_file():
    sys.exit("sheet.py: set XY_FONT to the path of UniversNextPro-Light.ttf "
             "(the OP-XY's face, from an OP-XY checkout)" + (f"; {_font} is not a file" if _font else ""))
if len(sys.argv) != 3:
    sys.exit("usage: XY_FONT=<UniversNextPro-Light.ttf> python tools/sheet.py <frames folder> <docs folder>")
font = pathlib.Path(_font).resolve()
frames, docs = (pathlib.Path(a).resolve() for a in sys.argv[1:3])
ET.register_namespace("", "http://www.w3.org/2000/svg")
ARTWORK = (ROOT / "panel.svg.fragment").read_text()


def svg(name, scale):
    """A frame applied to the artwork, as the renderers do, sized scale x."""
    frame = json.loads((frames / f"{name}.json").read_text())
    root = ET.fromstring(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="{480 * scale}" '
                         f'height="{220 * scale}">' + ARTWORK + "</svg>")
    by_id = {e.get("id"): e for e in root.iter() if e.get("id")}
    for i, (x, y, s, cos, sin, shade, paint) in enumerate(frame["nodes"]):
        e = by_id[f"recur_node_{i}"]
        c, n = cos / 1024 * s / 1024, sin / 1024 * s / 1024
        e.set("transform", f"matrix({c:.4f} {n:.4f} {-n:.4f} {c:.4f} {x / 1024:.4f} {y / 1024:.4f})")
        grey = f"#{shade * 17:02x}" * 1 + f"{shade * 17:02x}{shade * 17:02x}"
        if paint >= 1:
            e.set("stroke", grey)
        if paint == 2:
            e.set("fill", grey)
    for t, (x, y, s, chars) in enumerate(frame["texts"]):
        e = by_id[f"recur_text_{t}"]
        e.set("transform", f"matrix({s / 1024:.4f} 0 0 {s / 1024:.4f} {x / 1024:.4f} {y / 1024:.4f})")
        e.text = chars
    return ET.tostring(root, encoding="unicode").replace("ns0:", "")


def page(title, rows, columns, scale):
    width = 480 * scale
    cells = [f'<div class="head"></div>'] + [f'<div class="head">{c}</div>' for c in columns]
    for label, names in rows:
        cells.append(f'<div class="label">{label}</div>')
        for name in names:
            cells.append(svg(name, scale) if name and (frames / f"{name}.json").exists() else f'<div class="note">{name or ""}</div>')
        cells += ['<div></div>'] * (len(columns) - len(names))
    return (f'<!doctype html><style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{font}")}}'
            f'body{{background:#161616;margin:0;font:13px Helvetica,sans-serif;color:#bbb}}'
            f'.grid{{display:grid;grid-template-columns:70px repeat({len(columns)},{width}px);gap:8px;padding:6px 12px}}'
            f'.label{{align-self:center;font-size:15px;color:#ddd}}.note{{align-self:center}}svg{{display:block}}</style>'
            f'<body><div style="padding:10px 12px 4px">{title}</div><div class="grid">{"".join(cells)}</div></body>'), \
        70 + 24 + (width + 8) * len(columns), 40 + (220 * scale + 8) * (len(rows) + 1)


def shoot(html, size, out):
    with tempfile.TemporaryDirectory() as folder:
        path = pathlib.Path(folder) / "sheet.html"
        path.write_text(html)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                        "--force-device-scale-factor=1", f"--user-data-dir={folder}/profile", "--virtual-time-budget=3000",
                        f"--window-size={size[0]},{size[1]}", f"--screenshot={out}", f"file://{path}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


controls = ["pos", "grid", "slice", "step", "rate", "depth", "chrd"]
rows = [("map", [f"sheet-map-{v}" for v in range(6)])]
rows += [(c, [f"sheet-{c}-{v}" for v in (0, 50, 100)]) for c in controls]
rows.append(("empty", ["sheet-empty", "no sample: the header stays, a cover says how to load one"]))
columns = ["0 (map: cat)", "50 (map: baker)", "100 (map: gold)", "map: pell", "map: shear", "map: hex"]
for scale, name in ((1, "controls-1x.png"), (2, "controls.png")):
    html, w, h = page(f"Recur: each control at 0, 50 and 100 (map at each of its six), the rest at their defaults, at rest, "
                      f"{'1:1, as the screen shows it' if scale == 1 else 'at 2x'}", rows, columns[:6] if scale == 1 else columns[:3], scale)
    if scale == 2:   # at 2x, map gets a row of three and a second row of three
        two = [("map", [f"sheet-map-{v}" for v in range(3)]), ("map", [f"sheet-map-{v}" for v in range(3, 6)])]
        two += rows[1:]
        html, w, h = page("Recur: each control at 0, 50 and 100 (map at each of its six), the rest at their defaults, at rest, at 2x",
                          two, ["0 · cat / pell", "50 · baker / shear", "100 · gold / hex"], scale)
    shoot(html, (w, h), docs / name)
maps = ["cat", "baker", "gold", "pell", "shear", "hex"]
rows = [(m, [f"print-{m}-{g}-{state}" for g in (2, 4, 8) for state in ("rest", "scrambled")]) for m in maps]
html, w, h = page("Recur's six prints at 1:1, at rest and scrambled, on each grid",
                  rows, ["2x2 at rest", "2x2 scrambled", "4x4 at rest", "4x4 scrambled", "8x8 at rest", "8x8 scrambled"], 1)
shoot(html, (w, h), docs / "prints.png")
print("wrote", docs / "controls-1x.png", docs / "controls.png", docs / "prints.png")
