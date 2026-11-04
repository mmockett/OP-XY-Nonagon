#!/usr/bin/env python3
"""Checks that the files the sample engines share are byte-identical in every
copy: amp_curve.h, sample_view.h and tools/sample_view.py in ghost/ and
recur/, and, given its folder (or found beside this repository as
../opxy-grains-release), Grains' copies too.

    python3 tools/check_shared.py [grains folder]

Exit status 1 when any copy differs (or one is missing), naming it."""
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = {"amp_curve.h": ["ghost", "recur"], "sample_view.h": ["ghost", "recur"], "tools/sample_view.py": ["ghost", "recur"]}
folders = {name: [ROOT / m for m in machines] for name, machines in SHARED.items()}
grains = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "opxy-grains-release"
if grains.is_dir():
    for name in folders:
        folders[name].append(grains)
elif len(sys.argv) > 1:
    sys.exit(f"check_shared.py: {grains} is not a folder")
bad = False
for name, places in folders.items():
    sums = {}
    for folder in places:
        path = folder / name
        sums[path] = hashlib.sha1(path.read_bytes()).hexdigest() if path.is_file() else "missing"
    same = len(set(sums.values())) == 1 and "missing" not in sums.values()
    bad = bad or not same
    print(("ok  " if same else "FAIL") + f" {name}: " + ", ".join(f"{p.relative_to(p.parents[len(name.split('/'))])}"
                                                            f" {s[:12]}" for p, s in sums.items()))
sys.exit(1 if bad else 0)
