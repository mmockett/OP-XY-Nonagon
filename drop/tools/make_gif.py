#!/usr/bin/env python3
"""Makes docs/drop.gif: runs tests/screen.cpp's "gif" scene (the real display
code at 60 frames a second, every fifth frame kept), applies each frame to
panel.svg.fragment as the renderers do (tools/frame2svg.py), rasterises them
with a headless Chrome at 2x and joins them at 12 frames a second with ffmpeg.

    python tools/make_gif.py --sdk ../open-xy [--chrome PATH] [--font UniversNextPro-Light.ttf]

Needs a host C++ compiler, Google Chrome and ffmpeg. The frames are the
display's own; fonts are the browser's."""
import argparse
import pathlib
import subprocess
import sys
import tempfile
import json

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import frame2svg  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--sdk", required=True)
parser.add_argument("--chrome", default="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
parser.add_argument("--font")
parser.add_argument("--out", default=str(ROOT / "docs" / "drop.gif"))
args = parser.parse_args()

work = pathlib.Path(tempfile.mkdtemp(prefix="drop-gif-"))
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", str(ROOT / "tests" / "screen.cpp"),
                "-o", str(work / "screen")], check=True)
lines = subprocess.run([str(work / "screen"), "gif", "60", "every", "5"], check=True, capture_output=True,
                       text=True).stdout.splitlines()
for n, line in enumerate(lines):
    svg = frame2svg.apply(json.loads(line), args.font)
    page = work / f"f{n:04d}.html"
    page.write_text('<html><body style="margin:0;background:#000">' + svg.replace('width="480" height="220"', 'width="960" height="440"')
                    + "</body></html>")
    subprocess.run([args.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--window-size=960,440",
                    "--allow-file-access-from-files", "--virtual-time-budget=1000",
                    f"--screenshot={work / f'f{n:04d}.png'}", page.as_uri()], check=True, capture_output=True)
    print(f"\rframe {n + 1}/{len(lines)}", end="", flush=True)
print()
pathlib.Path(args.out).parent.mkdir(exist_ok=True)
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "12", "-i", str(work / "f%04d.png"), "-vf",
                "scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none",
                args.out], check=True)
print("wrote", args.out)
