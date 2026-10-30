#!/usr/bin/env python3
"""Makes docs/controls-1x.png, a contact sheet of Drop's screen at its true
480 x 222 pixels (judge legibility on this one), and docs/controls.png, the
same drawn twice as large to read: one row per
control at 0, 50 and 100 with the others at their defaults, each frame 1.8 s
into tests/screen.cpp's "contact" scene (a drop landed 1.5 s before, another
0.2 s before), applied to panel.svg.fragment as the renderers do and
rasterised with a headless Chrome.

    python tools/make_contact.py --sdk ../open-xy [--chrome PATH] [--font UniversNextPro-Light.ttf]

--font loads the device's own face for the header (the OP-XY carries it as
content/display/UniversNextPro-Light.otf); without it Chrome falls back."""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import frame2svg  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--sdk", required=True)
parser.add_argument("--chrome", default="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
parser.add_argument("--font")
args = parser.parse_args()

work = pathlib.Path(tempfile.mkdtemp(prefix="drop-contact-"))
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", str(ROOT / "tests" / "screen.cpp"),
                "-o", str(work / "screen")], check=True)
definition = json.loads((ROOT / "definition.json").read_text())
names = [p["label"].lower() for p in definition["parameters"]]
rows = []
for k, name in enumerate(names):
    cells = []
    for value in (0.0, 0.5, 1.0):
        line = subprocess.run([str(work / "screen"), "contact", str(k), str(value)], check=True, capture_output=True,
                              text=True).stdout.strip().splitlines()[-1]
        svg = frame2svg.apply(json.loads(line), args.font)
        cells.append(f'<td style="width:480px;height:220px;padding:0">{svg}</td>')   # inline, so it can load the font
    rows.append(f'<tr><th>{name}</th>{"".join(cells)}</tr>')
page = work / "sheet.html"
page.write_text('<html><body style="margin:0;background:#111;color:#ccc;font:16px UniversNextPro-Light">'
                '<table cellspacing="6"><tr><th></th><th>0</th><th>50</th><th>100</th></tr>' + "".join(rows) +
                "</table></body></html>")
height = 30 + 226 * len(rows)
for name, factor in (("controls-1x.png", 1), ("controls.png", 2)):
    out = ROOT / "docs" / name
    subprocess.run([args.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                    "--virtual-time-budget=1000", f"--force-device-scale-factor={factor}", f"--window-size=1560,{height}", f"--screenshot={out}",
                    page.as_uri()], check=True, capture_output=True)
    print("wrote", out)
