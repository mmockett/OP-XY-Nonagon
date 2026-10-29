#!/usr/bin/env python3
"""Makes docs/mach.gif: the screen through a scripted session, as the OP-XY
would draw it, at 720 x 330 and 12 frames a second (as Hexagon's GIFs are).
tests/screen.cpp prints the frames, tools/frame2svg.py applies them to the
artwork, headless Chrome rasterises each and ffmpeg assembles the GIF. Not
part of the machine.
  XY_FONT=<path> python tools/make_gif.py <screen binary> <scenario> <seconds> <out.gif>"""
import concurrent.futures
import json
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from frame2svg import apply  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
binary, scenario, seconds, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), pathlib.Path(sys.argv[4])
fps = 12
times = [f"{i / fps:.4f}" for i in range(int(seconds * fps))]
result = subprocess.run([binary, scenario, "60"] + times, capture_output=True, text=True, check=True)
frames = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
work = pathlib.Path(tempfile.mkdtemp(prefix="mach-gif-"))


def shoot(i_frame):
    i, frame = i_frame
    svg = apply(frame).replace('width="480" height="220"', 'width="720" height="330"')
    page = work / f"f{i:04d}.html"
    page.write_text('<html><body style="margin:0;background:#000">' + svg + "</body></html>")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=2000", f"--screenshot={work / f'f{i:04d}.png'}",
                    "--window-size=720,330", page.as_uri()], capture_output=True)


with concurrent.futures.ThreadPoolExecutor(6) as pool:
    list(pool.map(shoot, enumerate(frames)))
subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(fps), "-i", str(work / "f%04d.png"),
                "-vf", "split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none", str(out)], check=True)
print(out, len(frames), "frames")
