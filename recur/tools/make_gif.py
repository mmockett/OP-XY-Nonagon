#!/usr/bin/env python3
"""Makes docs/recur.gif: tools/screen.cpp's session, every 4th frame of its
60 a second (15 a second), on samples/break.wav; frame2svg.py draws each,
headless Chrome rasterises them at 720 x 330 and ffmpeg assembles them.
    XY_FONT=<path to UniversNextPro-Light.ttf> make_gif.py <screen binary> <work folder>
XY_FONT is the OP-XY's face, from an OP-XY checkout; it is never copied here."""
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
_font = os.environ.get("XY_FONT")
if not _font or not pathlib.Path(_font).is_file():
    sys.exit("make_gif.py: set XY_FONT to the path of UniversNextPro-Light.ttf "
             "(the OP-XY's face, from an OP-XY checkout)" + (f"; {_font} is not a file" if _font else ""))
if len(sys.argv) != 3:
    sys.exit("usage: XY_FONT=<UniversNextPro-Light.ttf> make_gif.py <screen binary> <work folder>")
binary, work = sys.argv[1], pathlib.Path(sys.argv[2]).resolve()
work.mkdir(parents=True, exist_ok=True)
for old in work.glob("gif-*"):
    old.unlink()
subprocess.run([binary, str(ROOT / "samples/break.wav"), str(work), "4"], check=True)


def whole(png):
    """Chrome now and then captures before it has drawn the lower part, which
    it leaves white; the screen's bottom rows are near black."""
    rows = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(png), "-vf", "crop=iw:6:0:ih-6", "-f", "rawvideo",
                           "-pix_fmt", "gray", "-"], check=True, capture_output=True).stdout
    return sum(rows) / len(rows) < 100


def shoot(frame):
    svg, png = frame.with_suffix(".svg"), frame.with_suffix(".png")
    subprocess.run([sys.executable, str(ROOT / "tools/frame2svg.py"), str(frame), str(svg)], check=True)
    svg.write_text(svg.read_text().replace('width="1440" height="660"', 'width="720" height="330"', 1))
    for _ in range(4):   # a launch now and then fails, hangs or captures half drawn: try again
        with tempfile.TemporaryDirectory() as profile:
            try:
                done = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                                       "--force-device-scale-factor=1", f"--user-data-dir={profile}", "--virtual-time-budget=1000",
                                       "--window-size=720,330", f"--screenshot={png}", svg.as_uri()],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
            except subprocess.TimeoutExpired:
                continue
        if done.returncode == 0 and png.exists() and whole(png):
            return
    sys.exit(f"make_gif.py: Chrome failed to capture {png.name} whole")


with concurrent.futures.ThreadPoolExecutor(4) as pool:
    list(pool.map(shoot, sorted(work.glob("gif-*.json"))))
subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-framerate", "15", "-i", str(work / "gif-%04d.png"),
                "-vf", "split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none",
                str(ROOT / "docs/recur.gif")], check=True)
print("docs/recur.gif")
