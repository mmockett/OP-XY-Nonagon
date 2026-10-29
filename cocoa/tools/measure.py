#!/usr/bin/env python3
"""Measures WAVs as stand-ins for ears: level, crest factor, spectral
centroid, harmonic-to-noise ratio, tonal share, roughness and pitch.

    python tools/measure.py <folder>/*.wav

- level: RMS while sounding (|x| > 0.001) and the loudest 0.4 s, dBFS.
- crest: peak over RMS while sounding, dB (a pad is about 10, a struck
  instrument 15 to 20).
- centroid: the spectral centroid in 46 ms frames while sounding, energy
  weighted, and its 10th and 90th percentiles across frames (how much the
  brightness moves).
- HNR: harmonic-to-noise ratio from each frame's normalised autocorrelation
  peak between 1 and 20 ms lags (Boersma), median over sounding frames.
- tonal: the share of the spectrum's energy within 3 bins of its 24 largest
  peaks, 0.6 to 1.6 s in, once a scoop has landed (1 is pure partials).
- roughness: Sethares' sum over pairs of the 24 largest peaks, 0.6 to 1.1 s
  in, with amplitudes normalised to the largest (0 is smooth).
- pitch: for files named *single-a3*, the strongest frequency near 220 Hz
  from 0.7 to 1.4 s, in cents from 220 Hz.
Needs numpy."""
import sys
import wave
import numpy as np

SR = 44100


def load(path):
    w = wave.open(path)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768


def peaks(x, a, b, count=24):
    seg = x[int(a * SR):int(b * SR)]
    n = len(seg)
    win = np.blackman(n)
    spec = np.abs(np.fft.rfft(seg * win))
    f = np.fft.rfftfreq(n, 1 / SR)
    local = (spec[1:-1] > spec[:-2]) & (spec[1:-1] >= spec[2:])
    idx = np.where(local)[0] + 1
    idx = idx[np.argsort(spec[idx])[::-1][:count]]
    return f[idx], spec[idx], spec, f


def roughness(fs, amps):
    amps = amps / (amps.max() + 1e-30)
    r = 0.0
    for i in range(len(fs)):
        for j in range(i + 1, len(fs)):
            f1, f2 = sorted((fs[i], fs[j]))
            s = 0.24 / (0.021 * f1 + 19)
            d = f2 - f1
            r += amps[i] * amps[j] * (np.exp(-3.5 * s * d) - np.exp(-5.75 * s * d))
    return r


def measure(path):
    x = load(path)
    sounding = np.abs(x) > 1e-3
    rms = np.sqrt(np.mean(x[sounding] ** 2)) if sounding.any() else 1e-9
    peak = np.max(np.abs(x))
    win = int(0.4 * SR)
    loudest = max(np.sqrt(np.mean(x[s:s + win] ** 2)) for s in range(0, max(1, len(x) - win), SR // 20))
    # Frames for centroid and HNR.
    n = 2048
    cents, weights, hnrs = [], [], []
    lo, hi = int(SR / 1000), int(SR / 50)
    for s in range(0, len(x) - n, n // 2):
        fr = x[s:s + n]
        e = np.mean(fr ** 2)
        if e < 1e-6:
            continue
        spec = np.abs(np.fft.rfft(fr * np.hanning(n))) ** 2
        f = np.fft.rfftfreq(n, 1 / SR)
        cents.append(np.sum(f * spec) / np.sum(spec))
        weights.append(e)
        h = fr * np.hanning(n)
        ac = np.correlate(h, h, 'full')[n - 1:]
        wac = np.correlate(np.hanning(n), np.hanning(n), 'full')[n - 1:]
        ac = ac / (ac[0] + 1e-30) / (wac / wac[0])
        r = np.clip(np.max(ac[lo:hi]), 1e-6, 0.999999)
        hnrs.append(10 * np.log10(r / (1 - r)))
    cents, weights = np.array(cents), np.array(weights)
    centroid = np.sum(cents * weights) / np.sum(weights)
    fs, amps, spec, f = peaks(x, 0.6, 1.6)
    tonal_mask = np.zeros_like(spec, dtype=bool)
    for fp in fs:
        k = int(round(fp * len(spec) * 2 / SR)) if False else int(np.argmin(np.abs(f - fp)))
        tonal_mask[max(0, k - 3):k + 4] = True
    tonal = np.sum(spec[tonal_mask] ** 2) / np.sum(spec ** 2)
    rf, ra, _, _ = peaks(x, 0.6, 1.1)
    rough = roughness(rf, ra)
    out = (f"{path.split('/')[-1]:30s} rms {20 * np.log10(rms):6.1f}  loudest {20 * np.log10(loudest):6.1f}  "
           f"peak {peak:.3f}  crest {20 * np.log10(peak / rms):5.1f}  centroid {centroid:6.0f} "
           f"({np.percentile(cents, 10):5.0f}-{np.percentile(cents, 90):5.0f})  HNR {np.median(hnrs):5.1f}  "
           f"tonal {tonal:.3f}  rough {rough:.3f}")
    if 'single-a3' in path:
        seg = x[int(0.7 * SR):int(1.4 * SR)]
        ff = np.arange(200, 240, 0.05)
        t = np.arange(len(seg)) / SR
        h = np.hanning(len(seg))
        mags = [abs(np.sum(seg * h * np.exp(-2j * np.pi * q * t))) for q in ff]
        out += f"  pitch {1200 * np.log2(ff[int(np.argmax(mags))] / 220):+.1f} c"
    print(out)


for p in sys.argv[1:]:
    measure(p)
