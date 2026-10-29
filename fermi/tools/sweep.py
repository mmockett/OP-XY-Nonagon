#!/usr/bin/env python3
"""Measures each control at 0, 25, 50, 75 and 100 with the others at their
defaults: an 8 s A3 note through Fermi's own render() (tools/render.cc),
measured by tools/measure.py, with the header's caption from the screen code
(tests/screen.cpp --captions). Prints the README's control table.

    python tools/sweep.py --sdk <open-xy checkout> --out <output folder>"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import measure  # noqa: E402

NAMES = ["Energy", "Masses", "Springs", "Time", "Dispersion", "Mode", "Damping", "Seed"]
DEFAULTS = [0.5, 2.5 / 7, 0, 0.22, 0.2, 0.125, 0, 0.5 / 64]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="fermi-sweep-"))
    inc = f"-I{args.sdk}/sdk/include"
    subprocess.run(["c++", "-std=c++11", "-O2", inc, f"-I{ROOT}", f'-DENGINE="{ROOT}/engine.cpp"', "-DNS=fermi",
                    str(ROOT / "tools/render.cc"), "-o", str(work / "render")], check=True)
    subprocess.run(["c++", "-std=c++11", "-O2", inc, str(ROOT / "tests/screen.cpp"), "-o", str(work / "screen")], check=True)
    captions = {}
    for line in subprocess.run([str(work / "screen"), "--captions"], check=True, capture_output=True, text=True).stdout.splitlines():
        k, v, c = line.split(" ", 2)
        captions[(int(k), int(v))] = c
    rows = ["| Control | 0 | 25 | 50 | 75 | 100 |", "| --- | --- | --- | --- | --- | --- |"]
    for k, name in enumerate(NAMES):
        cells = []
        for v in range(5):
            controls = list(DEFAULTS)
            controls[k] = v * 0.25
            scene = work / "scene.txt"
            scene.write_text(f"8.0 0.01 1.0\n{' '.join(f'{c:.5f}' for c in controls)}\n0.1 7.4 57\n")
            wav = out / f"{name.lower()}-{v * 25}.wav"
            subprocess.run([str(work / "render"), str(scene), str(wav)], check=True)
            m = measure.measure(wav, [(0.1, 7.4, 57)])
            c = m["centroid"]
            x = measure.load(wav)
            early = (x[int(0.35 * 44100):int(0.85 * 44100)] ** 2).mean()
            late = (x[int(1.85 * 44100):int(2.35 * 44100)] ** 2).mean() + 1e-20
            drop = 10 * __import__("math").log10(late / early)
            fade = f", {drop:+.0f} dB by 2 s" if abs(drop) > 1 else ""
            cells.append(f"`{captions[(k, v)]}` {c[0]:.0f} Hz ({c[1]:.0f}–{c[2]:.0f}){fade}")
        rows.append(f"| {name} | " + " | ".join(cells) + " |")
    table = "\n".join(rows) + "\n"
    (out / "sweep.md").write_text(table)
    print(table)


if __name__ == "__main__":
    main()
