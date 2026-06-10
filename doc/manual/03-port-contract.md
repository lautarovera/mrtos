# Chapter 3 — The Port Contract

## 3.1 The contract

A port is one header and one C file (`port/<name>/port.{h,c}`)
providing exactly one type and six functions:

```c
typedef ... port_stack_t;            /* stack cell: uint16_t on MSP430 */

uint16_t      port_irq_save(void);              /* enter critical sec. */
void          port_irq_restore(uint16_t key);   /* leave (restore key) */
void          port_yield(void);                 /* request a reschedule */
port_stack_t *port_stack_init(port_stack_t *base, size_t words,
                              void (*shell)(void *), void *arg);
void          port_start(void);                 /* tick on + first task */
void          port_idle(void);                  /* idle-loop body       */
/* optional: #define PORT_TCB_EXT  <extra TCB fields>                  */
```

Obligations, precisely:

- **`port_irq_save/restore`** — global interrupt mask with save/restore
  semantics (nestable by key, not by counter). On MCU ports this is the
  interrupt enable flag; on hosted ports where nothing is asynchronous
  it may legitimately be a no-op.
- **`port_yield`** — make the scheduler run "soon": either immediately
  (cooperative ports call `mrtos_sched_pick()` and switch on the spot)
  or deferred (MCU ports pend an interrupt that performs the switch
  once interrupts are re-enabled — this is what makes the API ISR-safe,
  §1.7). Callers always invoke it *inside* the critical section.
- **`port_stack_init`** — fabricate an initial context on a fresh stack
  such that the first time the scheduler switches to this task, control
  arrives in `shell(arg)` with a normal call environment. `shell` is
  the kernel's `task_shell`, `arg` is the TCB. Returns the initial
  saved-SP value to store in `tcb->sp`.
- **`port_start`** — start the tick source and transfer control to the
  already-picked first task. Never returns.
- **`port_idle`** — one iteration of "nothing to do". MCU ports sleep
  here; hosted ports advance simulated time here (with a consequence
  for testing, §7.3.1).
- **`PORT_TCB_EXT`** — optional extra TCB fields (the POSIX port stores
  a `ucontext_t` here). Keeps the core TCB free of port types.

The kernel offers ports two entry points in return: `mrtos_tick()`
(call once per tick, in tick-ISR context with the kernel's exclusion
guarantees) and `mrtos_sched_pick()` (recompute `mrtos_cur`).

## 3.2 Why the contract is this small

Each function is something the portable core *cannot* express: masking
interrupts, the shape of a stack frame, how control first enters a
task, what "sleep" means. Everything that *can* be portable (lists,
timeouts, priorities, all object semantics) is, so a port is
mechanical to write and small enough to review against the CPU manual.
The contract was kept falsifiable on purpose: three implementations
exist with zero `#ifdef`s in the core.

## 3.3 The three ports

| | `posix` | `msp430sim` | `msp430fr59xx` |
|---|---|---|---|
| Purpose | run the suite natively | run the suite on the real ISA | the actual product port |
| CPU | host (any) | MSP430 CPUX (GNU simulator) | MSP430 CPUX (silicon) |
| Context switch | `swapcontext()` (ucontext) | real `PUSHM.A/POPM.A` asm, call-based | real `PUSHM.A/POPM.A` asm, interrupt-based |
| Critical section | no-op | no-op | GIE bit |
| Tick source | `port_idle()` calls `mrtos_tick()` | same | Timer_A0 CCR0 ISR |
| Yield | immediate switch | immediate switch | pended software interrupt (§4.3) |
| Preemption | cooperative | cooperative | genuine asynchronous |
| ~Size | 55 lines | 100 lines | 156 lines |

### 3.3.1 POSIX port (`port/posix/`)

Each task is a `ucontext_t` (stored via `PORT_TCB_EXT`);
`port_stack_init` runs `makecontext` over the caller-supplied array,
`port_yield` is pick + `swapcontext`. `port_irq_save` is a no-op
because nothing is asynchronous: ticks happen only inside
`port_idle()`, i.e. when every task is blocked. A safety counter
(200 000 ticks) converts "everything blocked forever" — a deadlock —
into a clean exit code 2 instead of a hang, which CTest then reports.

ucontext stacks must be large (the host's libc runs on them), hence
the 8192-word (64 KiB) test stacks on this port only.

### 3.3.2 MSP430 simulator port (`port/msp430sim/`)

Same cooperative skeleton as POSIX, but the context switch is the real
thing — a naked function:

```asm
pushm.a #12, r15     ; save R4..R15, 20-bit, 2 words each
mov.w   r1,  @r12    ; old_tcb->sp = SP   (sp is TCB offset 0)
mov.w   r13, r1      ; SP = new task's saved sp
popm.a  #12, r15     ; restore R4..R15
ret                  ; resume at the new context's return address
```

This is the same register protocol the hardware port's ISRs use, minus
the interrupt frame (it is call-based: the return address on the stack
plays the role of the saved PC). `port_stack_init` fabricates the
matching frame: a trampoline address, then a 24-word `PUSHM.A` save
area.

One simulator-specific accommodation: task stacks are carved from a
fixed pool at 0xA000–0xEFFF instead of the caller's arrays, because the
GNU simulator kills the program whenever SP drops below the heap break
(see §7.5.4 for the full story). The caller's arrays are accepted and
ignored.

### 3.3.3 MSP430FR59xx port

Chapter 4, in full.

## 3.4 Writing a new port — checklist

1. Pick `port_stack_t` = the natural stack slot (usually the machine
   word).
2. Implement save/restore of the interrupt mask.
3. Decide the yield strategy: if the CPU has a software-pendable
   lowest-priority interrupt (PendSV on Cortex-M, the Timer_A CCR1
   trick on MSP430), use it and you get ISR-safety of the unified API;
   if not, a cooperative port is still useful for bring-up.
4. Write `port_stack_init` against your interrupt-frame layout. Get
   the first-argument register right (R12 on MSP430) and pre-set the
   saved status register so the task starts with interrupts enabled.
5. `tcb->sp` lives at TCB offset 0 — keep your assembly consistent
   with that, or take the TCB pointer and use an offset symbol.
6. Run the kernel test suite on your port before touching hardware:
   the suite only needs `printf` and `exit` to exist (§7.2).
