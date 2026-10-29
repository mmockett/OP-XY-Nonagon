#!/usr/bin/env python3
"""Renders each of Fermi's presets playing a short phrase, and Hexagon's
engines (opxy-pads) in the same roles playing the same phrases, through each
machine's own render() (tools/render.cc). Then measures them all
(tools/measure.py) and prints the table the README quotes.

    python tools/phrases.py --sdk <open-xy checkout> --pads <opxy-pads checkout> --out <output folder>

Needs a host C++ compiler and numpy."""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import measure  # noqa: E402

BPM = 96
BEAT = 60 / BPM


def chords(progression, length, gap=0.0, stagger=0.0):
    notes, t = [], 0.25
    for chord in progression:
        for i, m in enumerate(chord):
            notes.append((t + i * stagger, length - gap - i * stagger, m))
        t += length
    return notes, t


def line(pitches, step, hold, start=0.25):
    return [(start + i * step, hold, m) for i, m in enumerate(pitches) if m is not None], start + len(pitches) * step


# The phrases: (notes, seconds, attack, release), with a note's role.
PAD, _ = None, None
pad_notes, pad_end = chords([[57, 60, 64, 67], [53, 57, 60, 64], [48, 55, 60, 64], [55, 59, 62, 67]], 4 * BEAT * 1.5)
organ_notes, organ_end = chords([[48, 55, 64, 72], [47, 55, 62, 74], [45, 57, 64, 72], [52, 59, 64, 67]], 4 * BEAT)
long_notes, long_end = chords([[50, 57, 64, 65, 69], [46, 53, 62, 65, 69]], 8.0)
clarinet_notes, clarinet_end = line([69, 72, 76, 74, 72, 71, 72, 69, None, 64, 67, 69], BEAT / 2, BEAT / 2 * 0.92)
bell_notes, bell_end = line([84, 88, 91, 96, None, 91, 88, 86, 84, None, None, None], BEAT / 2, BEAT / 4)
bass_notes, bass_end = line([33, 33, None, 36, 38, None, 40, 38, 33, 33, None, 31, 31, None, 36, 38], BEAT / 2, BEAT / 2 * 0.8)
cluster_notes, cluster_end = chords([[48, 55, 62, 64]], 10.0, stagger=0.15)
flutter_notes, flutter_end = chords([[60, 64, 67, 71], [57, 60, 64, 69]], 2 * BEAT * 1.5)
PHRASES = {
    "pad": (pad_notes, pad_end + 2.5, 0.05, 1.5),
    "organ": (organ_notes, organ_end + 1.5, 0.01, 0.4),
    "long": (long_notes, long_end + 3.0, 0.3, 2.5),
    "lead": (clarinet_notes, clarinet_end + 1.5, 0.01, 0.25),
    "bell": (bell_notes, bell_end + 3.0, 0.002, 2.5),
    "bass": (bass_notes, bass_end + 1.0, 0.003, 0.15),
    "texture": (cluster_notes, cluster_end + 3.0, 0.4, 2.5),
    "keys": (flutter_notes, flutter_end + 2.0, 0.005, 1.0),
}
FERMI = {"Breath": "pad", "Organ": "organ", "Tsingou": "long", "Clarinet": "lead", "Bell": "bell",
         "Pluck": "bass", "Thermal": "texture", "Flutter": "keys"}
# Hexagon in the same roles (engine, preset, phrase).
HEXAGON = [("orbit", "Dusk Pad", "pad"), ("hollow", "Choir", "pad"), ("orbit", "Drawbars", "organ"),
           ("wane", "Clean", "lead"), ("moth", "Bell", "bell"), ("bloom", "Glass Bells", "bell"),
           ("bend", "Wah Bass", "bass"), ("moth", "Marimba", "bass"), ("bloom", "Fog", "texture"),
           ("hollow", "Whisper", "texture")]


def scene(path, phrase, controls):
    notes, seconds, attack, release = PHRASES[phrase]
    lines = [f"{seconds:.3f} {attack} {release}", " ".join(f"{v:.5f}" for v in controls)]
    lines += [f"{a:.4f} {b:.4f} {m}" for a, b, m in notes]
    pathlib.Path(path).write_text("\n".join(lines) + "\n")


def controls_of(definition, preset):
    values = definition["presets"][preset]
    out = [values[p["id"]] for p in definition["parameters"]]
    return out + [0.0] * (8 - len(out))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--pads", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="fermi-phrases-"))
    include = f"-I{args.sdk}/sdk/include"

    def build(engine_dir, ns):
        exe = work / f"render-{ns}"
        if not exe.exists():
            cmd = ["c++", "-std=c++11", "-O2", include, f"-I{engine_dir}", f'-DENGINE="{engine_dir}/engine.cpp"', f"-DNS={ns}"]
            subprocess.run(cmd + [str(ROOT / "tools/render.cc"), "-o", str(exe)], check=True)
        return exe

    rows = []
    fermi = json.loads((ROOT / "definition.json").read_text())
    exe = build(ROOT, "fermi")
    for preset, phrase in FERMI.items():
        wav = out / f"fermi-{preset.lower()}.wav"
        scene(work / "scene.txt", phrase, controls_of(fermi, preset))
        subprocess.run([str(exe), str(work / "scene.txt"), str(wav)], check=True)
        rows.append(("Fermi " + preset, phrase, measure.measure(wav, PHRASES[phrase][0])))
    for engine, preset, phrase in HEXAGON:
        d = pathlib.Path(args.pads) / engine
        definition = json.loads((d / "definition.json").read_text())
        exe = build(d, engine)
        wav = out / f"hexagon-{engine}-{preset.lower().replace(' ', '-')}.wav"
        scene(work / "scene.txt", phrase, controls_of(definition, preset))
        subprocess.run([str(exe), str(work / "scene.txt"), str(wav)], check=True)
        rows.append((f"{engine.capitalize()} {preset}", phrase, measure.measure(wav, PHRASES[phrase][0])))
    table = measure.table(rows)
    (out / "measurements.md").write_text(table)
    print(table)


if __name__ == "__main__":
    main()
