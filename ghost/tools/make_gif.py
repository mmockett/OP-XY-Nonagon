#!/usr/bin/env python3
"""Makes docs/ghost.gif: tools/frames.cpp's gif mode dumps 12 frames a second
of the screen, frame2svg.py draws each, headless Chrome rasterises them at
720 x 330 and ffmpeg assembles them.
    make_gif.py <frames binary> <work folder>
frame2svg.py needs XY_FONT, the path of UniversNextPro-Light.ttf."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
binary, work = sys.argv[1], pathlib.Path(sys.argv[2])
work.mkdir(parents=True, exist_ok=True)
for old in work.glob("frame-*"):
    old.unlink()
subprocess.run([binary, "gif", str(work)], check=True)
chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
for frame in sorted(work.glob("frame-*.json")):
    svg = frame.with_suffix(".svg")
    subprocess.run([sys.executable, str(ROOT / "tools/frame2svg.py"), str(frame), str(svg), "1.5"], check=True)
    subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=500", "--window-size=720,330",
                    f"--screenshot={frame.with_suffix('.png')}", svg.as_uri()], check=True, capture_output=True)
subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", "12", "-i", str(work / "frame-%04d.png"),
                "-vf", "split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none",
                str(ROOT / "docs/ghost.gif")], check=True)
print("docs/ghost.gif")
