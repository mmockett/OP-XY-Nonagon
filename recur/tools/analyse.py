#!/usr/bin/env python3
"""Measures a render against its source, standing in for ears:
  python analyse.py render.wav source.wav --pass SECONDS [--window SECONDS] [--slice SECONDS] [--home SECONDS] [--loop]

Per pass (an iteration's length), against the same stretch of the window
unscrambled (--loop: the window repeating, as the hiss path plays it):
  distance  RMS of (pass - original pass) over RMS of the original: 0 is the
            recording, about 1.4 two unrelated sounds of the same level;
  flatness  Welch spectral flatness (0 tonal, about 0.9 white noise);
  grid      the share of onsets within 15 ms of the 1/16 grid at 120 BPM.
Over the whole file: click-like events a second (a sample whose second
difference, at least -54 dBFS, stands 8 times above the 10 ms around it)
against the source's own rate, and the worst such score within 3 ms of a
slice seam."""
import argparse
import wave

import numpy as np


def load(path):
    with wave.open(path) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768


def flatness(x, n=1024):
    segs = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n + 1, n // 2)]
    if not segs:
        return float('nan')
    p = np.mean([np.abs(np.fft.rfft(s)) ** 2 for s in segs], axis=0)[1:n // 2] + 1e-12
    return float(np.exp(np.mean(np.log(p))) / np.mean(p))


def onsets(x, rate=44100, hop=256):
    """Onset times from spectral flux peaks."""
    n = 1024
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n, hop)]
    mags = np.array([np.abs(np.fft.rfft(f)) for f in frames])
    flux = np.maximum(0, np.diff(mags, axis=0)).sum(axis=1)
    if not len(flux):
        return np.array([])
    threshold = np.median(flux) + 2 * np.std(flux)
    peaks = [i for i in range(1, len(flux) - 1) if flux[i] > threshold and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]]
    times = (np.array(peaks) + 1) * hop / rate + n / 2 / rate - hop / rate
    return times


def on_grid(times, grid=0.125, tolerance=0.015):
    if not len(times):
        return float('nan')
    off = np.abs((times + grid / 2) % grid - grid / 2)
    return float(np.mean(off < tolerance))


def click_scores(x):
    d = np.abs(np.diff(x, 2))
    window = 441
    energy = np.convolve(d ** 2, np.ones(window), 'same') - d ** 2
    score = d / (np.sqrt(np.maximum(energy, 0) / (window - 1)) + 1e-9)
    score[d < 0.002] = 0   # below -54 dBFS a kink can't be heard
    score[:300] = 0
    return score


ap = argparse.ArgumentParser()
ap.add_argument('render')
ap.add_argument('source')
ap.add_argument('--pass', dest='pass_', type=float, required=True)
ap.add_argument('--slice', type=float, default=0)
ap.add_argument('--home', type=float, default=0)
ap.add_argument('--window', type=float, default=0)
ap.add_argument('--loop', action='store_true')
args = ap.parse_args()
out, src = load(args.render), load(args.source)
rate = 44100
length = int(args.pass_ * rate)
home = int(args.home * rate)
window = int((args.window or args.pass_) * rate)
if args.loop:
    once = src[home:home + window]
    unscrambled = np.tile(once, len(out) // window + 2)
else:
    unscrambled = np.tile(np.concatenate([src[home:], src[:home]])[:window], len(out) // window + 2)


def original_at(n):
    start = (n * length) % window
    return unscrambled[start:start + length]


level = np.sqrt(np.mean(original_at(0) ** 2)) + 1e-12
rows = []
for n in range(len(out) // length):
    p = out[n * length:(n + 1) * length] * 2   # the render peaks at half scale
    o = original_at(n)
    scale = np.sqrt(np.mean(p ** 2)) / (np.sqrt(np.mean(o ** 2)) + 1e-12)
    rows.append((n, np.sqrt(np.mean((p - o * scale) ** 2)) / np.sqrt(np.mean(p ** 2) + 1e-12),
                 flatness(p), on_grid(onsets(p))))
print(f"source: flatness {flatness(unscrambled[:window]):.3f}, onsets on the 1/16 grid {on_grid(onsets(unscrambled[:window])):.2f}")
for n, d, f, g in rows:
    print(f"  pass {n}: distance {d:.2f}  flatness {f:.3f}  on grid {g:.2f}")
score, source_score = click_scores(out), click_scores(src)
seconds = len(out) / rate
line = (f"clicks: {np.sum(score > 8) / seconds:.1f}/s against the source's {np.sum(source_score > 8) / (len(src) / rate):.1f}/s;"
        f" peak {20 * np.log10(np.abs(out).max() * 2 + 1e-12):.1f} dB of half scale")
if args.slice:
    step = int(args.slice * rate)
    near = [score[max(0, s - 132):s + 132].max() for s in range(step, len(out) - 132, step)]
    line += f"; worst at a seam {max(near):.1f}, median {np.median(near):.1f}"
print(line)
