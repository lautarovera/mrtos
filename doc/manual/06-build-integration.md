# Chapter 6 — Build & Integration

## 6.1 Philosophy: mRTOS as middleware

mRTOS is meant to be vendored into a product tree (submodule, subtree,
or copy) and consumed as a static library — like any third-party
middleware. The repository therefore has **two independent build
paths**:

- **CMake** (primary): library target + toolchain files + test suite.
- **Plain Makefiles** (`Makefile`, `Makefile.host`): kept for
  consumers without CMake; two files, no abstractions, easy to
  transliterate into any other build system.

## 6.2 Consuming mRTOS from a parent CMake project

```cmake
# Your tree:
#   third_party/mrtos/        <- this repo
#   config/mrtos_config.h     <- YOUR kernel configuration (§2.1)

set(MRTOS_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config)
add_subdirectory(third_party/mrtos)
target_link_libraries(app PRIVATE mrtos::mrtos)
```

That is the entire integration. Details:

- **`mrtos::mrtos`** is a STATIC library: `kernel/mrtos.c` plus the
  selected port's `port.c`. Its include directories (kernel + port +
  your config dir) propagate as PUBLIC usage requirements — your code
  just `#include "mrtos.h"`.
- **`MRTOS_CONFIG_DIR`** points at the directory containing *your*
  `mrtos_config.h`. The application owns the configuration; the
  in-tree configs (`test/`, `test/sim/`, `app/`) are only fallbacks so
  the repo builds standalone.
- **Port selection** is automatic: cross builds
  (`CMAKE_SYSTEM_NAME=Generic`) get `msp430fr59xx`, native builds get
  `posix`. Override explicitly with `-DMRTOS_PORT=<name>`.
- Tests and the demo app are gated behind `PROJECT_IS_TOP_LEVEL`, so
  adding mRTOS to your superproject adds **one library target and
  nothing else** — no test pollution, no surprise executables.

## 6.3 Toolchain acquisition

TI's GCC for MSP430 is downloadable from a **public, no-registration
mirror** (used and verified during this project's validation):

```sh
mkdir -p ~/toolchains && cd ~/toolchains
curl -O https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2
curl -O https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip
tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2
python3 -m zipfile -e msp430-gcc-support-files-1.212.zip .
```

Two artifacts, and you need both:

| Artifact | Contents |
|---|---|
| `msp430-gcc-…-linux64.tar.bz2` (~64 MB) | the compiler: `msp430-elf-gcc`, binutils, gdb, newlib — **and `msp430-elf-run`, the ISA simulator** (§7.5) |
| `msp430-gcc-support-files-….zip` (~22 MB) | per-device headers (`msp430fr5994.h`) and linker scripts (`msp430fr5994.ld`) — packaged separately by TI |

Point the build at them via `MSP430_GCC_DIR` and `MSP430_SUPPORT_DIR`
(environment or `-D`). TI's *full installer* bundles the support files
inside `<gcc>/include`, which is the toolchain file's default for
`MSP430_SUPPORT_DIR`.

## 6.4 The three build configurations

```sh
# 1. Host (tests, native gcc)
cmake -B build-host && cmake --build build-host
ctest --test-dir build-host

# 2. ISA simulator (tests on the MSP430 instruction set)
cmake -B build-sim --toolchain cmake/msp430sim.cmake \
      -DMSP430_GCC_DIR=$HOME/toolchains/msp430-gcc-9.3.1.11_linux64
cmake --build build-sim && ctest --test-dir build-sim

# 3. Target image (MSP430FR5994, -Os)
cmake -B build-msp430 --toolchain cmake/msp430fr5994.cmake \
      -DMSP430_GCC_DIR=... -DMSP430_SUPPORT_DIR=... -DCMAKE_BUILD_TYPE=Release
cmake --build build-msp430        # produces mrtos_fr5994.elf + size report
```

Or all of it in one command (§6.7).

Reference footprint at `-Os` (msp430-elf-gcc 9.3.1, kernel + port +
demo app): **text 2542 B, data 4 B, bss 1300 B** — SRAM usage 1304 of
4096 bytes, dominated by the demo's four 192-byte task stacks plus the
idle stack.

## 6.5 Size optimization decisions

- **`-Os` everywhere on target.** It is the strongest size
  optimization msp430-gcc offers. LTO was evaluated and rejected: at
  2.5 KB of text in a single translation-unit-sized kernel it saved
  nothing measurable, and it interacts poorly with `naked` ISRs and
  linker-placed vectors.
- **`-ffunction-sections -fdata-sections` + `--gc-sections`**: any
  kernel function the application never references (e.g. all of the
  queue API in a semaphore-only product) is dropped at link time. The
  middleware promise — "you pay for what you call" — is enforced by
  the linker, not by configuration macros.
- **`-mhwmult=f5series`**: use the F5-series hardware multiplier.

## 6.6 Build-system pitfalls encountered (and fixed)

Documented so they are not re-introduced:

1. **`-mmcu` must appear exactly once on a combined compile+link
   line.** The driver translates `-mmcu` into `-T msp430fr5994.ld`;
   passing it via both CFLAGS and LDFLAGS made ld see the device
   script twice and abort ("linker script … appears multiple times").
   The original Makefile shipped with this bug — the target had never
   actually linked. Fix: `-mmcu` lives in CFLAGS only.
2. **CMake toolchain files are re-read by `try_compile`,** which does
   *not* inherit `-D` cache variables. `cmake/msp430*.cmake` therefore
   stash `MSP430_GCC_DIR`/`MSP430_SUPPORT_DIR` into the environment on
   first read — the only carrier that survives into the sub-project.
3. **CMake appends platform defaults after `*_INIT` flags,** so
   `CMAKE_C_FLAGS_RELEASE_INIT "-Os"` silently became `-Os … -O3` (and
   `-O3` wins as the last flag). The toolchain files pin
   `CMAKE_C_FLAGS_RELEASE`/`_MINSIZEREL` in the cache instead. Verify
   with `cmake --build … --verbose` if you ever touch these.

## 6.7 The validation runner

`tools/run_tests.py` is a single-file Python script with inline
metadata (PEP 723) — `uv` resolves the interpreter; there are zero
third-party dependencies:

```sh
uv run tools/run_tests.py                   # host suite
uv run tools/run_tests.py --sim             # + suite on the ISA simulator
uv run tools/run_tests.py --target          # + FR5994 image build
uv run tools/run_tests.py --clean --sim --target   # full pipeline
```

It configures/builds each tree, runs CTest, parses results, enforces
the SRAM bound on the target image (`data + bss ≤ 4096`), prints a
summary, and exits nonzero on any failure — suitable as a CI job
verbatim. Expected steady-state output:

```
host tests : 6/6 passed
sim tests  : 5/5 passed
target     : build OK, text=2542 data=4 bss=1300 (SRAM used 1304/4096)
```

## 6.8 Legacy Makefiles

- `make -f Makefile.host run` — builds and runs the original smoke
  test (`test/test_kernel.c`) with native gcc.
- `make GCC_DIR=… SUPPORT_DIR=…` — builds `mrtos_fr5994.elf` at `-Os`
  and prints section sizes.

They build the same sources with the same flags as CMake; they are a
fallback, not a second source of truth.

## 6.9 Flashing

```sh
mspdebug tilib "prog build-msp430/mrtos_fr5994.elf"
```

`tilib` drives the eZ-FET debug probe built into the LaunchPad; an
MSP-FET works identically. (`mspdebug` is in the Ubuntu/Debian
archives.)
