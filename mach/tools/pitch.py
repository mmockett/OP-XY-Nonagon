#!/usr/bin/env python3
"""Pitch accuracy of the phrases tools/render_phrases.py renders: for each note
of a melodic preset, the pitch heard over the last half of the note (at most
its last quarter second) against the key, in cents. Not part of the machine.
  python tools/pitch.py <render folder>"""
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from measure import RATE, load  # noqa: E402
import render_phrases as phrases  # noqa: E402  (imports its phrase table only)


def heard(x, start, n, hz):
    """The strongest spectral peak within three semitones of hz (Hann window,
    zero-padded 16 times, parabolic between bins): the fundamental's, or for
    the bell its prime's."""
    seg = x[start:start + n] * np.hanning(n)
    m = 16 * n
    spec = np.abs(np.fft.rfft(seg, m))
    lo, hi = int(hz / 1.19 * m / RATE), int(hz * 1.19 * m / RATE) + 1
    k = lo + int(np.argmax(spec[lo:hi]))
    a, b, c = np.log(spec[k - 1] + 1e-30), np.log(spec[k] + 1e-30), np.log(spec[k + 1] + 1e-30)
    return (k + 0.5 * (a - c) / (a - 2 * b + c)) * RATE / m


folder = pathlib.Path(sys.argv[1])
for name in ("Init", "Siren Lead", "Doppler Bass"):
    notes, _, _ = phrases.PHRASES[name]
    x = load(folder / (name.lower().replace(" ", "-") + ".wav"))
    cents = []
    for on, off, n in notes:
        hz = 440 * 2 ** ((phrases.midi(n) - 69) / 12)
        # The note's last half, at most its last quarter second.
        span = min(0.25, (off - on) / 2)
        cents.append(1200 * np.log2(heard(x, int((off - span) * RATE), int(span * RATE), hz) / hz))
    print(f"{name:13s} cents from the key at each note's end: " + " ".join(f"{c:+.1f}" for c in cents)
          + f"   (worst {max(abs(c) for c in cents):.1f})")
