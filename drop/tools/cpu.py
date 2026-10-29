#!/usr/bin/env python3
"""The SDK's CPU estimate, taken steadily on a busy computer: builds the same
bench (Drop's render at every control's nine positions, its slowest setting
kept) and reference loop as `sdk.py test`, then runs them alternately many
times and keeps the fastest of each, since other work only ever slows a run.

    python tools/cpu.py --sdk ../open-xy [--cxx clang++] [--runs 15]"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--sdk", required=True)
parser.add_argument("--cxx", default="c++")
parser.add_argument("--runs", type=int, default=15)
args = parser.parse_args()
sys.path.insert(0, args.sdk)
from tooling import cpu, machines  # noqa: E402

scratch = pathlib.Path(tempfile.mkdtemp(prefix="drop-cpu-"))
machine = machines.machine_at(ROOT)
cpu.block_cost(machine, args.cxx, scratch)        # builds the bench
cpu.reference_cost(args.cxx, scratch)             # and the reference
bench = scratch / ("cpu-" + machine.id) / "bench"
reference = scratch / "cpu-reference" / "reference"
best_bench, best_reference = 1e30, 1e30
for _ in range(args.runs):
    best_reference = min(best_reference, float(subprocess.run([str(reference)], capture_output=True, text=True).stdout))
    best_bench = min(best_bench, float(subprocess.run([str(bench)], capture_output=True, text=True).stdout))
estimate = best_bench / best_reference * cpu.REFERENCE_INSTRUCTIONS
print(f"{machine.id}: about {estimate:,.0f} Blackfin instructions per voice block at its slowest setting "
      f"(fastest of {args.runs} alternate runs: {best_bench:.0f} ns a block against the reference's {best_reference:.1f} ns)")
