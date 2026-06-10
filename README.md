# mRTOS — minimal preemptive RTOS for MSP430FR59xx

A from-scratch real-time kernel designed for the MSP430FR59xx family
(CPUX core, FRAM), with a concrete implementation for the **MSP430FR5994**
and a POSIX host port that runs the unmodified kernel under a unit-test
suite.

```
mrtos/
├── kernel/                 portable core (no CPU-specific code)
│   ├── mrtos.h             public API + port contract
│   └── mrtos.c             scheduler, tasks, sem, mutex, queue
├── port/
│   ├── msp430fr59xx/       real port: CPUX asm, Timer_A0, LPM0 idle
│   └── posix/              host port: ucontext, drives the test suite
├── app/main_fr5994.c       LaunchPad MSP-EXP430FR5994 demo
├── test/test_kernel.c      kernel test suite (runs on the host)
├── Makefile                target build (TI msp430-gcc)
└── Makefile.host           host test build (gcc)
```

> **Full documentation:** the reference/user manual lives in
> [doc/manual/](doc/manual/README.md) — architecture, API, port
> contract, hardware constraints, build integration, and the complete
> verification story. The executable test plan is
> [doc/VALIDATION.md](doc/VALIDATION.md). This README is the short
> version.

## Design

### Scheduling model

Fixed-priority preemptive scheduler with 8 levels (0 = idle, 7 = highest)
and round-robin among equal priorities every `MRTOS_CFG_SLICE_TICKS`.

Data structures, chosen for O(1) hot paths on a 16-bit CPU:

- One **circular doubly-linked ready list per priority** plus an 8-bit
  ready bitmap. Picking the next task is a branch-reduced 3-step
  highest-bit scan of one byte — no loops over TCBs.
- The running task **stays in its ready list**; time-slicing is just
  rotating `ready_head[prio]`, and preemption is picking a higher head.
- **Wait lists** (semaphore/mutex/queue) reuse the same `next/prev`
  links — a task is in exactly one of {ready list, wait list} — sorted
  by descending priority, FIFO within a priority.
- Sleeps and timeouts live in a **delta-sorted delay list**: the tick
  handler decrements only the head, so tick cost is independent of the
  number of sleeping tasks.

Everything is **statically allocated**: the application owns TCBs and
stacks (FRAM-friendly, no heap, deterministic memory map).

### Concurrency primitives

- **Counting semaphore** — `give()` is ISR-safe.
- **Mutex with priority inheritance** — single level: the owner is
  raised to the blocker's priority (re-queued if ready, wait-list
  re-sorted if blocked) and demoted at unlock, with direct hand-off to
  the highest waiter. Documented limitations: no recursion, no
  transitive inheritance chains, no de-inheritance when a waiter times
  out before unlock.
- **Message queue** — fixed-size items, copy semantics, Mesa-style
  wakeups (woken waiters re-check the condition in a loop, so the
  timeout restarts on each retry; documented).

The whole kernel is protected by a single global critical section
(`port_irq_save/restore` = GIE on MSP430). Because ISRs run with the
same exclusion, **the same API is ISR-safe** for non-blocking calls:
`mrtos_sem_give()` and `mrtos_queue_send/recv(..., 0)` work unchanged
from interrupt handlers.

### The port contract

A port provides exactly six functions plus one type (see the bottom of
`mrtos.h`): `port_irq_save/restore`, `port_yield`, `port_stack_init`,
`port_start`, `port_idle`, and `port_stack_t` (optionally `PORT_TCB_EXT`
for private TCB fields). The POSIX port (≈70 lines) exists to prove the
contract is real: the kernel compiles and passes its test suite on a
second, radically different "architecture" without a single `#ifdef`.

## MSP430FR59xx port

### Context layout

CPUX registers are 20 bits. The context is R4–R15 saved with
`PUSHM.A #12, R15` (2 words per register) beneath the hardware interrupt
frame, which per SLAU367 is `PC[15:0]` pushed first, then `SR` with
`PC[19:16]` packed into bits 15:12. `port_stack_init()` fabricates this
exact frame for new tasks, with GIE set in the synthetic SR and the
argument pre-loaded into the R12 slot (first-argument register in the
MSP430 ABI). The TCB's `sp` field **must be its first member** — the
assembly accesses it at offset 0.

### Tick and yield

- **Tick**: Timer_A0 CCR0, up mode, SMCLK/8 (1 MHz timebase at 8 MHz
  SMCLK), `TIMER0_A0_VECTOR`. 1 kHz tick → `CCR0 = 999`, exact.
- **Yield**: MSP430 has no PendSV, so the port synthesizes one.
  `TA0CCR1` is parked at `0xFFFF`, unreachable in up mode, which makes
  `TA0CCTL1.CCIFG` a **software-only interrupt source** on
  `TIMER0_A1_VECTOR`. `port_yield()` just sets the flag:
  - from a task with interrupts enabled it fires immediately;
  - inside a critical section it pends until GIE is restored;
  - inside any application ISR it pends and fires right after the outer
    `RETI` — which is what makes the unified ISR-safe API work.

