#!/usr/bin/env python3
"""Makes docs/controls.png, the contact sheet: tools/frames.cpp's sheet mode
dumps each control at 0, 50 and 100 (the others at their defaults, a note
held) and the empty state; frame2svg.py draws each at 1:1, the device's
480 x 220 pixels, and headless Chrome rasterises the grid.
    make_sheet.py <frames binary> <work folder>
frame2svg.py needs XY_FONT, the path of UniversNextPro-Light.ttf."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
binary, work = sys.argv[1], pathlib.Path(sys.argv[2])
work.mkdir(parents=True, exist_ok=True)
subprocess.run([binary, "sheet", str(work)], check=True)
rows = [(n, f"sheet-{n}") for n in ("ring", "voice", "split", "trade", "beat", "orbit", "near", "bed")]
rows.append(("no sample", "sheet-empty"))
cells = []
for label, stem in rows:
    cells.append(f'<div class="label">{label}</div>')
    for v in (0, 50, 100):
        frame = work / f"{stem}-{v}.json"
        svg = frame.with_suffix(".svg")
        subprocess.run([sys.executable, str(ROOT / "tools/frame2svg.py"), str(frame), str(svg), "1"], check=True)
        cells.append(f'<div>{svg.read_text()}</div>')
head = "".join(f'<div class="label top">{t}</div>' for t in ("", "0", "50", "100"))
html = f"""<html><head><style>
body {{ margin: 0; background: #1a1a1a; color: #bbb; font: 14px Helvetica, sans-serif; }}
.grid {{ display: grid; grid-template-columns: 90px 480px 480px 480px; gap: 6px; padding: 8px; }}
.label {{ display: flex; align-items: center; justify-content: flex-end; padding-right: 8px; }}
.top {{ justify-content: center; height: 20px; }}
</style></head><body><div class="grid">{head}{''.join(cells)}</div></body></html>"""
page = work / "sheet.html"
page.write_text(html)
height = 8 + 26 + len(rows) * 226 + 8
subprocess.run(["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=500",
                f"--window-size={90 + 3 * 480 + 4 * 6 + 16},{height}", f"--screenshot={ROOT / 'docs/controls.png'}", page.as_uri()],
               check=True, capture_output=True)
print("docs/controls.png")
