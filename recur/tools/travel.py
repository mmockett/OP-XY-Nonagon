#!/usr/bin/env python3
"""How much each control changes the sound across its travel, from tools/render.cpp's
sweep in steps of 5% (sweep-<control>-<percent>.wav, over the Recur preset on the drums):
  python tools/travel.py <folder>

change   the RMS difference between the render at that setting and at 0, over their
         level: 0 the same sound, about 1.4 two unrelated sounds of the same level;
spectrum the mean absolute difference of their long-term spectra, dB (timbre and level);
flat     the longest stretch of travel over which neighbouring 5% steps barely differ
         (change under 0.05 and spectrum under 0.5 dB): a dead zone."""
import pathlib
import sys
import wave

import numpy as np

folder = pathlib.Path(sys.argv[1])
names = ["pos", "grid", "slice", "map", "step", "rate", "depth", "chrd"]


def load(path):
    with wave.open(str(path)) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768


def change(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    level = np.sqrt((np.mean(a ** 2) + np.mean(b ** 2)) / 2) + 1e-12
    return float(np.sqrt(np.mean((a - b) ** 2)) / level)


def spectrum(x, n=2048):
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n, n // 2)]
    return 10 * np.log10(np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0)[4:600] + 1e-10)


def timbre(a, b):
    return float(np.mean(np.abs(spectrum(a) - spectrum(b))))


print("| control | 50: change, spectrum | 100: change, spectrum | longest flat stretch |")
print("| --- | --- | --- | --- |")
for c, name in enumerate(names):
    renders = [load(folder / f"sweep-{c}-{v}.wav") for v in range(0, 101, 5)]
    steps = [(change(renders[i], renders[i + 1]), timbre(renders[i], renders[i + 1])) for i in range(20)]
    longest = run = 0
    for d, t in steps:
        run = run + 1 if d < 0.05 and t < 0.5 else 0
        longest = max(longest, run)
    cells = [f"{change(renders[0], renders[k]):.2f}, {timbre(renders[0], renders[k]):.1f} dB" for k in (10, 20)]
    print(f"| {name} | {cells[0]} | {cells[1]} | {longest * 5}% |")