Both kernel ISRs are naked: save context → store SP into
`mrtos_cur->sp` → hop onto a **dedicated ISR stack** → call the C
handler (which may retarget `mrtos_cur`) → reload SP from the possibly
different `mrtos_cur` → `POPM.A` → `RETI`. Per-task ISR overhead on the
task stack is only the 52-byte save area.

### Idle and low power

`port_idle()` enters LPM0 (`CPUOFF`); SMCLK stays alive so the tick
keeps running. Because idle's *saved* SR carries `CPUOFF`, re-scheduling
idle transparently re-enters sleep on `RETI` — no special casing.
A tickless variant (Timer_A on ACLK, LPM3) is the natural extension and
is compatible with this architecture.

### FR5994 specifics (app layer)

`app/main_fr5994.c` does the family-correct bring-up: WDT stop first,
all GPIO driven low, `PM5CTL0 &= ~LOCKLPM5` (mandatory on FRAM parts),
DCO at 8 MHz (`CSCTL1 = DCOFSEL_3 | DCORSEL`, verified against TI
examples). At ≤ 8 MHz FRAM needs 0 wait states; **above 8 MHz you must
set `FRCTL0 = FRCTLPW | NWAITS_1` before raising the clock**. Demo:
LED1 blinker, button-driven LED2 via semaphore from the PORT5 ISR
(S1 = P5.6 on the LaunchPad), and a producer/consumer queue pair.

## Verification status — read this

What is verified by execution (host, `make -f Makefile.host run`):

- priority scheduling order, delta-list wake order, semaphore
  block/give, semaphore timeout (exact tick count), mutex priority
  inheritance + demotion + hand-off, queue ordering through a full
  buffer, recv timeout, round-robin via yield. 12/12 assertions pass.

What is **not** verified here, stated plainly:

- The MSP430 build itself: no MSP430 toolchain exists in the
  environment where this was produced. The assembly was hand-checked
  against SLAU367 (frame layout, `PUSHM.A`/`POPM.A` ordering and
  20-bit width) but has not been assembled or run on silicon.
- The `__attribute__((naked, interrupt(VECTOR)))` combination on
  msp430-elf-gcc. If your toolchain rejects it, move the two ISR bodies
  to a `.S` file and place the vectors via
  `.section __interrupt_vector_<n>` (note in `port.c`).
- Asynchronous preemption paths (tick firing at arbitrary instruction
  boundaries) — the host port is cooperative by design. The race
  analysis is in the port comments (e.g., a tick that fires before a
  pended yield performs a redundant but harmless pick).

## Known limitations (deliberate scope cuts)

- Small memory model only (code/pointers ≤ 64 KB; the FR5994's upper
  FRAM is unused). Large-model support changes `mov.w → mov[x].a` and
  `call → calla` in the port.
- Mutex PI is single-level, non-recursive, no timeout de-inheritance.
- Queue timeouts restart on Mesa re-check.
- No task deletion (returning tasks are retired to SUSPENDED forever).
- Application ISRs run on the interrupted task's stack — keep them lean
  or grow task stacks accordingly.

## Building

### CMake (recommended — middleware-style integration)

mRTOS builds as a static library you drop into a parent project:

```cmake
set(MRTOS_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config)  # your mrtos_config.h
add_subdirectory(third_party/mrtos)
target_link_libraries(app PRIVATE mrtos::mrtos)
```

The port is picked automatically (POSIX for native builds, msp430fr59xx
when cross-compiling); override with `-DMRTOS_PORT=<name>`.

Standalone host tests:

```sh
cmake -B build-host && cmake --build build-host
ctest --test-dir build-host
```

Standalone target build (TI msp430-gcc, see `doc/VALIDATION.md` for
toolchain download):

```sh
cmake -B build-msp430 --toolchain cmake/msp430fr5994.cmake \
      -DMSP430_GCC_DIR=/path/to/msp430-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build-msp430        # -Os; prints section sizes
```

The same suite also runs on the **real MSP430 ISA** without hardware,
via the GNU simulator bundled with msp430-gcc (`msp430-elf-run`) and the
`port/msp430sim` port (real CPUX `PUSHM.A`/`POPM.A` context switch):

```sh
cmake -B build-sim --toolchain cmake/msp430sim.cmake \
      -DMSP430_GCC_DIR=/path/to/msp430-gcc
cmake --build build-sim && ctest --test-dir build-sim
```

One-shot validation runner (host suite + optional simulator suite and
target build):

```sh
uv run tools/run_tests.py [--sim] [--target] [--clean]
```

### Plain Makefiles (kept for non-CMake consumers)

Host tests: `make -f Makefile.host run`

Target: install TI's msp430-gcc + support files, then
`make GCC_DIR=/path/to/msp430-gcc`. Flash `mrtos_fr5994.elf` with
MSP-FET/mspdebug (`tilib`, eZ-FET on the LaunchPad).

## Test suite

`test/` holds six host-run binaries: unit tests per primitive
(`test_unit_task`, `test_unit_sem`, `test_unit_mutex`,
`test_unit_queue`), an integration suite (`test_integration`:
producer/consumer stress, same-tick timeout-vs-give race, Mesa wakeup
stealing, priority-inversion bounding) and the original smoke test.
The full validation plan, including the manual on-target checklist for
the LaunchPad, is in `doc/VALIDATION.md`.
