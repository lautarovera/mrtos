# Chapter 2 — API Reference

Everything an application may call or define. Declared in
`kernel/mrtos.h`. "CS" below means the call enters the global critical
section internally; callers never wrap kernel calls in their own
`port_irq_save/restore`.

## 2.1 Configuration (`mrtos_config.h`)

The application provides `mrtos_config.h` on the include path (the
CMake `MRTOS_CONFIG_DIR` mechanism, §6.2). All macros have defaults;
override only what you need.

| Macro | Default | Meaning |
|---|---|---|
| `MRTOS_CFG_TICK_HZ` | `1000` | Kernel time base. All timeouts/sleeps are in ticks of this rate. The port must configure its timer to match (§4.2). |
| `MRTOS_CFG_SLICE_TICKS` | `10` | Round-robin quantum among equal-priority tasks. `1` = rotate every tick; large values approximate run-to-completion within a priority. |
| `MRTOS_CFG_IDLE_STACK_WORDS` | `64` | Idle task stack, in `port_stack_t` units. Must also absorb ISR frames on the MSP430 (§4.6). |
| `MRTOS_CFG_STACK_CHECK` | `1` | Guard-word stack tripwire (§1.13). Disable only where the port owns the stack layout (POSIX port). |
| `MRTOS_CFG_STACK_USAGE` | `1` | Paint stacks at creation and enable `mrtos_stack_unused()` high-water marking (§2.6). Off: no paint cost, the function returns 0. |
| `PORT_CFG_SMCLK_HZ` | `8000000` | (MSP430 port) SMCLK frequency the board actually configures. The port derives the tick reload from it; range and exact divisibility by `MRTOS_CFG_TICK_HZ` are enforced at compile time — agreement with the real clock is the application's responsibility. |

Fixed constants:

| Constant | Value | Notes |
|---|---|---|
| `MRTOS_PRIO_LEVELS` | 8 | Priorities 0…7. 0 is reserved-by-convention for idle; user tasks should use 1…7. |
| `MRTOS_PRIO_MAX` | 7 | Highest. `mrtos_task_create()` clamps anything above to this. |
| `MRTOS_FOREVER` | `0xFFFF` | Timeout sentinel: block indefinitely. Consequence: the maximum *finite* timeout is 65534 ticks (§2.8). |
| `MRTOS_MS(ms)` | — | Millisecond → tick conversion at `MRTOS_CFG_TICK_HZ`. Integer math: at 1 kHz, exact; at lower tick rates values truncate (e.g. `MRTOS_MS(1)` at 100 Hz is 0 — and a 0 timeout means *poll*, see §2.8). |

Return codes (int):

| Code | Value | Returned by |
|---|---|---|
| `MRTOS_OK` | 0 | success |
| `MRTOS_ERR_TIMEOUT` | −1 | timed out, or poll (`timeout=0`) found the object unavailable |
| `MRTOS_ERR_OWNER` | −2 | mutex misuse: recursive lock, or unlock by non-owner |

## 2.2 Kernel lifecycle

```c
void mrtos_init(void);
```
Resets all scheduler state. Call exactly once, before any other kernel
call. (Calling it again while tasks exist abandons them — only the
host test ports could even survive that.)

```c
void mrtos_task_create(mrtos_tcb_t *tcb, const char *name,
                       void (*entry)(void *), void *arg, uint8_t prio,
                       port_stack_t *stack, size_t stack_words);
```
CS. Initializes `tcb`, fabricates an initial stack frame via the port,
and makes the task READY. Usable both before `mrtos_start()` and at
runtime from another task. If created at runtime with a priority above
the caller's, **the new task preempts immediately** (before
`mrtos_task_create` returns to the caller's line of code).

- `prio` is clamped to `MRTOS_PRIO_MAX`.
- `stack` / `stack_words`: application-owned array of `port_stack_t`.
  The whole array belongs to the kernel until the task retires.
