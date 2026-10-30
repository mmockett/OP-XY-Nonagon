#!/usr/bin/env python3
"""Makes the contact sheets: tools/frames.cpp's sheet mode dumps each control
at 0, 50 and 100 (the others at their defaults, a sample loaded, a note
held), then no sample: the hint, with bed at 0, 50 and 100. frame2svg.py
draws each, and headless Chrome rasterises the grid twice: docs/controls-1x.png
at 1:1, the device's 480 x 220 pixels, and docs/controls.png at twice that,
for reading.
    make_sheet.py <frames binary> <work folder>
frame2svg.py needs XY_FONT, the path of UniversNextPro-Light.ttf."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
binary, work = sys.argv[1], pathlib.Path(sys.argv[2])
work.mkdir(parents=True, exist_ok=True)
subprocess.run([binary, "sheet", str(work)], check=True)
rows = [(n, f"sheet-{n}") for n in ("ring", "voice", "split", "trade", "beat", "orbit", "near")]
rows += [("bed", "sheet-bed"), ("no sample", "sheet-empty")]
for scale, name in ((1, "controls-1x.png"), (2, "controls.png")):
    cells = []
    for label, stem in rows:
        cells.append(f'<div class="label">{label}</div>')
        for v in (0, 50, 100):
            frame = work / f"{stem}-{v}.json"
            svg = work / f"{stem}-{v}-{scale}.svg"
            subprocess.run([sys.executable, str(ROOT / "tools/frame2svg.py"), str(frame), str(svg), str(scale)], check=True)
            cells.append(f'<div>{svg.read_text()}</div>')
    head = "".join(f'<div class="label top">{t}</div>' for t in ("", "0", "50", "100"))
    w, h = 480 * scale, 220 * scale
    html = f"""<html><head><style>
body {{ margin: 0; background: #1a1a1a; color: #bbb; font: {12 * scale}px UniversNextPro-Light; }}
.grid {{ display: grid; grid-template-columns: {100 * scale}px {w}px {w}px {w}px; gap: {6 * scale}px; padding: {8 * scale}px; }}
.label {{ display: flex; align-items: center; justify-content: flex-end; padding-right: {8 * scale}px; text-align: right; }}
.top {{ justify-content: center; height: {20 * scale}px; }}
</style></head><body><div class="grid">{head}{''.join(cells)}</div></body></html>"""
    page = work / f"sheet-{scale}.html"
    page.write_text(html)
    width = 100 * scale + 3 * w + 3 * 6 * scale + 16 * scale
    height = 16 * scale + 26 * scale + len(rows) * (h + 6 * scale)
    subprocess.run(["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=500",
                    f"--window-size={width},{height}", f"--screenshot={ROOT / 'docs' / name}", page.as_uri()],
                   check=True, capture_output=True)
    print("docs/" + name)
