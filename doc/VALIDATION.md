# mRTOS validation plan

Two stages: an automated host suite that anyone can run in minutes, and a
manual on-target checklist for the LaunchPad MSP-EXP430FR5994. A release
is "validated" when **all** automated checks pass and the on-target
checklist is green on real silicon.

## 1. Automated suite (host)

Prerequisites: gcc, cmake ≥ 3.20, [uv](https://docs.astral.sh/uv/).

```sh
uv run tools/run_tests.py            # host suite only
uv run tools/run_tests.py --sim      # + same suite on the MSP430 ISA (simulator)
uv run tools/run_tests.py --target   # + MSP430FR5994 cross-build
```

Toolchain for `--target` (no registration needed, public TI mirror):

```sh
mkdir -p ~/toolchains && cd ~/toolchains
curl -O https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2
curl -O https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip
tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2
python3 -m zipfile -e msp430-gcc-support-files-1.212.zip .
```

With this layout (`~/toolchains/`), every tool in the repo
auto-detects the toolchain — no environment variables required. Only
for non-standard locations: export `MSP430_GCC_DIR` /
`MSP430_SUPPORT_DIR` (or `source tools/env.sh`).

### What the suite covers

| Binary | Scope |
|---|---|
| `test_unit_task` | creation/priority order, prio clamping, task retirement, sleep accuracy, `now()` monotonicity, `sleep(0)`≡yield, equal-prio yield alternation, `MRTOS_MS` |
| `test_unit_sem` | initial count, poll take, limit saturation, block/give, exact timeout, wake by priority, FIFO within a priority, timeout cleanup |
| `test_unit_mutex` | uncontended lock, `ERR_OWNER` paths (recursion, foreign unlock), poll fail, waiter timeout leaves owner intact, PI on READY owner, PI on BLOCKED owner (wait-list re-sort), multi-waiter hand-off in priority order |
| `test_unit_queue` | poll on empty/full, FIFO across wraparound, copy semantics, struct items, recv/send timeouts, blocked sender completion |
| `test_unit_stack` | stack painting, high-water marking (`mrtos_stack_unused`), overflow absence; on the simulator the marks are genuine MSP430 frame sizes |
| `test_integration` | 2-producer/2-consumer stress (no loss, no duplication, per-stream order), same-tick timeout-vs-give race (exactly-one-token invariant), Mesa wakeup steal + re-block, priority inversion bounded by inheritance |
| `test_kernel` | original 12-assertion smoke test |

Pass criterion: `ctest` reports 6/6; the runner exits 0.

### Simulator stage (`--sim`)

QEMU has no MSP430 support; the practical simulator is the GNU one
(`msp430-elf-run`), which ships inside TI's msp430-gcc. It emulates the
CPU/CPUX instruction set but **no peripherals or interrupts**, so the
fr59xx port can't run on it. Instead, `port/msp430sim/` is a cooperative
port (like POSIX) whose context switch is **real CPUX assembly**
(`PUSHM.A`/`POPM.A`, SP parked in the TCB — the same register protocol as
the fr59xx ISRs). Running the suite there validates what the host can't:

- the kernel compiled by msp430-gcc at `-Os` (16-bit `int`, 16-bit
  pointers, real integer promotion/overflow behavior);
- 20-bit register save/restore and stack-frame layout on the actual ISA.

Build quirk, documented in `port/msp430sim/port.c`: the simulator aborts
if SP ever drops below the heap break (which starts at `_end`), so task
stacks live in a fixed pool at 0xA000–0xEFFF rather than in the
caller-supplied `.bss` arrays.

### Micro-benchmark (`tools/bench.py`)

`bench/bench.c` brackets each kernel primitive between two empty marker
functions; `tools/bench.py` drives `msp430-elf-gdb`'s built-in
simulator to count single-stepped instructions per interval.
Deterministic to the instruction (ideal for CI regression tracking),
**not** cycle-accurate (1–6 cycles per MSP430 instruction, FRAM wait
states unmodeled). Reference values at `-Os`, msp430-gcc 9.3.1.11:

| Metric | Instructions |
|---|---|
| one-way context switch (yield) | ~52 |
| sem_give → waiter running (full hand-off) | ~132 |
| queue send / recv (poll) | ~77 |
| mutex lock + unlock (uncontended) | ~73 |
| tick, no sleepers | ~45 |
| tick, 8 sleepers | ~56 (O(1) delta list: not 8×) |

On target the same markers become GPIO toggles: cycle-exact numbers
via logic analyzer or Timer_A reads — planned with the HIL bench.

### Known coverage gap (by design)

Both the POSIX port and the simulator port are **cooperative**: ticks
advance only while idle runs, so asynchronous preemption (a tick landing
between two arbitrary instructions) never happens off-target. Races that
depend on it — ISR-driven `sem_give`, tick-vs-pended-yield ordering,
time-slice preemption of a CPU-bound task — are only exercised on target
(section 2, T4–T6). Also not simulated: Timer_A, LPM behavior, FRAM wait
states.

## 2. On-target checklist (MSP-EXP430FR5994)

Flash & run: `make run` (see `doc/DEBUG.md` for one-time setup;
`tools/check_board.sh` diagnoses the USB/probe chain step by step).

| # | Test | Procedure | Pass criterion |
|---|---|---|---|
| T1 | Boot + tick | Power the board | LED1 (P1.0) blinks steadily at 1 Hz (toggle every 500 ms; verify a few cycles against a watch) |
| T2 | Tick accuracy | Scope/logic analyzer on P1.0 | Period 1000 ms ± 1 %; jitter < 1 tick (1 ms) |
| T3 | ISR → semaphore | Press S1 (P5.6) repeatedly, vary cadence | LED2 (P1.1) toggles once per press; no missed/double toggles (30 ms debounce) |
| T4 | ISR-safe API under load | Hold S1 bursts while LED1 blinks | No hang, no blink disturbance — exercises `sem_give` from PORT5 ISR + deferred yield after RETI |
| T5 | Queue under preemption | Debugger: watch `cons_checksum` | Strictly increasing by n(n+1)/2 pattern (sum of 0,1,2,…); producer/consumer at equal prio time-slice correctly |
| T6 | Soak | Leave running ≥ 1 h with periodic S1 presses | Still responsive; LED1 still 1 Hz; `cons_checksum` still advancing |
| T7 | Stack guard | Debugger: set a task stack word `stack[0]` ≠ 0x5AFE | Execution traps in `mrtos_stack_overflow_hook` with `t->name` identifying the task |
| T8 | Low power | Ammeter on the 3V3 jumper, no button activity | Average current consistent with LPM0 idle (CPU mostly asleep, wakes 1000×/s for tick) |

Record results per release tag (toolchain version, board revision, date).

## 3. Build configurations validated

| Configuration | Command | Status checked by |
|---|---|---|
| Host, Debug | `cmake -B build-host && ctest --test-dir build-host` | runner |
| Simulator, `-Os` | `cmake --toolchain cmake/msp430sim.cmake …` + CTest via `msp430-elf-run` | runner (`--sim`) |
| Target, `-Os` | `cmake --toolchain cmake/msp430fr5994.cmake …` | runner (`--target`), SRAM bound asserted |
| Legacy Makefiles | `make -f Makefile.host run` · `make GCC_DIR=…` | manual, kept for non-CMake consumers |
