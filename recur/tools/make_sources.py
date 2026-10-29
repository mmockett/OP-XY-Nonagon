#!/usr/bin/env python3
"""Synthesises Recur's test material, two bars at 120 BPM each (4 s, 44.1 kHz,
16-bit mono): a drum loop and a melodic phrase over a bass line. Both are made
here, so they are free to use: python make_sources.py <folder>"""
import pathlib
import sys
import wave

import numpy as np

RATE, BEAT = 44100, 0.5
SIXTEENTH = BEAT / 4
LENGTH = int(RATE * 8 * BEAT)
rng = np.random.default_rng(7)


def place(out, sound, at):
    start = int(at * RATE)
    end = min(len(out), start + len(sound))
    out[start:end] += sound[:end - start]


def kick():
    t = np.arange(int(0.35 * RATE)) / RATE
    f = 45 + 90 * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 9)


def snare():
    t = np.arange(int(0.25 * RATE)) / RATE
    noise = rng.standard_normal(len(t))
    noise = np.convolve(noise, [1, -0.6], 'same')
    return 0.55 * noise * np.exp(-t * 18) + 0.5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t * 25)


def hat(open_=False):
    t = np.arange(int((0.18 if open_ else 0.05) * RATE)) / RATE
    noise = np.diff(rng.standard_normal(len(t) + 1))
    return 0.22 * noise * np.exp(-t * (14 if open_ else 70))


def drums():
    out = np.zeros(LENGTH)
    for bar in range(2):
        b = bar * 16
        for s in (0, 6, 8, 11 if bar else 10):
            place(out, kick(), (b + s) * SIXTEENTH)
        for s in (4, 12):
            place(out, snare(), (b + s) * SIXTEENTH)
        for s in range(16):
            if s % 2 == 0 or s in (7, 15):
                place(out, hat(s == 14) * (1 if s % 4 == 0 else 0.7), (b + s) * SIXTEENTH)
    return out


def pluck(freq, length, bright=1.0):
    t = np.arange(int(length * RATE)) / RATE
    v = np.zeros(len(t))
    for h in range(1, 12):
        v += np.sin(2 * np.pi * freq * h * t) * np.exp(-t * (3 + h * 1.6 / bright)) / h
    return v * np.minimum(1, t * 400)


def phrase():
    out = np.zeros(LENGTH)
    # C minor: a melody in eighths and sixteenths over a bass on each half bar.
    notes = [(0, 72, 2), (2, 75, 2), (4, 79, 2), (6, 77, 1), (7, 75, 1), (8, 74, 4), (12, 70, 2), (14, 72, 2),
             (16, 75, 2), (18, 79, 2), (20, 82, 3), (23, 80, 1), (24, 79, 2), (26, 77, 2), (28, 75, 4)]
    for s, note, length in notes:
        place(out, 0.5 * pluck(440 * 2 ** ((note - 69) / 12), length * SIXTEENTH + 0.3), s * SIXTEENTH)
    for s, note in ((0, 36), (8, 32), (16, 39), (24, 31)):
        place(out, 0.6 * pluck(440 * 2 ** ((note - 69) / 12), 8 * SIXTEENTH, 0.5), s * SIXTEENTH)
    return out


def save(path, x):
    x = x / np.abs(x).max() * 0.85
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes((x * 32767).astype('<i2').tobytes())


folder = pathlib.Path(sys.argv[1])
save(folder / 'source-drums.wav', drums())
save(folder / 'source-phrase.wav', phrase())
print('wrote', folder / 'source-drums.wav', folder / 'source-phrase.wav')
