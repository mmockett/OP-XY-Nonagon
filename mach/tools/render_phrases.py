#!/usr/bin/env python3
"""Renders each preset of definition.json playing a short phrase (a melody, a
chord progression, or a bass line for the bass presets) through tools/render.cc,
plus the level checks: one note of each source, and an eight-note chord of each
preset. Not part of the machine.
  python tools/render_phrases.py <render binary> <out folder>"""
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
binary, out = (sys.argv[1], pathlib.Path(sys.argv[2])) if __name__ == "__main__" else (None, None)
if out: out.mkdir(parents=True, exist_ok=True)
definition = json.loads((ROOT / "definition.json").read_text())
ids = [p["id"] for p in definition["parameters"]]
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    """'C4', 'Eb3', 'F#2' to MIDI."""
    step = NAMES[name[0]]
    rest = name[1:]
    if rest.startswith("b"): step, rest = step - 1, rest[1:]
    if rest.startswith("#"): step, rest = step + 1, rest[1:]
    return 12 * (int(rest) + 1) + step


def render(name, values, notes, seconds, release):
    spec = [f"out {out / (name + '.wav')}", f"seconds {seconds}", f"release {release}",
            "params " + " ".join(str(values[i]) for i in ids)]
    spec += [f"note {on} {off} {midi(n)}" for on, off, n in notes]
    subprocess.run([binary], input="\n".join(spec), text=True, check=True)
    return out / (name + ".wav")


def line(start, beat, pattern):
    """A melody: (note, beats held, beats to the next) from `start`."""
    notes, t = [], start
    for n, held, step in pattern:
        notes.append((round(t, 4), round(t + held * beat, 4), n))
        t += step * beat
    return notes


def chords(start, length, progression, hold=0.9):
    notes = []
    for i, chord in enumerate(progression):
        t = start + i * length
        notes += [(round(t, 4), round(t + hold * length, 4), n) for n in chord]
    return notes


b = 0.6   # 100 BPM
PHRASES = {
    # A bell melody: each note swoops in and lands on its pitch.
    "Init": (line(0.1, b, [("E4", 1.6, 2), ("G4", 1.6, 2), ("A4", 1.6, 2), ("G4", 0.8, 1), ("E4", 0.8, 1),
                           ("D4", 1.6, 2), ("C4", 3, 4)]), 9.5, 0.8),
    "Flyby Pad": (chords(0.1, 4 * b, [["C3", "G3", "B3", "E4"], ["A2", "E3", "G3", "C4"],
                                      ["F2", "C3", "E3", "A3"], ["G2", "D3", "F3", "B3"]]), 11.0, 1.5),
    "Reverse Swell": (chords(0.1, 4 * b, [["A3", "C4", "E4"], ["F3", "A3", "C4"], ["C4", "E4", "G4"], ["G3", "B3", "D4"]], 0.95), 11.0, 1.2),
    "Boom Drum": (line(0.1, b / 2, [("C2", 0.8, 2), ("C2", 0.8, 1.5), ("G1", 0.8, 0.5), ("C2", 0.8, 2), ("Eb2", 0.8, 2),
                                    ("C2", 0.8, 2), ("C2", 0.8, 1.5), ("G1", 0.8, 0.5), ("Bb1", 0.8, 2), ("C2", 0.8, 2)]), 6.0, 0.3),
    "Siren Lead": (line(0.1, b, [("A4", 1.8, 2), ("C5", 0.9, 1), ("D5", 0.9, 1), ("E5", 1.8, 2), ("D5", 0.9, 1),
                                 ("C5", 0.9, 1), ("A4", 3.5, 4)]), 8.5, 0.6),
    "Drone Pass": (chords(0.1, 8 * b, [["C2", "G2", "C3"], ["Bb1", "F2", "D3"]], 0.95), 11.5, 2.0),
    "Choir Jet": (chords(0.1, 4 * b, [["D3", "A3", "F4"], ["Bb2", "F3", "D4"], ["F3", "A3", "C4"], ["C3", "G3", "E4"]]), 11.0, 1.5),
    "Doppler Bass": (line(0.1, b / 2, [("C2", 0.9, 1), ("C2", 0.9, 1), ("Eb2", 0.9, 1), ("C2", 0.9, 1), ("F2", 0.9, 1),
                                       ("G2", 0.9, 1), ("Bb2", 0.9, 1), ("G2", 0.9, 1), ("C2", 0.9, 1), ("C2", 0.9, 1),
                                       ("Eb2", 0.9, 1), ("C2", 0.9, 1), ("Bb1", 0.9, 1), ("G1", 0.9, 1), ("Bb1", 0.9, 1), ("C2", 1.8, 2)]), 6.5, 0.2),
}

if __name__ == "__main__":
    made = []
    for name, values in definition["presets"].items():
        notes, seconds, release = PHRASES[name]
        made.append(render(name.lower().replace(" ", "-"), values, notes, seconds, release))
        # The level check: an eight-note chord (C major over three octaves) on the preset.
        chord = ["C2", "G2", "C3", "E3", "G3", "C4", "E4", "G4"]
        made.append(render("chord8-" + name.lower().replace(" ", "-"), values,
                           [(0.1, 0.1 + 0.9 * 4 * b, n) for n in chord], 4 * b + 4.0, 1.0))
    for path in made:
        print(path)
