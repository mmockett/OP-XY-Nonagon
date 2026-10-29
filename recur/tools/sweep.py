#!/usr/bin/env python3
"""Measures tools/render.cpp's sweep: each control at 0, 25, 50, 75 and 100 over
the Recur preset on the drums, against the window unscrambled. Prints a
Markdown table: python sweep.py <folder with sweep-*.wav and source-drums.wav>

distance: the RMS difference from the unscrambled window, over the level, in
  bars after the first (for step, the first bar: where a note starts);
  0 the recording, about 1.4 unrelated;
grid: the share of onsets within 15 ms of the 1/16 grid;
peak: dBFS of the render (one note; half scale is -6)."""
import pathlib
import sys
import wave

import numpy as np

folder = pathlib.Path(sys.argv[1])
RATE, BAR = 44100, 88200
names = ["pos", "grid", "slice", "map", "step", "rate", "depth", "chrd"]
grids, slices = [2, 4, 8], [0, 1, 2, 4, 8, 16]
slice_names = ["hiss", "1/64", "1/32", "1/16", "1/8", "1/4"]
maps = ["cat", "baker", "gold", "pell", "shear", "hex"]
rates = ["off", "4 bar", "2 bar", "1 bar", "1/2", "1/4", "1/8", "1/16"]
chords = ["off", "dtun", "oct", "5th", "maj", "min", "harm"]
preset = [0, 0.5, 0.583333, 0.083333, 0, 0.4375, 0.7, 0.071429]


def load(path):
    with wave.open(str(path)) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768


def pick(v, n):
    """The engine's choice: the control as Q31, its top 16 bits, times n."""
    return ((int(v * 2147483647) >> 15) * n) >> 16


def onsets(x, hop=256, n=1024):
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n, hop)]
    mags = np.array([np.abs(np.fft.rfft(f)) for f in frames])
    flux = np.maximum(0, np.diff(mags, axis=0)).sum(axis=1)
    threshold = np.median(flux) + 2 * np.std(flux)
    peaks = [i for i in range(1, len(flux) - 1) if flux[i] > threshold and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]]
    return (np.array(peaks) + 1) * hop / RATE + n / 2 / RATE - hop / RATE


def on_grid(t, grid=0.125):
    return float(np.mean(np.abs((t + grid / 2) % grid - grid / 2) < 0.015)) if len(t) else float('nan')


source = load(folder / "source-drums.wav")
span = len(source)
print("| control | 0 | 25 | 50 | 75 | 100 |")
print("| --- | --- | --- | --- | --- | --- |")
for c, name in enumerate(names):
    cells = []
    for v in range(5):
        values = preset[:]
        values[c] = v * 0.25
        out = load(folder / f"sweep-{c}-{v * 25}.wav") * 2
        g, sl = grids[pick(values[1], 3)], pick(values[2], 6)
        home = int(values[0] * span)
        if sl == 0:   # hiss: the window of (16 g)^2 samples, looped
            size = (16 * g) ** 2
            home = int(values[0] * (span - size + 1))
            window = source[home:home + size]
        else:   # the window from the start to ending at the end; longer, it wraps and pos turns it
            size = g * g * slices[sl] * 1378.125
            room = span - size if span > size else span - slices[sl] * 1378.125
            home = int(values[0] * room)
            idx = (home + np.arange(int(size))) % span
            window = source[idx]
        unscrambled = np.tile(window, len(out) // max(1, len(window)) + 2)[:len(out)]
        ds = []
        for b in range(1, len(out) // BAR):
            p, o = out[b * BAR:(b + 1) * BAR], unscrambled[b * BAR:(b + 1) * BAR]
            k = np.sqrt(np.mean(p ** 2)) / (np.sqrt(np.mean(o ** 2)) + 1e-12)
            ds.append(np.sqrt(np.mean((p - o * k) ** 2)) / (np.sqrt(np.mean(p ** 2)) + 1e-12))
        caption = {"pos": f"{home / RATE:.2f}s", "grid": f"{g}x{g}", "slice": slice_names[sl],
                   "map": maps[pick(values[3], 6)], "step": f"{v * 25}", "rate": rates[pick(values[5], 8)],
                   "depth": f"{v * 25}%", "chrd": chords[pick(values[7], 7)]}[name]
        peak = 20 * np.log10(np.abs(out).max() / 2 + 1e-12)
        if name == "step":   # where a note starts: the first bar
            p, o = out[:BAR], unscrambled[:BAR]
            k = np.sqrt(np.mean(p ** 2)) / (np.sqrt(np.mean(o ** 2)) + 1e-12)
            ds = [np.sqrt(np.mean((p - o * k) ** 2)) / (np.sqrt(np.mean(p ** 2)) + 1e-12)]
            position = round(values[4] * 2 * 10) / 10   # step runs from iteration 0 to the last (2 of 3)
            caption = f"{position:g}/3"
        cells.append(f"{caption}: {np.mean(ds):.2f}, {on_grid(onsets(out)):.2f}, {peak:.1f}")
    print(f"| **{name}** | " + " | ".join(cells) + " |")
