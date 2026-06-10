# mRTOS Reference Manual

mRTOS is a minimal fixed-priority preemptive real-time kernel for the
MSP430FR59xx family, written as a portable C core plus a small per-port
layer. This manual is the complete reference: architecture, API,
design decisions and their trade-offs, hardware constraints, build
integration, and the three-stage verification strategy (host, ISA
simulator, target).

## Chapters

| # | Chapter | Contents |
|---|---|---|
| 1 | [Architecture](01-architecture.md) | Kernel objects, scheduler, data structures, locking model, ISR safety |
| 2 | [API Reference](02-api-reference.md) | Every public function, type, macro, return code, and usage constraint |
| 3 | [The Port Contract](03-port-contract.md) | What a port must provide; how the three existing ports implement it |
| 4 | [MSP430FR59xx Port](04-msp430-port.md) | Context layout, tick, the synthesized yield interrupt, low power, FR5994 bring-up |
| 5 | [Limitations](05-limitations.md) | Deliberate scope cuts, hardware constraints, and the failure modes they imply |
| 6 | [Build & Integration](06-build-integration.md) | CMake middleware packaging, toolchain setup, Makefiles, size optimization |
| 7 | [Verification](07-verification.md) | Test framework, host (POSIX) testing, ISA simulation, on-target checklist, coverage map |

## Reading guide

- **Integrating mRTOS into a product?** Read chapters 6, 2, 5 — in that
  order.
- **Porting to a new MCU?** Chapters 1, 3, 4.
- **Reviewing or extending the kernel?** Chapters 1, 5, 7.
- **Acting as tester / release gatekeeper?** Chapter 7 and
  [doc/VALIDATION.md](../VALIDATION.md) (the executable test plan this
  manual explains).

## Document conventions

- `code` identifiers refer to symbols in `kernel/mrtos.h` /
  `kernel/mrtos.c` unless a path is given.
- "Tick" means one period of the kernel time base
  (`MRTOS_CFG_TICK_HZ`, default 1 kHz → 1 ms).
- "Critical section" always means the single global interrupt lock
  (`port_irq_save()`/`port_irq_restore()`); mRTOS has no other lock.
