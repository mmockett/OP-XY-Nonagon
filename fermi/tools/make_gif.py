#!/usr/bin/env python3
"""Makes docs/fermi.gif: runs tests/screen.cpp's scene (the real display code;
a breathing note, energy swept up into chaos, then a cubic chain), applies
each frame to panel.svg.fragment as the renderers do, rasterises the frames
with a headless Chrome at 720 x 330 and joins them with ffmpeg.

    python tools/make_gif.py --sdk <open-xy checkout> [--fps 10] [--chrome PATH] [--png frame-number:out.png]

Needs a host C++ compiler, Google Chrome and ffmpeg. The frames are the
display's own, lettered in the device's face, UniversNextPro-Light: pass its
.ttf with --font, or set XY_FONT to its path."""
import argparse, copy, json, os, pathlib, subprocess, tempfile, xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--sdk", required=True)
parser.add_argument("--fps", type=int, default=10)
parser.add_argument("--chrome", default="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
parser.add_argument("--png", action="append", default=[], help="also save frame N as a 3x PNG: N:path")
parser.add_argument("--contact", action="store_true", help="also write docs/controls.png: each control at 0, 50 and 100, at 1:1")
parser.add_argument("--no-gif", action="store_true", help="only the contact sheet and stills")
parser.add_argument("--font", default=os.environ.get("XY_FONT"),
                    help="the device's face, UniversNextPro-Light.ttf, loaded in place (never copied into the "
                         "checkout); defaults to $XY_FONT")
args = parser.parse_args()
if not args.font:
    parser.error("no font: pass --font PATH or set XY_FONT to the path of UniversNextPro-Light.ttf")
if not os.path.isfile(args.font):
    parser.error(f"font not found: {args.font}")
S = tempfile.mkdtemp(prefix="fermi-gif-")
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", str(ROOT / "tests/screen.cpp"), "-o", f"{S}/screen"], check=True)
subprocess.run([f"{S}/screen", f"{S}/frames.jsonl", str(args.fps)], check=True)
frames = [json.loads(line) for line in open(f"{S}/frames.jsonl")]
base = ET.fromstring((ROOT / "panel.svg.fragment").read_text())
W, H, CHUNK = 720, 330, 12
FACE = (f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}'
        f'text{{font-family:"UniversNextPro-Light"}}</style>')
CHROME = [args.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=3000"]


def svg(data, w=W, h=H):
    root = copy.deepcopy(base)
    by = {e.get("id"): e for e in root.iter() if e.get("id")}
    pal = [(by[f"fermi_palette_{i}"].get("fill"), by[f"fermi_palette_{i}"].get("stroke"), by[f"fermi_palette_{i}"].get("stroke-width"))
           for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data["nodes"]):
        e = by.get(f"fermi_node_{i}")
        if e is None:
            continue
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set("transform", f"matrix({a} {b} {-b} {a} {x / 1024} {y / 1024})")
        if paint in (1, 2):
            e.set("stroke", pal[shade][1])
            e.set("stroke-width", pal[shade][2])
            if paint == 2 and e.get("fill") not in (None, "none"):
                e.set("fill", pal[shade][0])
    for p, pts in enumerate(data["paths"]):
        by[f"fermi_node_{p}"].set("d", "M" + " L".join(f"{pts[2 * i] / 1024},{pts[2 * i + 1] / 1024}" for i in range(len(pts) // 2)) + " Z")
    for t, (x, y, s, chars) in enumerate(data["texts"]):
        e = by[f"fermi_text_{t}"]
        e.set("transform", f"matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})")
        e.text = chars
    body = ET.tostring(root, encoding="unicode").replace("ns0:", "").replace(":ns0", "")
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="{w}" height="{h}" style="display:block">'
            f'<rect width="480" height="220" fill="#000"/>{body}</svg>')


def shoot(svgs, w, h, out):
    html = f"{S}/chunk.html"
    open(html, "w").write('<html><head>' + FACE + '</head><body style="margin:0;background:#000">' + "".join(svgs) + "</body></html>")
    subprocess.run(CHROME + [f"--window-size={w},{h * len(svgs)}",
                    f"--screenshot={out}", f"file://{html}"], check=True, capture_output=True)


if args.contact:
    subprocess.run([f"{S}/screen", "--contact", f"{S}/contact.jsonl"], check=True)
    sheet = [json.loads(line) for line in open(f"{S}/contact.jsonl")]
    names = ["energy", "masses (8, 20, 32)", "springs", "time", "dispersion", "mode (1, 2, 4)", "damping", "seed (off, 32, 63)"]
    cw, ch, label = 480, 220, 150
    cells = ['<div style="display:flex;margin-left:%dpx">' % label + "".join(
        f'<div style="width:{cw}px;color:#aaa;font:15px Helvetica;text-align:center;padding:6px 0">{h}</div>'
        for h in ("0", "50", "100")) + "</div>"]
    for k in range(8):
        row = "".join(f'<div style="width:{cw}px;height:{ch}px;border:1px solid #222;box-sizing:border-box">{svg(sheet[3 * k + v], cw, ch)}</div>'
                      for v in range(3))
        cells.append(f'<div style="display:flex;align-items:center"><div style="width:{label}px;color:#ddd;font:16px Helvetica;'
                     f'padding-left:10px;box-sizing:border-box">{names[k]}</div>{row}</div>')
    height = 30 + 8 * ch
    html = f"{S}/contact.html"
    open(html, "w").write('<html><head>' + FACE + '</head><body style="margin:0;background:#000">' + "".join(cells) + "</body></html>")
    out_png = str(ROOT / "docs/controls.png")
    subprocess.run(CHROME + [f"--window-size={label + 3 * cw},{height}",
                    "--force-device-scale-factor=1", f"--screenshot={out_png}", f"file://{html}"], check=True, capture_output=True)
    print("wrote", out_png)

for spec in args.png:
    n, path = spec.split(":", 1)
    shoot([svg(frames[int(n)], 1440, 660)], 1440, 660, path)
    print("wrote", path)
if args.no_gif:
    raise SystemExit
os.makedirs(f"{S}/png", exist_ok=True)
n = 0
for start in range(0, len(frames), CHUNK):
    chunk = frames[start:start + CHUNK]
    shot = f"{S}/chunk.png"
    shoot([svg(d) for d in chunk], W, H, shot)
    for k in range(len(chunk)):
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", shot, "-vf", f"crop={W}:{H}:0:{k * H}", f"{S}/png/f{n:04d}.png"], check=True)
        n += 1
out = str(ROOT / "docs/fermi.gif")
subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", str(args.fps), "-i", f"{S}/png/f%04d.png", "-vf",
                "split[a][b];[a]palettegen=max_colors=32:reserve_transparent=0[p];[b][p]paletteuse=dither=none", "-loop", "0", out], check=True)
print(n, "frames ->", out)
