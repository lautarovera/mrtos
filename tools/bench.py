#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""
mRTOS micro-benchmark runner (simulator).

Counts the instructions each kernel primitive executes on the MSP430
ISA, by driving msp430-elf-gdb's built-in simulator: breakpoints on
bench_mark_a/b, single-step between them, one count per interval.
Deterministic to the instruction - two runs of the same binary always
agree - which makes the numbers ideal for regression tracking.

NOT cycle-accurate: MSP430 instructions take 1-6 cycles and FRAM wait
states are not modeled. Cycle-exact numbers come from the same markers
on real hardware (GPIO toggles + logic analyzer / Timer_A reads).

Usage:
  uv run tools/bench.py [--build] [--markdown out.md]

Needs MSP430_GCC_DIR and an existing build-sim tree (or --build).
"""
from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM_BUILD = ROOT / "build-sim"

# Contract with bench/bench.c: ids, order, samples.
SAMPLES = 3
METRICS = [
    ("baseline",  "marker overhead (subtracted from the rest)"),
    ("yield",     "one-way context switch between equal-prio tasks"),
    ("sem_wake",  "sem_give -> higher-prio waiter running (full hand-off)"),
    ("q_send",    "queue_send, poll, no waiter"),
    ("q_recv",    "queue_recv, poll, data present"),
    ("mutex",     "uncontended lock + unlock"),
    ("tick_0",    "mrtos_tick, empty delay list"),
    ("tick_8",    "mrtos_tick, 8 tasks sleeping (O(1) delta list)"),
]
N_INTERVALS = len(METRICS) * SAMPLES

GDB_SCRIPT = f"""\
set pagination off
set confirm off
target sim
load
break bench_mark_a
break bench_mark_b
run
set $i = 0
while $i < {N_INTERVALS}
  set $id = bench_id
  set $n = 0
  set $endpc = (long)bench_mark_b
  while (long)$pc != $endpc
    stepi
    set $n = $n + 1
  end
  printf "BENCH id=%d insns=%d\\n", $id, $n
  set $i = $i + 1
  if $i < {N_INTERVALS}
    continue
  end
end
quit
"""


def main() -> None:
    ap = argparse.ArgumentParser(description="mRTOS simulator benchmark")
    ap.add_argument("--build", action="store_true",
                    help="(re)configure and build build-sim first")
    ap.add_argument("--markdown", type=Path,
                    help="also write the table to this file")
    opts = ap.parse_args()

    gcc_dir = os.environ.get("MSP430_GCC_DIR")
    if not gcc_dir:
        hits = sorted(Path.home().glob("toolchains/msp430-gcc-*_linux64"))
        if hits:
            gcc_dir = str(hits[-1])
            os.environ["MSP430_GCC_DIR"] = gcc_dir
    if not gcc_dir:
        sys.exit("ERROR: no msp430-gcc found (set MSP430_GCC_DIR "
                 "or install under ~/toolchains, see doc/VALIDATION.md)")

    if opts.build:
        subprocess.run(
            ["cmake", "-B", SIM_BUILD, "-S", ROOT,
             "--toolchain", ROOT / "cmake" / "msp430sim.cmake",
             "-DCMAKE_BUILD_TYPE=Release", f"-DMSP430_GCC_DIR={gcc_dir}"],
            check=True)
    subprocess.run(["cmake", "--build", SIM_BUILD, "--target", "bench"],
                   check=True)

    elf = SIM_BUILD / "bench"
    with tempfile.NamedTemporaryFile("w", suffix=".gdb", delete=False) as f:
        f.write(GDB_SCRIPT)
        script = f.name
    p = subprocess.run(
        [ROOT / "tools" / "msp430-gdb.sh", elf, "-batch", "-x", script],
        capture_output=True, text=True, stdin=subprocess.DEVNULL)
    os.unlink(script)

    counts: dict[int, list[int]] = {}
    for m in re.finditer(r"BENCH id=(\d+) insns=(\d+)", p.stdout):
        counts.setdefault(int(m.group(1)), []).append(int(m.group(2)))

    if sorted(counts) != list(range(len(METRICS))) or \
       any(len(v) != SAMPLES for v in counts.values()):
        print(p.stdout[-2000:], file=sys.stderr)
        sys.exit(f"ERROR: expected {len(METRICS)}x{SAMPLES} intervals, "
                 f"got {{id: len(v) for id, v in counts.items()}}")

    base = statistics.median(counts[0])
    lines = ["| Metric | Instructions | Notes |",
             "|---|---|---|"]
    for mid, (name, desc) in enumerate(METRICS):
        if mid == 0:
            continue
        vals = [c - base for c in counts[mid]]
        spread = "" if len(set(vals)) == 1 else \
            f" (min {min(vals)}, max {max(vals)})"
        lines.append(f"| {name} | {statistics.median(vals):.0f}{spread} "
                     f"| {desc} |")
    lines.append(f"\n_Marker overhead {base:.0f} instructions, subtracted. "
                 f"{SAMPLES} samples/metric. Instruction counts on the GNU "
                 f"MSP430 simulator - deterministic, not cycle-accurate._")
    table = "\n".join(lines)
    print(table)
    if opts.markdown:
        opts.markdown.write_text(table + "\n")


if __name__ == "__main__":
    main()
