"""Measurements that stand in for ears, for tools/phrases.py.

- Level: RMS over the sounding part (dBFS), peak, crest factor (peak / RMS, dB) and DC.
- Harmonicity: the energy in the spectrum's peaks against the rest (dB), the
  median over 0.37 s windows: a tonal-to-noise ratio that does not assume
  harmonic partials. Tonal sounds read 25 dB and up (capped at 60); breathy ones lower.
- Brightness: the spectral centroid, its median and 10th-90th percentile
  range over the windows (Hz): the range is how far the timbre moves.
- Roughness: Plomp-Levelt / Sethares dissonance between the windows' peaks,
  normalized by their power: 0 for a single sine, about 0.1 to 0.3 for
  harmonic tones and chords, higher for beating clusters.
- Pitch: for each note, the strongest peak within 50 cents of its key, 0.1 to
  0.4 s after it starts: the median and largest deviation (cents).
- Clicks: samples whose second difference stands 12 times above its local RMS.
"""
import math
import wave

import numpy as np

RATE = 44100
N = 16384
_n = np.arange(N)
WINDOW = 0.35875 - 0.48829 * np.cos(2 * np.pi * _n / N) + 0.14128 * np.cos(4 * np.pi * _n / N) - 0.01168 * np.cos(6 * np.pi * _n / N)
FREQ = np.fft.rfftfreq(N, 1 / RATE)


def load(path):
    w = wave.open(str(path))
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768.0


def peaks(power, floor_db=-60, most=48):
    p = power
    top = p.max()
    if top <= 0:
        return []
    idx = np.nonzero((p[1:-1] > p[:-2]) & (p[1:-1] >= p[2:]) & (p[1:-1] > top * 10 ** (floor_db / 10)))[0] + 1
    idx = idx[np.argsort(p[idx])[::-1][:most]]
    out = []
    for i in idx:
        # parabolic interpolation on the log spectrum
        a, b, c = np.log(p[i - 1] + 1e-30), np.log(p[i] + 1e-30), np.log(p[i + 1] + 1e-30)
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
        out.append(((i + d) * RATE / N, p[i - 2:i + 3].sum()))
    return out


def dissonance(pk):
    if len(pk) < 2:
        return 0.0
    f = np.array([x[0] for x in pk])
    a = np.sqrt(np.array([x[1] for x in pk]))
    total = 0.0
    for i in range(len(f)):
        for j in range(i + 1, len(f)):
            lo, hi = min(f[i], f[j]), max(f[i], f[j])
            s = 0.24 / (0.021 * lo + 19)
            total += a[i] * a[j] * (math.exp(-3.5 * s * (hi - lo)) - math.exp(-5.75 * s * (hi - lo)))
    return total / (a * a).sum()


def measure(path, notes):
    x = load(path)
    loud = np.abs(x) > 10 ** (-60 / 20)
    sounding = x[np.nonzero(loud)[0][0]:np.nonzero(loud)[0][-1]] if loud.any() else x
    rms = math.sqrt(np.mean(sounding ** 2) + 1e-20)
    peak = float(np.abs(x).max())
    d2 = np.abs(np.diff(x, 2))
    local = np.sqrt(np.convolve(d2 ** 2, np.ones(441) / 441, mode="same")) + 1e-6
    clicks = int(((d2 / local > 12) & (d2 > 1e-3)).sum())
    hnr, cent, rough = [], [], []
    for i in range(0, len(x) - N, N // 2):
        seg = x[i:i + N]
        if np.mean(seg ** 2) < 10 ** (-50 / 10):
            continue
        p = np.abs(np.fft.rfft(seg * WINDOW)) ** 2
        pk = peaks(p)
        tonal = sum(e for _, e in pk)
        hnr.append(min(60.0, 10 * math.log10(tonal / max(p.sum() - tonal, 1e-30))))
        cent.append(float((p * FREQ).sum() / p.sum()))
        rough.append(dissonance(pk[:24]))
    cents = []
    for start, length, midi in notes:
        a = int((start + 0.1) * RATE)
        seg = x[a:a + 8192]
        if len(seg) < 8192 or np.mean(seg ** 2) < 1e-8:
            continue
        w = np.hanning(8192)
        f0 = 440 * 2 ** ((midi - 69) / 12)
        spec = np.abs(np.fft.rfft(seg * w, 524288)) ** 2
        fr = np.fft.rfftfreq(524288, 1 / RATE)
        band = (fr > f0 * 2 ** (-50 / 1200)) & (fr < f0 * 2 ** (50 / 1200))
        if band.any():
            k = np.argmax(spec * band)
            cents.append(1200 * math.log2(fr[k] / f0))
    c = np.abs(np.array(cents)) if cents else np.array([float("nan")])
    return {"rms": 20 * math.log10(rms), "peak": peak, "crest": 20 * math.log10(peak / rms), "dc": float(x.mean()),
            "hnr": float(np.median(hnr)) if hnr else float("nan"),
            "centroid": (float(np.median(cent)), float(np.percentile(cent, 10)), float(np.percentile(cent, 90))) if cent else (0, 0, 0),
            "rough": float(np.median(rough)) if rough else 0.0,
            "pitch": (float(np.median(c)), float(np.max(c))), "clicks": clicks}


def table(rows):
    lines = ["| Sound | Role | RMS dBFS | Peak | Crest dB | Tonal dB | Centroid Hz (10-90%) | Roughness | Pitch c (median / worst) | Clicks |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for name, role, m in rows:
        c = m["centroid"]
        lines.append(f"| {name} | {role} | {m['rms']:.1f} | {m['peak']:.2f} | {m['crest']:.1f} | {m['hnr']:.1f} | "
                     f"{c[0]:.0f} ({c[1]:.0f}-{c[2]:.0f}) | {m['rough']:.2f} | {m['pitch'][0]:.1f} / {m['pitch'][1]:.1f} | {m['clicks']} |")
    return "\n".join(lines) + "\n"
