#!/usr/bin/env python3
"""Stand-ins for listening, for WAVs or raw float32 (.f32) renders at 44.1 kHz:
peak and crest factor, loudness (BS.1770 integrated and loudest 400 ms),
spectral centroid over time, harmonic-to-noise ratio (autocorrelation), a
roughness index (amplitude-envelope modulation at 15-150 Hz) and DC.
Not part of the machine.
  python tools/measure.py file.wav ...      (one line each)"""
import math
import pathlib
import sys
import wave

import numpy as np

RATE = 44100


def load(path):
    p = pathlib.Path(path)
    if p.suffix == ".f32":
        return np.fromfile(p, dtype="<f4").astype(np.float64)
    with wave.open(str(p)) as w:
        raw = w.readframes(w.getnframes())
    b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
    return np.where(v >= 1 << 23, v - (1 << 24), v) / 8388607.0


def biquad(x, b, a):
    # Direct form II transposed in blocks of numpy (scalar loop on a list: fine for seconds of audio).
    y = np.empty_like(x)
    z1 = z2 = 0.0
    b0, b1, b2 = b
    _, a1, a2 = a
    for i, v in enumerate(x.tolist()):
        out = b0 * v + z1
        z1 = b1 * v - a1 * out + z2
        z2 = b2 * v - a2 * out
        y[i] = out
    return y


def k_weight(x):
    G, Q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    K = math.tan(math.pi * fc / RATE)
    Vh, Vb = 10 ** (G / 20), (10 ** (G / 20)) ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    x = biquad(x, [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
               [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])
    fc, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * fc / RATE)
    a0 = 1 + K / Q + K * K
    return biquad(x, [1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])


def loudness(x):
    k = k_weight(x)
    block, hop = int(0.4 * RATE), int(0.1 * RATE)
    ms = np.array([np.mean(k[i:i + block] ** 2) for i in range(0, max(1, len(k) - block), hop)])
    lk = -0.691 + 10 * np.log10(ms + 1e-20)
    gated = ms[lk > -70]
    if not len(gated):
        return -99.0, -99.0
    rel = -0.691 + 10 * np.log10(np.mean(gated)) - 10
    return -0.691 + 10 * np.log10(np.mean(ms[(lk > -70) & (lk > rel)])), float(lk.max())


def frames(x, n, hop):
    for i in range(0, len(x) - n, hop):
        yield i, x[i:i + n]


def active(x, n=4096):
    """Windows within 30 dB of the loudest."""
    e = np.array([np.mean(f ** 2) for _, f in frames(x, n, n // 2)])
    return e > e.max() * 1e-3


def centroid(x):
    n, out = 4096, []
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / RATE)
    mask = active(x, n)
    for j, (_, seg) in enumerate(frames(x, n, n // 2)):
        if j < len(mask) and mask[j]:
            s = np.abs(np.fft.rfft(seg * w)) ** 2
            out.append(float((f * s).sum() / (s.sum() + 1e-30)))
    out = np.array(out) if out else np.array([0.0])
    return np.median(out), np.percentile(out, 10), np.percentile(out, 90)


def hnr(x):
    """Median harmonic-to-noise ratio (dB) of 46 ms windows, from the
    normalized autocorrelation peak between 1 and 25 ms (40 Hz .. 1 kHz)."""
    n, out = 2048, []
    mask = active(x, n)
    w = np.hanning(n)
    for j, (_, seg) in enumerate(frames(x, n, n // 2)):
        if j >= len(mask) or not mask[j]:
            continue
        s = seg * w
        spec = np.fft.rfft(s, 2 * n)
        ac = np.fft.irfft(np.abs(spec) ** 2)[:n]
        wac = np.fft.irfft(np.abs(np.fft.rfft(w, 2 * n)) ** 2)[:n]
        r = ac / (ac[0] + 1e-30) / (wac / wac[0] + 1e-9)
        lo, hi = RATE // 1000, RATE // 40
        peak = min(0.9999, max(1e-4, float(r[lo:hi].max())))
        out.append(10 * math.log10(peak / (1 - peak)))
    return float(np.median(out)) if out else 0.0


def roughness(x):
    """Envelope modulation at 15-150 Hz (where beating sounds rough), as a share
    of the envelope's mean power: 0 is smooth."""
    env = np.abs(x)
    k = int(0.002 * RATE)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    mask = env > env.max() * 0.03
    if mask.sum() < RATE // 4:
        return 0.0
    e = env[mask]
    spec = np.abs(np.fft.rfft(e - e.mean())) ** 2
    f = np.fft.rfftfreq(len(e), 1 / RATE)
    band = spec[(f >= 15) & (f <= 150)].sum()
    return float(band / (len(e) * (e.mean() ** 2) * len(e) / 2 + 1e-30))


def describe(path):
    x = load(path)
    peak = float(np.abs(x).max())
    mask = np.abs(x) > peak * 1e-3
    rms = math.sqrt(float(np.mean(x[mask] ** 2))) if mask.any() else 1e-9
    integ, loudest = loudness(x)
    c, c10, c90 = centroid(x)
    return {"file": pathlib.Path(path).name, "peak": peak, "peak_db": 20 * math.log10(peak + 1e-12),
            "crest_db": 20 * math.log10(peak / rms), "lufs": integ, "loudest": loudest,
            "centroid": c, "centroid10": c10, "centroid90": c90, "hnr": hnr(x), "rough": roughness(x),
            "dc": float(x.mean())}


if __name__ == "__main__":
    for path in sys.argv[1:]:
        d = describe(path)
        print(f"{d['file']:34s} peak {d['peak']:.2f} ({d['peak_db']:5.1f} dBFS)  crest {d['crest_db']:4.1f} dB  "
              f"{d['lufs']:5.1f} LUFS (400 ms max {d['loudest']:5.1f})  centroid {d['centroid']:5.0f} Hz "
              f"[{d['centroid10']:4.0f}..{d['centroid90']:4.0f}]  HNR {d['hnr']:4.1f} dB  rough {d['rough']:.3f}  DC {d['dc']:+.1e}")
