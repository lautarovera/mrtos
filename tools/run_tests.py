#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""
mRTOS validation runner.

  uv run tools/run_tests.py                 # host: build + unit/integration
  uv run tools/run_tests.py --target        # also cross-build for MSP430FR5994
  uv run tools/run_tests.py --clean         # wipe build dirs first

The host suite needs only cmake + a native gcc. The target build needs
TI's msp430-gcc; point MSP430_GCC_DIR (and, for the standalone tarball,
MSP430_SUPPORT_DIR) at it, e.g.:

  export MSP430_GCC_DIR=$HOME/toolchains/msp430-gcc-9.3.1.11_linux64
  export MSP430_SUPPORT_DIR=$HOME/toolchains/msp430-gcc-support-files/include

Exit code: 0 = everything requested passed, 1 = any failure.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST_BUILD = ROOT / "build-host"
TARGET_BUILD = ROOT / "build-msp430"
SIM_BUILD = ROOT / "build-sim"

# FR5994: 8 KiB classic FRAM window for code in the small memory model
# would be wrong to assert here; what we do bound is total FRAM (256 KiB)
# and SRAM (4 KiB + 4 KiB LEA). The .elf must at minimum fit SRAM.
SRAM_LIMIT = 4096


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(str(c) for c in cmd)}")
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, text=True,
                          capture_output=True, **kw)


def fail(msg: str) -> None:
    print(f"\nERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def ctest_suite(build_dir: Path, extra_cfg: list[str]) -> tuple[int, int, list[str]]:
    """Configure, build and run a CTest suite. Returns (passed, total, failed-names)."""
    p = run(["cmake", "-B", build_dir, "-S", ROOT, *extra_cfg])
    if p.returncode != 0:
        fail(f"configure failed:\n{p.stdout}{p.stderr}")
    p = run(["cmake", "--build", build_dir, "--parallel"])
    if p.returncode != 0:
        fail(f"build failed:\n{p.stdout}{p.stderr}")

    p = run(["ctest", "--test-dir", build_dir, "--output-on-failure"])
    print(p.stdout)
    # Result lines: "... test_name ...   Passed", "...***Failed" or
    # "...***Exception: SegFault". Anything that is not literally
    # "Passed" counts as a failure.
    tests = re.findall(r"Test\s+#\d+: (\S+) \.+\**\s*(\w+)", p.stdout)
    passed = [name for name, status in tests if status == "Passed"]
    failed = [name for name, status in tests if status != "Passed"]
    if p.returncode != 0 and not failed:
        failed = ["<ctest failed but no test parsed as failing>"]
    return len(passed), len(tests), failed


def target_build() -> str:
    gcc_dir = os.environ.get("MSP430_GCC_DIR")
    if not gcc_dir:
        fail("--target requested but MSP430_GCC_DIR is not set")
    args = ["cmake", "-B", TARGET_BUILD, "-S", ROOT,
            "--toolchain", ROOT / "cmake" / "msp430fr5994.cmake",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DMSP430_GCC_DIR={gcc_dir}"]
    support = os.environ.get("MSP430_SUPPORT_DIR")
    if support:
        args.append(f"-DMSP430_SUPPORT_DIR={support}")
    p = run(args)
    if p.returncode != 0:
        fail(f"target configure failed:\n{p.stdout}{p.stderr}")
    p = run(["cmake", "--build", TARGET_BUILD])
    if p.returncode != 0:
        fail(f"target build failed:\n{p.stdout}{p.stderr}")

    elf = TARGET_BUILD / "mrtos_fr5994.elf"
    p = run([Path(gcc_dir) / "bin" / "msp430-elf-size", elf])
    print(p.stdout)
    m = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)", p.stdout, re.M)
    if not m:
        fail("could not parse msp430-elf-size output")
    text, data, bss = (int(g) for g in m.groups())
    if data + bss > SRAM_LIMIT:
        fail(f"SRAM overflow: data+bss = {data + bss} > {SRAM_LIMIT}")
    return f"text={text} data={data} bss={bss} (SRAM used {data + bss}/{SRAM_LIMIT})"


def main() -> None:
    ap = argparse.ArgumentParser(description="mRTOS validation runner")
    ap.add_argument("--target", action="store_true",
                    help="also cross-build the MSP430FR5994 image")
    ap.add_argument("--sim", action="store_true",
                    help="also run the suite on the GNU MSP430 simulator")
    ap.add_argument("--clean", action="store_true",
                    help="remove build directories first")
    opts = ap.parse_args()

    if opts.clean:
        for d in (HOST_BUILD, TARGET_BUILD, SIM_BUILD):
            shutil.rmtree(d, ignore_errors=True)

    print("=== host test suite (POSIX port) ===")
    passed, total, failed = ctest_suite(HOST_BUILD, ["-DCMAKE_BUILD_TYPE=Debug"])

    sim_result = None
    if opts.sim:
        if not os.environ.get("MSP430_GCC_DIR"):
            fail("--sim requested but MSP430_GCC_DIR is not set")
        print("\n=== simulator test suite (MSP430 ISA, msp430-elf-run) ===")
        sim_result = ctest_suite(SIM_BUILD, [
            "--toolchain", str(ROOT / "cmake" / "msp430sim.cmake"),
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DMSP430_GCC_DIR={os.environ['MSP430_GCC_DIR']}"])

    size_report = None
    if opts.target:
        print("\n=== target build (MSP430FR5994, -Os) ===")
        size_report = target_build()

    any_failed = bool(failed) or total == 0
    print("\n=== summary ===")
    print(f"host tests : {passed}/{total} passed"
          + (f"  FAILED: {', '.join(failed)}" if failed else ""))
    if sim_result:
        s_passed, s_total, s_failed = sim_result
        any_failed |= bool(s_failed) or s_total == 0
        print(f"sim tests  : {s_passed}/{s_total} passed"
              + (f"  FAILED: {', '.join(s_failed)}" if s_failed else ""))
    if size_report:
        print(f"target     : build OK, {size_report}")
    elif opts.target:
        print("target     : FAILED")

    sys.exit(1 if any_failed else 0)


if __name__ == "__main__":
    main()
