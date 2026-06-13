# Chapter 5 — Limitations

Every entry here is a *decision*, not an accident. For each: what is
missing, why, what it costs you, and how to work within it.

## 5.1 Small memory model only

**What:** code and data pointers are 16 bits; everything lives in the
lower 64 KB. The FR5994's upper FRAM bank (the part has 256 KB total)
is unused.

**Why:** the large model changes pointer width (20-bit, `.A`
instructions, `CALLA`/`RETA`), which touches the context frame, every
`mov.w` of a pointer in the port assembly, and function-pointer
storage in the TCB. Supporting both would double the assembly surface
to verify.

**Cost / workaround:** 2.5 KB of kernel text in a 48 KB lower-FRAM
window leaves ample room for most applications. Large-model support is
mechanical (the README names the instruction substitutions) but must
be done deliberately, with the context layout re-verified.

## 5.2 Mutex: single-level PI, no recursion, no timeout de-inheritance

**What:**
- Priority inheritance boosts only the *direct* owner; chains
  (A waits on B, B waits on C) do not propagate A's priority to C.
- Recursive locking returns `MRTOS_ERR_OWNER` instead of counting.
- If the boosting waiter times out, the owner keeps the inherited
  priority until its next `unlock` (which always demotes).

**Why:** transitive PI requires walking an owner→mutex→owner chain at
block time, recursion needs a counter plus semantics for PI under
partial unlock, timeout de-inheritance needs back-references from
waiter to boosted owner. Each is real code and real state on every
mutex operation, paid by all users, to support patterns a small system
should avoid anyway.

**Cost / workaround:** keep lock nesting shallow and never hold a
mutex across a blocking call (the PI-on-BLOCKED-owner path exists and
is tested, but chains beyond depth 1 lose the inversion bound). The
self-healing demotion at unlock bounds the timeout gap: a stale boost
survives at most until the owner leaves its critical section.

## 5.3 Queue timeout restarts on Mesa re-check

**What:** a queue waiter that is woken but loses the item to a faster
task re-blocks with its **full original timeout**, not the remainder
(§1.11).

**Why:** remaining-time bookkeeping costs a timestamp per blocked task
and a subtraction in a hot path, to tighten a bound that only moves
under sustained adverse contention.

**Cost / workaround:** treat queue timeouts as *per-attempt* bounds,
not deadlines. For a hard deadline, loop with `mrtos_now()`:

```c
uint32_t t0 = mrtos_now();
while (mrtos_queue_recv(&q, &v, deadline - (mrtos_now() - t0)) == MRTOS_OK)
    ...
```

## 5.4 No task deletion, no join, no restart

**What:** a task function that returns is retired to
`MRTOS_TASK_SUSPENDED` forever. There is no `mrtos_task_delete()`.

**Why:** deletion is the most bug-prone API in small RTOSes: the
victim may own a mutex, sit in a wait list and the delay list, or be
mid-handoff. Doing it safely requires resource tracking the kernel
otherwise doesn't need. Static systems start their tasks once.

**Cost / workaround:** structure workers as servers blocking on a
queue/semaphore rather than as run-to-completion jobs. TCB/stack reuse
*is* permitted once the previous occupant reads SUSPENDED (§1.12) —
that is the supported "restart" idiom.

## 5.5 Hardware constraints inherited from the FR59xx family

These are silicon facts the port obeys; they bind applications too:

- **FRAM wait states:** 0 wait states only up to 8 MHz. Run faster and
  you must program `NWAITS_1` *first* (§4.7). The demo pins the system
  at 8 MHz to stay in the simple regime.
- **LOCKLPM5:** GPIO is dead after reset until unlocked. Any
  application replacing `board_init()` must keep this line.
- **Application ISRs run on the interrupted task's stack** (§4.6):
  every task pays the worst ISR frame. Keep ISRs to flag-clear +
  `give`/`send(…, 0)`.
- **Single global interrupt lock = GIE:** every kernel critical
  section masks *all* maskable interrupts for its (short) duration.
  Interrupt latency is bounded by the longest kernel critical section
  — list operations, so tens of cycles — but it is nonzero;
  cycle-exact sampling should use timer-capture hardware, not ISR
  timestamps.
- **8 priority levels / 16-bit timeouts / 32-bit tick counter:**
  consequences spelled out in §2.1 and §2.9.

## 5.6 No tickless idle (yet — kernel half landed)

**What:** the shipped `port_idle()` still idles in LPM0 with the tick
firing 1000×/s. The *kernel* support for tickless is in place
(`mrtos_next_deadline`, `mrtos_tick_advance`, power locks — §2.7/§2.8,
tested in `test_unit_tickless`); a tickless `port_idle()` is not yet
written.

**Why the rest is deferred:** the remaining work is the port half —
reprogram the wake timer on ACLK, enter LPM3, reconcile elapsed time
on wake, and prove the LPM-entry interrupt race on silicon. Tracked in
`doc/POWER.md` §2.1. The structure was always ready: the delta list's
head *is* the next deadline, and the idle-resume path needs no special
casing (§4.5).

**Cost today:** idle floor is LPM0 + 1 kHz wakeups — measured at
**277 µA** on the FR5994 LaunchPad (T8). For battery products,
Timer_A-on-ACLK + LPM3 tickless is the single highest-value extension,
targeting single-digit µA.

## 5.7 Cooperative test ports — verification gap

The POSIX and simulator ports advance ticks only in `port_idle()`, so
asynchronous preemption never occurs off-target. This is a *test
coverage* limitation, not a kernel one; §7.7 maps exactly which
behaviors remain target-only and which checklist items cover them.

## 5.8 Things the kernel assumes about you

Stated once, here, rather than defensively checked in code:

- You do not call blocking APIs from ISRs (only `give` / `send(…,0)` /
  `recv(…,0)` / `take(…,0)`).
- You do not reuse a TCB before its task retires (§1.12).
- You do not re-init objects that have waiters.
- You size stacks for task + 52 B context + your worst ISR (§4.6).
- Task names point at storage that outlives the task (string
  literals).
