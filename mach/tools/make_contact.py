#!/usr/bin/env python3
"""Makes a contact sheet: the screen with each control at 0, 50 and 100 and
the others at the Init sound's, one row per control, as the OP-XY would draw
it at rest: at 1:1 (480 x 222 a frame, docs/controls-1x.png), as the screen
shows it, or enlarged (docs/controls.png at 2) to look closely. tests/screen.cpp's still scenario prints each frame,
tools/frame2svg.py applies it to the artwork and headless Chrome rasterises
the sheet. Not part of the machine.
  XY_FONT=<path> python tools/make_contact.py <screen binary> <out.png> [scale]"""
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from frame2svg import apply  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
NAMES = ["mach", "pass", "source", "dur", "boom", "air", "tune", "echo"]
binary, out = sys.argv[1], pathlib.Path(sys.argv[2])
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
rows = []
for c, name in enumerate(NAMES):
    cells = []
    for value in (0.0, 0.5, 1.0):
        # The enum control at its first, middle and last option.
        v = {0.0: 0.1, 0.5: 0.5, 1.0: 0.9}[value] if name == "source" else value
        result = subprocess.run([binary, f"still:{c}:{v}", "60", "1.4"], capture_output=True, text=True, check=True)
        frame = json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))
        svg = apply(frame).replace('height="220" viewBox="0 0 480 220"', 'height="222" viewBox="0 0 480 222"')
        cells.append(f'<td><div class="cell">{svg}</div><div class="v">{int(value * 100)}</div></td>')
    rows.append(f'<tr><th>{name}</th>{"".join(cells)}</tr>')
style = ("body{margin:0;background:#111;color:#ccc;font:600 15px -apple-system,Helvetica,sans-serif}"
         "table{border-spacing:10px 6px;padding:6px}th{text-align:right;padding-right:6px;width:64px;color:#ddd}"
         "td{vertical-align:top}.cell svg{display:block;width:480px;height:220px}.v{text-align:center;color:#888;font-size:12px}")
page = f'<html><head><style>{style}</style></head><body><table>{"".join(rows)}</table></body></html>'
work = pathlib.Path(tempfile.mkdtemp(prefix="mach-contact-"))
(work / "sheet.html").write_text(page)
width, height = 3 * 490 + 100, 8 * 250 + 20
subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=3000",
                f"--force-device-scale-factor={scale}",
                f"--screenshot={out.resolve()}", f"--window-size={width},{height}", (work / "sheet.html").as_uri()],
               capture_output=True, check=True)
print(out)
