# Chapter 1 — Architecture

## 1.1 Design goals

mRTOS targets a 16-bit MCU with FRAM and a few KB of SRAM. Every
structural decision follows from four goals:

1. **O(1) hot paths.** Scheduling, blocking and waking must not loop
   over the task population. On a 16 MHz CPU running a 1 kHz tick,
   linear scans would dominate the budget.
2. **Static allocation only.** The application owns every TCB, stack
   and queue buffer. There is no heap, so memory behavior is fully
   deterministic and visible in the linker map — important on FRAM
   parts where you deliberately place data.
3. **One locking rule.** A single global critical section protects
   every kernel structure. Simple to reason about, and it makes the
   normal API ISR-safe for free (§1.7).
4. **Portability as a falsifiable property.** The core contains zero
   CPU-specific code; the port contract (chapter 3) is small enough
   that three independent implementations exist, and the test suite
   runs on two of them.

## 1.2 Kernel objects

```
mrtos_tcb_t      task control block (app-allocated)
mrtos_sem_t      counting semaphore
mrtos_mutex_t    mutex with single-level priority inheritance
mrtos_queue_t    fixed-item-size message queue, copy semantics
```

All four are plain structs the application instantiates (statically,
typically). `mrtos_*_init()` functions only fill fields — they never
allocate, never fail, and may be called again to re-initialize a
quiesced object.

## 1.3 The task control block

```c
typedef struct mrtos_tcb {
    port_stack_t      *sp;          /* saved stack pointer — MUST be +0  */
    struct mrtos_tcb  *next, *prev; /* ready OR wait list (circular)     */
    struct mrtos_tcb **wait_head;   /* which wait list we sit in, if any */
    struct mrtos_tcb  *dnext;       /* delta delay list                  */
    uint16_t           delta;       /* ticks relative to predecessor     */
    uint8_t            in_delay;
    uint8_t            prio;        /* effective (may be inherited)      */
    uint8_t            base_prio;   /* assigned at creation              */
    uint8_t            state;       /* READY / BLOCKED / SUSPENDED       */
    int8_t             wake_res;    /* result the unblocked task sees    */
    void             (*entry)(void *);
    void              *arg;
    port_stack_t      *stack_base;
    size_t             stack_words;
    const char        *name;
    /* PORT_TCB_EXT: optional port-private fields (e.g. ucontext_t)     */
} mrtos_tcb_t;
```

Two invariants are load-bearing:

- **`sp` must be the first member.** The MSP430 context-switch assembly
  stores and loads SP at offset 0 of the TCB without involving the
  compiler. Reordering this struct breaks the target port silently.
- **`next`/`prev` are shared between the ready list and wait lists.**
  A task is in *exactly one* of {a ready list, a wait list} at any
  moment, so one pair of links suffices. The delay list uses its own
  link (`dnext`) because a task can simultaneously be in a wait list
  *and* the delay list (a timed block).

`wake_res` is the channel through which a waker communicates the
outcome to a blocked task: `block_current()` pre-loads it with
`MRTOS_ERR_TIMEOUT`, and a genuine wake overwrites it with `MRTOS_OK`.
When the blocked task resumes, it simply returns `self->wake_res` —
no flags, no re-derivation of why it woke.

## 1.4 Scheduler

### 1.4.1 Ready structure

One **circular doubly-linked list per priority level** (8 levels,
0 = idle … 7 = highest) plus an **8-bit ready bitmap** with bit *p* set
iff `ready_head[p]` is non-empty.

Picking the next task is a branch-reduced highest-set-bit scan of one
byte:

```c
uint8_t map = ready_map;            /* never 0: idle is always ready */
uint8_t p = 0;
if (map & 0xF0) { p += 4; map >>= 4; }
if (map & 0x0C) { p += 2; map >>= 2; }
if (map & 0x02) { p += 1; }
mrtos_cur = ready_head[p];
```

Three conditional adds — no loops, no tables, no CLZ instruction
needed (the MSP430 has none). This is `mrtos_sched_pick()`, the only
scheduling decision in the kernel; everything else just edits lists
and calls it.

### 1.4.2 The running task stays in its ready list

Many kernels dequeue the running task. mRTOS does not:
`mrtos_cur == ready_head[p]` while running. The consequences:

- **Time-slicing is one pointer rotation.** Every
  `MRTOS_CFG_SLICE_TICKS` ticks, the tick handler does
  `ready_head[p] = ready_head[p]->next` and re-picks. Equal-priority
  tasks round-robin with zero list surgery.
