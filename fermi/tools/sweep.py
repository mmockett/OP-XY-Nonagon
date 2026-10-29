#!/usr/bin/env python3
"""Measures each control at 0, 25, 50, 75 and 100 with the others at their
defaults: a 12 s A3 note through Fermi's own render() (tools/render.cc),
with the header's caption from the screen code (tests/screen.cpp
--captions). Prints the README's control table and writes it to
<out>/sweep.md.

Each render is cut into 93 ms frames and measured as ears would hear it:
- level: RMS over the note (dBFS), and its fall by 2 s (dB);
- brightness: the spectral centroid's median and 10th-90th percentile (Hz);
- breath: the struck partial's share of the energy, frame by frame. Its
  depth is the share's 10th-90th percentile range; its period is the time
  caption's (the calibrated recurrence), quoted where the control moves it;
- even share: the energy at even harmonics of the key over all harmonics,
  where the cubic chain (odd partials only) differs from the quadratic;
- harmonic share: the energy within 25 cents of a harmonic of the key, which
  dispersion takes away; and roughness (tools/measure.py), the beating
  between partials that dispersion brings;
- change (Δ): how far each setting's long-term spectrum (third-octave bands)
  stands from the previous setting's, mean dB, so a flat stretch of a
  control shows as small steps.

    python tools/sweep.py --sdk <open-xy checkout> --out <output folder> [--only Energy,Seed]"""
import argparse
import math
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import measure  # noqa: E402

NAMES = ["Energy", "Masses", "Springs", "Time", "Dispersion", "Mode", "Damping", "Seed"]
RATE = 44100
SECONDS = 12.0
MIDI = 57
HOP, SIZE = 4096, 8192


def defaults():
    """The controls' defaults, as tests/screen.cpp's contact sheet has them."""
    text = (ROOT / "tests/screen.cpp").read_text()
    at = text.index("const double defaults[8] = {", text.index("void contact"))
    return [float(eval(v)) for v in text[at:text.index("}", at)].split("{")[1].split(",")]


def analyse(x):
    f0 = 440 * 2 ** ((MIDI - 69) / 12)
    window = np.hanning(SIZE)
    freq = np.fft.rfftfreq(SIZE, 1 / RATE)
    near = lambda k, cents: (freq > k * f0 * 2 ** (-cents / 1200)) & (freq < k * f0 * 2 ** (cents / 1200))
    key = near(1, 40)
    harmonics = [near(k, 25) for k in range(1, 40)]
    bands = [(lo, lo * 2 ** (1 / 3)) for lo in 80 * 2 ** (np.arange(22) / 3)]
    share, cent, rough, power = [], [], [], np.zeros(len(freq))
    start, stop = int(0.3 * RATE), int((SECONDS - 0.5) * RATE)
    for i in range(start, stop - SIZE, HOP):
        p = np.abs(np.fft.rfft(x[i:i + SIZE] * window)) ** 2
        total = p.sum()
        if total < 1e-9:
            continue
        power += p
        share.append(p[key].sum() / total)
        cent.append((p * freq).sum() / total)
        if len(cent) % 4 == 1:
            rough.append(measure.dissonance(measure.peaks(np.abs(np.fft.rfft(x[i:i + measure.N] * measure.WINDOW)) ** 2)[:24]))
    seg = x[start:stop]
    out = {"level": 10 * math.log10(np.mean(seg ** 2) + 1e-20)}
    early = np.mean(x[int(0.35 * RATE):int(0.85 * RATE)] ** 2) + 1e-20
    late = np.mean(x[int(1.85 * RATE):int(2.35 * RATE)] ** 2) + 1e-20
    out["fall"] = 10 * math.log10(late / early)
    out["silent"] = len(cent) < 4
    if out["silent"]:
        return out
    cent, share = np.array(cent), np.array(share)
    out["centroid"] = (float(np.median(cent)), float(np.percentile(cent, 10)), float(np.percentile(cent, 90)))
    out["depth"] = float(np.percentile(share, 90) - np.percentile(share, 10))
    harmonic = sum(power[h].sum() for h in harmonics)
    out["even"] = sum(power[h].sum() for h in harmonics[1::2]) / (harmonic + 1e-20)
    out["harmonic"] = harmonic / (power.sum() + 1e-20)
    out["rough"] = float(np.median(rough))
    out["bands"] = np.array([10 * math.log10(power[(freq >= lo) & (freq < hi)].sum() + 1e-12) for lo, hi in bands])
    return out


def change(a, b):
    """The mean difference in dB between two long-term spectra, each against its loudest band, over bands within 50 dB of it."""
    if a is None or a.get("silent") or b.get("silent"):
        return float("nan")
    x, y = a["bands"] - a["bands"].max(), b["bands"] - b["bands"].max()
    keep = (x > -50) | (y > -50)
    return float(np.mean(np.abs(np.maximum(x, -50) - np.maximum(y, -50))[keep]))


def cell(caption, period, m, previous, name):
    if m.get("silent"):
        return f"`{caption}` silent by 2 s"
    c = m["centroid"]
    parts = [f"`{caption}` {c[0]:.0f} Hz ({c[1]:.0f}–{c[2]:.0f})"]
    if m["depth"] > 0.04:
        parts.append(f"breath {m['depth']:.2f}" + (f" every {period}" if name in ("Masses", "Springs", "Mode") else ""))
    else:
        parts.append("still")
    if name == "Springs":
        parts.append(f"even {m['even']:.2f}")
    if name == "Dispersion":
        parts.append(f"harmonic {m['harmonic']:.2f}, rough {m['rough']:.3f}")
    if abs(m["fall"]) > 1:
        parts.append(f"{m['fall']:+.0f} dB by 2 s")
    if previous is not None:
        parts.append(f"Δ {change(previous, m):.1f} dB")
    return ", ".join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--only", help="comma-separated controls to sweep, e.g. Energy,Seed")
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
        key, *values = line.split("\t")
        k, v = key.split()
        captions[(int(k), int(v))] = values
    base = defaults()
    only = set(args.only.split(",")) if args.only else None
    rows = ["| Control | 0 | 25 | 50 | 75 | 100 |", "| --- | --- | --- | --- | --- | --- |"]
    for k, name in enumerate(NAMES):
        if only and name not in only:
            continue
        cells, previous = [], None
        for v in range(5):
            controls = list(base)
            controls[k] = v * 0.25
            scene = work / "scene.txt"
            scene.write_text(f"{SECONDS} 0.01 1.0\n{' '.join(f'{c:.5f}' for c in controls)}\n0.1 {SECONDS - 0.4} {MIDI}\n")
            wav = out / f"{name.lower()}-{v * 25}.wav"
            subprocess.run([str(work / "render"), str(scene), str(wav)], check=True)
            m = analyse(measure.load(wav))
            cells.append(cell(captions[(k, v)][k], captions[(k, v)][3], m, previous, name))
            previous = m
        rows.append(f"| {name} | " + " | ".join(cells) + " |")
        print(rows[-1], flush=True)
    (out / "sweep.md").write_text("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
