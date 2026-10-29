#!/usr/bin/env python3
"""Measures rendered WAVs for level, DC and clicks: python measure.py a.wav b.wav ...

A click is a sample whose second difference stands far above that of the 10 ms
around it (excluding itself): the score is that ratio, and steady noise rarely
passes 6. Scores are reported away from the file's own 3 ms fade in."""
import sys
import wave

import numpy as np

for path in sys.argv[1:]:
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    peak = 20 * np.log10(np.abs(x).max() + 1e-12)
    rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
    dc = x.mean()
    d = np.abs(np.diff(x, 2))
    window = 441
    energy = np.convolve(d ** 2, np.ones(window), 'same') - d ** 2
    local = np.sqrt(energy / (window - 1)) + 1e-9
    score = d / local
    score[:300] = 0
    top = np.argsort(score)[::-1][:3]
    print(f"{path.split('/')[-1]:22s} peak {peak:6.1f} dBFS  rms {rms:6.1f} dBFS  dc {dc:+.5f}  "
          f"clicks>8: {int((score > 8).sum()):3d}  worst {score[top[0]]:5.1f} at {top[0] / 44100:.3f}s")