- **`mrtos_yield()` is the same rotation,** done voluntarily.
- **Preemption is implicit.** When a higher-priority task becomes
  ready, `mrtos_sched_pick()` simply lands on a higher bitmap bit; the
  preempted task is still at (or near) the head of its own level and
  resumes untouched when that level is reached again.

### 1.4.3 Idle

`mrtos_start()` creates an internal idle task at priority 0 before
starting the scheduler, which is why `ready_map` is never zero and the
pick needs no empty-system special case. Idle runs `port_idle()` in a
loop — on the MSP430 that's LPM0 sleep, on the host ports it is where
simulated ticks advance (chapter 7 discusses the testing consequences).

## 1.5 Wait lists

Each blocking object (semaphore, mutex, queue tx, queue rx) owns one
wait list: circular, doubly-linked through the same `next`/`prev`
fields, **sorted by descending priority, FIFO among equals**. The head
is always the next task to wake, so `wake_first()` is O(1); the cost
of priority ordering is paid once at insertion (`wait_insert()` walks
the list — O(waiters), acceptable because wait lists are short in
practice).

A task records which list it sits in via `wait_head`, so the *tick
handler* can remove it from the right object when a timed block
expires, without knowing which object it was.

## 1.6 Time: the delta delay list

Sleeps and timeouts share one **delta-sorted singly-linked list**:
each node's `delta` is relative to its predecessor, so the head's
`delta` is the only number the tick handler ever decrements.

```
delay_head -> [A Δ3] -> [B Δ0] -> [C Δ4]
   means: A wakes in 3 ticks, B at the same tick as A, C 4 ticks later.
```

Tick cost is therefore **independent of how many tasks are sleeping**.
When the head's delta hits 0, the handler pops *every* zero-delta node
(simultaneous wakes), and for each one:

1. clears its delay-list membership,
2. removes it from its wait list if it was in one (`wait_head`),
3. readies it — leaving `wake_res` at the pre-loaded
   `MRTOS_ERR_TIMEOUT`, which is precisely how a timeout reports
   itself.

`delay_insert()` walks the list to find the insertion point
(O(sleepers)), subtracting deltas as it goes, and compensates the
successor's delta. `delay_remove()` (early wake) folds the removed
node's delta into its successor. Both run inside the critical section.

A subtle ordering property falls out of this structure and matters for
race behavior (§7.4.2): when a timeout and a wake collide on the same
tick, **the tick handler readies the timed-out task first**, so the
timed-out task — if higher priority — runs and observes its timeout
before the would-be waker runs. Exactly one of the two outcomes
happens; the token is never lost or duplicated.

## 1.7 Locking model and ISR safety

The entire kernel is protected by **one global critical section**:
`port_irq_save()` / `port_irq_restore()`, which on the MSP430 maps to
the GIE bit. There are no per-object locks, no lock ordering, no
nesting subtleties — every kernel entry point does save → mutate →
restore.

Because ISRs on the MSP430 run with the same exclusion (an ISR body
executes with interrupts masked unless it re-enables them), **the
normal API is ISR-safe for any call that cannot block**:

- `mrtos_sem_give()`
- `mrtos_queue_send(q, item, 0)` and `mrtos_queue_recv(q, item, 0)`

There is no separate `*_from_isr()` API surface. The mechanism that
makes this work is the **deferred yield**: when a kernel call made
from an ISR wakes a higher-priority task, `port_yield()` does not
switch immediately — it pends a software interrupt that fires after
the outer `RETI` (§4.3). From a task context the same call switches
(almost) immediately. Callers never need to know which context they
are in.

Blocking calls (`take/lock/send/recv` with timeout ≠ 0) and
`mrtos_mutex_*` in general are **task-context only**.

## 1.8 Blocking and waking: the two primitives

All four synchronization objects are built from two internal
operations, both called inside the critical section:

```c
block_current(wl, timeout)   /* dequeue self, join wait list wl (may be
                                NULL for pure sleep), join delay list if
                                timeout != FOREVER, wake_res = TIMEOUT,
                                then port_yield() */
wake_first(wl, res)          /* pop wait-list head, leave delay list,
                                wake_res = res, ready it */
```

plus `wake_preempt_check(t)`: after waking `t`, yield if `t`
outranks the caller. This is why a `give`/`send`/`unlock` from a task
immediately hands the CPU to a higher-priority waiter, while the same
call from an ISR defers the switch to after `RETI`.

## 1.9 Semaphore semantics

Counting semaphore with a cap:

