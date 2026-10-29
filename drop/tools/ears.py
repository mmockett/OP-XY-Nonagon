#!/usr/bin/env python3
"""Stand-ins for listening: measures held notes (raw float32 or 16-bit WAV
files at 44.1 kHz) and prints, for each, its level, crest factor, harmonic-to-
noise ratio, spectral centroid over time, how much its loudness fluctuates
(0.5 to 15 Hz: beats and rhythm) and how rough it is (15 to 300 Hz), and its
pitch against the key. A measuring tool, not part of the machine.

    python tools/ears.py <file>:<midi>[:<from seconds, 0.5>] ...

Needs numpy."""
import sys
import wave

import numpy as np

RATE = 44100


def load(path):
    if path.endswith(".wav"):
        w = wave.open(path)
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768.0
    return np.fromfile(path, dtype=np.float32).astype(float)


def measure(x, f0, start=0.5):
    x = x[int(start * RATE):len(x) - int(0.2 * RATE)]
    x = x[:np.max(np.nonzero(np.abs(x) > 1e-5)) + 1] if np.any(np.abs(x) > 1e-5) else x   # to its end
    rms = np.sqrt(np.mean(x ** 2)) + 1e-12
    out = {"rms dBFS": 20 * np.log10(rms), "crest": np.abs(x).max() / rms}
    # Harmonic-to-noise: power within 3% of each harmonic under 6 kHz against the rest, 50 Hz to 6 kHz.
    n = 8192
    p = 0
    for i in range(0, len(x) - n, n // 2):
        p = p + np.abs(np.fft.rfft(x[i:i + n] * np.blackman(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / RATE)
    band = (f > 50) & (f < 6000)
    harmonic = np.zeros_like(f, dtype=bool)
    for k in range(1, int(6000 / f0) + 1):
        harmonic |= np.abs(f - k * f0) < 0.03 * k * f0
    h = p[band & harmonic].sum()
    out["HNR dB"] = 10 * np.log10(h / (p[band & ~harmonic].sum() + 1e-30))
    # Pitch: power-weighted centre of the first four harmonics' bands.
    cents, weight = 0, 0
    for k in range(1, 5):
        m = np.abs(f - k * f0) < 0.06 * k * f0
        if p[m].sum() > 0:
            c = (f[m] * p[m]).sum() / p[m].sum()
            cents += p[m].sum() * 1200 * np.log2(c / (k * f0))
            weight += p[m].sum()
    out["pitch cents"] = cents / weight if weight else 0
    # Spectral centroid over 46 ms frames.
    cs = []
    for i in range(0, len(x) - 2048, 1024):
        s = np.abs(np.fft.rfft(x[i:i + 2048] * np.hanning(2048))) ** 2
        ff = np.fft.rfftfreq(2048, 1 / RATE)
        if s.sum() > 0:
            cs.append((ff * s).sum() / s.sum())
    out["centroid Hz"] = np.median(cs)
    out["centroid 10-90%"] = (np.percentile(cs, 10), np.percentile(cs, 90))
    # The envelope (rectified, 2 ms), at 2 kHz: its modulation, as a share of its mean.
    env = np.convolve(np.abs(x), np.ones(88) / 88, "same")[::22]
    er = RATE / 22
    e = env - env.mean()
    spec = np.abs(np.fft.rfft(e * np.hanning(len(e)))) ** 2
    fm = np.fft.rfftfreq(len(e), 1 / er)
    total = (env.mean() ** 2) * len(e) * 0.375 * len(e)   # the mean's power, through the same window
    out["fluctuation dB"] = 10 * np.log10(spec[(fm > 0.5) & (fm < 15)].sum() / total)
    out["roughness dB"] = 10 * np.log10(spec[(fm >= 15) & (fm < 300)].sum() / total)
    return out


if __name__ == "__main__":
    rows = []
    for arg in sys.argv[1:]:
        parts = arg.split(":")
        path, midi = parts[0], parts[1]
        start = float(parts[2]) if len(parts) > 2 else 0.5
        f0 = 440 * 2 ** ((int(midi) - 69) / 12)
        rows.append((path.split("/")[-1].rsplit(".", 1)[0], measure(load(path), f0, start)))
    print(f"{'':24s} {'rms':>6s} {'crest':>5s} {'HNR':>5s} {'cents':>6s} {'centroid':>8s} {'10-90%':>12s} {'fluct':>6s} {'rough':>6s}")
    for name, m in rows:
        lo, hi = m["centroid 10-90%"]
        print(f"{name:24s} {m['rms dBFS']:6.1f} {m['crest']:5.1f} {m['HNR dB']:5.1f} {m['pitch cents']:+6.1f} "
              f"{m['centroid Hz']:8.0f} {lo:5.0f}-{hi:<6.0f} {m['fluctuation dB']:6.1f} {m['roughness dB']:6.1f}")
