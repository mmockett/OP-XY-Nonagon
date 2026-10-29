#!/usr/bin/env python3
"""What each control does to the sound: a held A3 (tools/sweep.cpp) at each
control's 0, 25, 50, 75 and 100, the others at their defaults, measured.

    life     seconds until the note falls 40 dB under its level
    level    RMS dBFS over two seconds from 0.3 s (from 20 ms, if shorter)
    pulses   pulses a second: the loudness's peaks
    even     how steady the pulses' spacing is: the spread of the intervals
             between them, as a share of their mean (0 is a metronome)
    swing    the longer of each two neighbouring intervals, as a share of
             the two (0.5 is straight, 0.67 a triplet shuffle)
    centroid the spectral centroid's median, Hz
    over2.5k the share of power over 2.5 kHz, dB
    clicks   bright clicks a second (the crackle)

    python tools/sweep.py --sdk <sdk> [--midi 57] [--seconds 8]

Needs numpy and a host C++ compiler."""
import argparse
import json
import pathlib
import subprocess
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATE = 44100


def measure(x):
    env = np.convolve(np.abs(x), np.ones(441) / 441, "same")          # 10 ms
    peak = env.max() + 1e-12
    alive = np.nonzero(env > peak * 0.01)[0]
    life = (alive[-1] / RATE) if len(alive) else 0.0
    # From 0.3 s (from 20 ms for a note shorter than a second) for two seconds.
    a = int((0.3 if life > 1 else 0.02) * RATE)
    b = min(len(x), a + int(2 * RATE), max(a + 4410, int(life * RATE)))
    seg = x[a:b]
    out = {"life": life, "level": 20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-12)}
    # The pulses: peaks of the loudness (2 ms), at least 22 ms apart.
    e = np.convolve(np.abs(seg), np.ones(88) / 88, "same")[::22]
    er = RATE / 22
    smooth = np.convolve(e, np.ones(9) / 9, "same")
    gap = int(er / 45)
    peaks = [i for i in range(gap, len(smooth) - gap)
             if smooth[i] == smooth[i - gap:i + gap + 1].max() and smooth[i] > 1.3 * np.median(smooth)]
    out["pulses"] = len(peaks) / (len(seg) / RATE)
    iv = np.diff(peaks) / er
    out["even"] = float(np.std(iv) / np.mean(iv)) if len(iv) > 2 else float("nan")
    # Swing: in each pair of neighbouring intervals, the longer's share (0.5 straight).
    out["swing"] = float(np.median(np.maximum(iv[:-1], iv[1:]) / (iv[:-1] + iv[1:]))) if len(iv) > 3 else 0.5
    # Spectrum.
    cs, hs = [], []
    for i in range(0, len(seg) - 2048, 1024):
        s = np.abs(np.fft.rfft(seg[i:i + 2048] * np.hanning(2048))) ** 2
        ff = np.fft.rfftfreq(2048, 1 / RATE)
        if s.sum() > 0:
            cs.append((ff * s).sum() / s.sum())
            hs.append(s[ff > 2500].sum() / s.sum())
    out["centroid"] = float(np.median(cs)) if cs else 0.0
    out["over2.5k"] = float(10 * np.log10(np.mean(hs) + 1e-12)) if hs else -120.0
    # Clicks: a 5 kHz high-pass's envelope jumping six times over its median.
    d = np.diff(seg, 2)
    he = np.convolve(np.abs(d), np.ones(22) / 22, "same")
    over = he > 6 * (np.median(he) + 1e-9)
    starts = np.nonzero(over[1:] & ~over[:-1])[0]
    out["clicks"] = len(starts) / (len(seg) / RATE)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--midi", type=int, default=57)
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--json")
    args = parser.parse_args()
    work = pathlib.Path(tempfile.mkdtemp(prefix="drop-sweep-"))
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", str(ROOT / "tools" / "sweep.cpp"),
                    "-o", str(work / "sweep")], check=True)
    names = [p["label"].lower() for p in json.loads((ROOT / "definition.json").read_text())["parameters"]]
    rows = {}
    keys = ["life", "level", "pulses", "even", "swing", "centroid", "over2.5k", "clicks"]
    print(f"{'':10s} {'':>4s} " + " ".join(f"{k:>9s}" for k in keys))
    for k, name in enumerate(names):
        for v in (0, 0.25, 0.5, 0.75, 1):
            raw = subprocess.run([str(work / "sweep"), str(k), str(v), str(args.seconds), str(args.midi)],
                                 check=True, capture_output=True).stdout
            m = measure(np.frombuffer(raw, dtype=np.float32).astype(float))
            rows[f"{name} {int(v * 100)}"] = m
            print(f"{name:10s} {int(v * 100):4d} " + " ".join(f"{m[key]:9.2f}" for key in keys))
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