- `take`: decrement if positive; with `timeout == 0` poll-fail;
  otherwise block on the wait list.
- `give`: **if anyone is waiting, the token goes directly to the
  highest-priority waiter** — the count is *not* incremented and then
  re-decremented. Otherwise the count increments, saturating at
  `limit` (extra gives are absorbed silently, by design — a binary
  semaphore with `limit = 1` cannot "store up" button presses).

Direct hand-off avoids a classic race (a third task stealing the token
between give and the waiter's wake) and makes the wake order exactly
the wait-list order: priority, then FIFO.

## 1.10 Mutex semantics: single-level priority inheritance

- **Not recursive.** A second `lock` by the owner returns
  `MRTOS_ERR_OWNER` immediately (deadlock would be the alternative).
- **Owner check on unlock.** Unlock by a non-owner returns
  `MRTOS_ERR_OWNER` and changes nothing.
- **Inheritance (the priority-inversion countermeasure):** when a task
  blocks on an owned mutex, the owner's *effective* priority is raised
  to the blocker's (if higher). The raise handles both owner states:
  - owner READY → re-queued into the higher ready list;
  - owner BLOCKED on something else → its wait list is re-sorted so it
    also wakes ahead of lower-priority peers there.
- **Demotion at unlock, unconditionally.** `unlock` restores
  `base_prio` before waking anyone. This also self-heals the one
  documented gap: if the high-priority waiter *times out* before
  unlock, the owner keeps the inherited priority until its next unlock
  (no de-inheritance on waiter timeout — see §5.2).
- **Direct hand-off:** unlock transfers ownership to the
  highest-priority waiter rather than releasing and letting waiters
  race. Combined with the post-demotion `port_yield()`, the waiter
  with the highest priority runs next if it outranks everyone ready.

What single-level means: the inherited priority is not propagated
transitively through chains (A blocks on B's mutex while B blocks on
C's — C is not boosted on A's behalf). §5.2 explains how to live with
that.

## 1.11 Queue semantics: copy, Mesa-style

Fixed-size items copied in and out of an application-supplied ring
buffer (`item_size × length` bytes). Copy semantics mean the sender's
buffer is free for reuse the instant `send` returns, and an ISR can
send a stack variable safely.

Wakeups are **Mesa-style**: a woken waiter re-checks the condition in
a loop rather than assuming it holds. The scenario that requires this:
a receiver is woken because an item arrived, but before it runs, a
higher-priority task polls the queue and steals the item. The woken
receiver finds the queue empty and simply blocks again inside the same
`recv` call. Consequence (documented, §5.3): **the timeout restarts on
each retry** — a `recv(q, &v, 100)` that loses two such races can wait
up to ~300 ticks. If you need a hard deadline, check `mrtos_now()`
around the call.

The alternative (Hoare-style hand-off, copying directly into the
waiter's buffer at send time) was rejected: it needs a per-waiter
destination pointer in the TCB and makes the ISR path longer; Mesa
re-check is two extra branches and is what every mainstream RTOS does.

## 1.12 Task lifecycle

```
mrtos_task_create() ── READY ──┬── running (scheduler's choice)
                               ├── BLOCKED (object wait / sleep)
                               └── entry() returns → SUSPENDED, forever
```

A task function that returns is **retired**: an internal shell
(`task_shell`) removes it from the ready structure and parks it in
`MRTOS_TASK_SUSPENDED`, never to be scheduled again. There is no
deletion, no join, no restart API (§5.4).

**TCB/stack reuse hazard:** `mrtos_task_create()` `memset`s the TCB.
Creating a task in a TCB whose previous occupant has not yet retired
corrupts the ready list — the kernel does not defend against this (a
check would cost code in the hot path to catch a pure programming
error). Re-use is safe only after the previous task's state reads
`MRTOS_TASK_SUSPENDED`. The test suite tripped exactly this once
(§7.3.3), which is why it is called out here.

## 1.13 Stack overflow detection

When `MRTOS_CFG_STACK_CHECK` is on, `mrtos_task_create()` writes two
guard words (`0x5AFE`) at the *base* (lowest addresses) of each stack,
and the tick handler verifies the current task's guards every tick.
On corruption it calls `mrtos_stack_overflow_hook(tcb)` — weak default:
trap in an infinite loop with interrupts off, so a debugger shows the
culprit via `tcb->name`. Override the hook to log/reset in production.

This catches overflow *after the fact* and only on the running task at
tick time; it is a tripwire, not an MPU. Size stacks generously
(§4.6 quantifies the MSP430 per-task ISR overhead that must fit).