- `name` is stored by pointer (not copied) — use string literals.
- **Reuse rule:** see §1.12. The TCB and stack may be reused only
  after the previous task's `state == MRTOS_TASK_SUSPENDED`.
- A task function that returns retires permanently; there is no exit
  status.

```c
void mrtos_start(void);
```
Creates the idle task, picks the first task, transfers control via
`port_start()`. **Does not return.** Anything after the call is dead
code (the demo app relies on this).

```c
void mrtos_yield(void);
```
CS. Rotate the caller's priority level and reschedule. A no-op in
effect when the caller is alone at the highest ready priority.

```c
void mrtos_sleep(uint16_t ticks);
```
CS. Block for `ticks` (1…65535). `mrtos_sleep(0)` degrades to
`mrtos_yield()`. Actual delay is `ticks` to `ticks+1` periods
depending on the phase of the tick when called.

```c
uint32_t mrtos_now(void);
```
CS (a consistent 32-bit read on a 16-bit CPU requires it). Ticks since
`mrtos_start()`. Wraps after ~49.7 days at 1 kHz; compute intervals
with unsigned subtraction (`now - then`), which stays correct across a
single wrap.

```c
extern mrtos_tcb_t * volatile mrtos_cur;
```
The running task. Read-only outside kernel/port code. Useful for
diagnostics (`mrtos_cur->name`).

## 2.3 Semaphore

```c
void mrtos_sem_init(mrtos_sem_t *s, int16_t initial, int16_t limit);
int  mrtos_sem_take(mrtos_sem_t *s, uint16_t timeout);
int  mrtos_sem_give(mrtos_sem_t *s);
```

Counting semaphore, count capped at `limit` (§1.9: gives beyond the
cap are absorbed, gives with waiters hand the token over directly and
skip the count). `take` → `MRTOS_OK` or `MRTOS_ERR_TIMEOUT`. `give`
always returns `MRTOS_OK`.

Context rules: `give` is **ISR-safe**. `take(s, 0)` (poll) is ISR-safe.
`take` with a nonzero timeout is task-only.

`mrtos_sem_init` may re-initialize a semaphore with no waiters; the
demo app uses this to discard queued bounces after a debounce window.
Re-initializing with waiters present strands them — don't.

## 2.4 Mutex

```c
void mrtos_mutex_init(mrtos_mutex_t *m);
int  mrtos_mutex_lock(mrtos_mutex_t *m, uint16_t timeout);
int  mrtos_mutex_unlock(mrtos_mutex_t *m);
```

Ownership semantics with single-level priority inheritance (§1.10).
**Task context only — never from an ISR** (a mutex has an owner; an
ISR is nobody).

`lock` returns:
- `MRTOS_OK` — owned (immediately, or after a hand-off);
- `MRTOS_ERR_TIMEOUT` — `timeout==0` and the mutex was owned, or the
  wait expired (owner unchanged, caller removed from the wait list);
- `MRTOS_ERR_OWNER` — caller already owns it (no recursion).

`unlock` returns `MRTOS_OK`, or `MRTOS_ERR_OWNER` if the caller is not
the owner (state untouched). Unlock demotes the caller to `base_prio`
*before* the hand-off, and ends with a yield, so a higher-priority
new owner runs immediately.

## 2.5 Queue

```c
void mrtos_queue_init(mrtos_queue_t *q, void *storage,
                      uint16_t item_size, uint16_t length);
int  mrtos_queue_send(mrtos_queue_t *q, const void *item, uint16_t timeout);
int  mrtos_queue_recv(mrtos_queue_t *q, void *item, uint16_t timeout);
```

Ring buffer of `length` items, each exactly `item_size` bytes, in
application-supplied `storage` (`item_size × length` bytes, aligned
for the item type). Copy semantics both ways.

- `send` blocks while full; `recv` blocks while empty (Mesa re-check,
  §1.11 — note the timeout-restart caveat).
