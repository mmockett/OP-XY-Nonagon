#!/usr/bin/env python3
"""The README's control table: each control at 0, 25, 50, 75 and 100 with the
others at the Init sound's, one note (C4 held) each, and what that control
changes, measured: from the audio (tools/measure.py) and from the voice's own
roots (tools/render.cc's trace). Not part of the machine.
With tests/screen.cpp built, each cell also gives the header's caption.
  python tools/control_table.py <render binary> <scratch folder> [screen binary]"""
import json
import math
import pathlib
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from measure import RATE, centroid, hnr, load, loudness  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
binary, work = sys.argv[1], pathlib.Path(sys.argv[2])
screen = sys.argv[3] if len(sys.argv) > 3 else None
work.mkdir(parents=True, exist_ok=True)
definition = json.loads((ROOT / "definition.json").read_text())
ids = [p["id"] for p in definition["parameters"]]
init = definition["presets"]["Init"]
SUBSTEP = 97391 / 67108864        # seconds of listener time a sub-step


def play(control, value):
    values = dict(init)
    values[control] = value
    name = f"{control}-{int(value * 100):03d}"
    spec = [f"out {work / (name + '.wav')}", f"trace {work / (name + '.txt')}", "seconds 12", "release 0.3",
            "params " + " ".join(str(values[i]) for i in ids), "note 0.0 11.5 60"]
    subprocess.run([binary], input="\n".join(spec), text=True, check=True)
    x = load(work / (name + ".wav"))
    rows = [list(map(float, line.split())) for line in (work / (name + ".txt")).read_text().splitlines()]
    return x, np.array(rows)


def caption(c, value):
    """The header's value for control c at this setting (page 1's then page 2's)."""
    if not screen: return ""
    result = subprocess.run([screen, f"still:{c}:{value}", "60", "0.5"], capture_output=True, text=True, check=True)
    texts = json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))["texts"]
    return texts[4 + c if c < 4 else 12 + c - 4][3]


def heard(trace, r):
    """The root's heard pitch over the key (rate times the pitch factor) while it sounds."""
    live = (trace[:, 1 + 3 * r] >= 1) & (trace[:, 1 + 3 * r] <= 2) & (trace[:, 3 + 3 * r] > 0) & (trace[:, 2 + 3 * r] != 0)
    ratio = np.abs(trace[:, 2 + 3 * r]) / 67108864 / SUBSTEP * trace[:, -1] / 65536
    return trace[live, 0], ratio[live]


def pitches(trace, r=0):
    """The root's heard pitch against the key, semitones, from its 1st to its
    99th percentile over the time it sounds (the fold's first instants aside)."""
    t, ratio = heard(trace, r)
    ratio = ratio[ratio > 0]
    if len(ratio) < 3: return None
    st = 12 * np.log2(ratio)
    return np.percentile(st, 1), np.percentile(st, 99)


def glide(x, trace):
    lo, hi = pitches(trace)
    text = f"{hi:+.0f} to {lo:+.0f} st"
    back = pitches(trace, 1)
    return text + (f"; back {back[0]:+.0f} to {back[1]:+.0f}" if back else "")


def swoop(trace):
    """Seconds the forward image takes over the middle 80% of its glide."""
    t, ratio = heard(trace, 0)
    st = 12 * np.log2(np.maximum(ratio, 1e-9))
    lo, hi = np.percentile(st, 2), np.percentile(st, 98)
    a, b = hi - 0.1 * (hi - lo), lo + 0.1 * (hi - lo)
    ta, tb = t[np.argmax(st <= a)], t[np.argmax(st <= b)]
    return tb - ta


def envelope(x, n=2205):
    m = len(x) // n
    return np.sqrt(np.mean(x[:m * n].reshape(m, n) ** 2, axis=1))


def first_sound(x):
    e = envelope(x, 441)
    return np.argmax(e > e.max() * 1e-3) * 441 / RATE


def length(x):
    e = envelope(x)
    on = np.nonzero(e > e.max() * 0.01)[0]       # within 40 dB of its loudest 50 ms
    return (on[-1] - on[0] + 1) * 2205 / RATE if len(on) else 0.0


def boom(x):
    """The boom's peak and its spectral centroid, over its first 30 ms."""
    start = int(first_sound(x) * RATE)
    seg = x[start:start + 1323]
    p = np.abs(seg).max()
    spectrum = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192)) ** 2
    f = np.fft.rfftfreq(8192, 1 / RATE)
    return 20 * math.log10(p) if p > 0 else -120, float((f * spectrum).sum() / (spectrum.sum() + 1e-30))


def ends(trace):
    """The pitch each image ends on, against the key (semitones)."""
    out = []
    for r, name in ((0, "fwd"), (1, "back")):
        t, ratio = heard(trace, r)
        if len(t): out.append(f"{name} {12 * math.log2(ratio[-1]):+.0f}")
    return ", ".join(out) + " st"


def high(x):
    """The share of the sound above 500 Hz, dB."""
    spectrum = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / RATE)
    return 10 * math.log10(spectrum[f > 500].sum() / (spectrum.sum() + 1e-30) + 1e-12)


def echo_delay(trace):
    """Milliseconds from the direct sound's first arrival to the ground's."""
    def first(rs):
        on = np.nonzero(np.any([(trace[:, 1 + 3 * r] >= 1) & (trace[:, 1 + 3 * r] <= 2) for r in rs], axis=0))[0]
        return trace[on[0], 0] if len(on) else None
    a, b = first([0, 1]), first([2, 3])
    return None if a is None or b is None else 1000 * (b - a)


NAMES = ["organ", "pad", "voice", "bell", "siren"]
ROWS = [
    ("mach", "the forward image's glide against the key (and the backward one's), 1st to 99th percentile",
     lambda x, tr: glide(x, tr)),
    ("pass", "how long the glide takes, and the loudest 400 ms",
     lambda x, tr: f"{swoop(tr):.2f} s, {loudness(x)[1]:.0f} LUFS"),
    ("source", "spectral centroid",
     lambda x, tr, v=None: f"{centroid(x)[0]:.0f} Hz"),
    ("dur", "how long it sounds (within 40 dB of its loudest)",
     lambda x, tr: f"{length(x):.1f} s"),
    ("boom", "the boom's peak and centroid (first 30 ms)",
     lambda x, tr: "{:.0f} dBFS, {:.0f} Hz".format(*boom(x))),
    ("air", "the sound's share above 500 Hz",
     lambda x, tr: f"{high(x):.0f} dB"),
    ("tune", "the pitch each image ends on, against the key",
     lambda x, tr: ends(tr)),
    ("echo", "the ground's sound, after the direct sound's",
     lambda x, tr: "none" if echo_delay(tr) is None else f"+{echo_delay(tr):.0f} ms"),
]
print("| | 0 | 25 | 50 | 75 | 100 | measured |")
print("| --- | --- | --- | --- | --- | --- | --- |")
for control, what, measure in ROWS:
    cells = []
    for value in (0.0, 0.25, 0.5, 0.75, 1.0):
        # source: its five options at 0, 25, 50, 75 and 100
        v = min(value, 0.9) if control == "source" else value
        x, tr = play(control, v)
        cell = measure(x, tr)
        label = caption(ids.index(control), v)
        if label and control != "source": cell = f"`{label}` {cell}"
        if control == "source": cell = f"{NAMES[min(4, int(v * 5))]} {cell}"
        cells.append(cell)
    print(f"| **{control}** | " + " | ".join(cells) + f" | {what} |")