- `timeout == 0` makes either call a poll → ISR-safe. This is the
  intended ISR pattern: `mrtos_queue_send(&q, &ev, 0)` from an
  interrupt, dropping the event if full (check the return code).
- FIFO order is preserved per queue, including across ring wraparound.
- Senders/receivers blocked on a full/empty queue wake in priority
  order, not send order.

## 2.6 Diagnostics

```c
size_t mrtos_stack_unused(const mrtos_tcb_t *t);
```
High-water mark: how many words of `t`'s stack were never written
since creation (stacks are painted with `MRTOS_STACK_PAINT` at
`mrtos_task_create()`). Use it to size stacks from evidence: run the
worst-case workload, read the marks, keep a margin. O(stack size)
linear scan — call from idle/diagnostic code, not hot paths.

Two caveats. A task that legitimately stores the paint value at its
deepest extent under-reports by those words (inherent to the
technique). And the mark *includes* the context-save area the port
pushes on preemption/retirement (24–26 words on the MSP430 ports) —
that floor is real usage and must stay inside your budget; it
dominates the mark of otherwise-shallow tasks.

```c
void mrtos_stack_overflow_hook(mrtos_tcb_t *t);   /* weak */
```
Called from the tick handler, **inside the critical section**, when a
task's stack guards are corrupt. Default: infinite trap. An override
must not block and should treat the system as compromised (the
overflow already smashed memory below the stack): log minimal state
and reset.

## 2.7 Power-mode locks

```c
void    mrtos_pm_lock(uint8_t max_lpm);
void    mrtos_pm_unlock(uint8_t max_lpm);
uint8_t mrtos_pm_max_lpm(void);
```
A driver that needs clocks running while it works (e.g. LEA needs
SMCLK) caps how deep idle may sleep: `mrtos_pm_lock(MRTOS_LPM0)` while
busy, `mrtos_pm_unlock(MRTOS_LPM0)` after. Counter-based, so nesting
and multiple independent holders compose; the cap is the most
restrictive (shallowest) level any holder requested. `mrtos_pm_max_lpm()`
returns it — the tickless `port_idle()` sleeps no deeper than that, or
at `MRTOS_CFG_LPM_DEFAULT` (LPM3) when nothing is held. Levels are
`MRTOS_LPM0`..`MRTOS_LPM4` (higher = deeper); the port maps them to
hardware. All three are ISR-safe; unbalanced and out-of-range calls are
ignored.

## 2.8 Port-facing entry points

`mrtos_tick()` and `mrtos_sched_pick()` are exported for ports only
(chapter 3). Application code never calls them.

A **tickless** port additionally uses two entry points to suppress the
periodic tick while idle: `mrtos_next_deadline()` returns the ticks
until the earliest pending wake (0 if no task has a timeout), so
`port_idle()` can program a wake timer that far ahead and sleep;
`mrtos_tick_advance(n)` folds the `n` ticks slept through back into the
time base in one O(woken) step (waking everything due, adjusting
`tick_count`). Both are called with interrupts disabled, like
`mrtos_tick()`. Active-phase behaviour is unchanged — these run only
while the idle task would otherwise spin on the tick.

## 2.9 Timeout semantics — exact rules

All timeouts are `uint16_t` ticks:

| Value | Meaning |
|---|---|
| `0` | Poll: never block, return `MRTOS_ERR_TIMEOUT` if unavailable. The only blocking-API form that is ISR-safe. |
| `1 … 65534` | Block at most this many ticks. Real bound is *t…t+1* tick periods (tick phase), as verified by the suite's "blocked ~N ticks" checks. |
| `0xFFFF` (`MRTOS_FOREVER`) | Block indefinitely. |

Resolution rule for a wake racing a same-tick timeout: the timeout is
processed first by the tick handler; the give/send then lands as if
nobody had been waiting (e.g. a semaphore give goes into the count).
Exactly one outcome occurs — verified by `test_integration` scenario
2b (§7.4.2).
