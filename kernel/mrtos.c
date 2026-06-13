/*
 * mRTOS kernel - portable core. No CPU-specific code in this file.
 *
 * Locking model: every kernel structure is protected by a single global
 * critical section (port_irq_save/restore). On MSP430 this maps to GIE;
 * ISRs therefore run with the same exclusion guarantees as tasks, which
 * is what makes mrtos_sem_give()/mrtos_queue_send(timeout=0) ISR-safe.
 *
 * Scheduling: fixed-priority preemptive, 8 levels, round-robin among
 * equal priorities every MRTOS_CFG_SLICE_TICKS. The running task stays
 * in its ready list; ready_head[prio] is rotated to time-slice.
 */
#include <string.h>
#include "mrtos.h"

#define STACK_MAGIC 0x5AFEu

mrtos_tcb_t * volatile mrtos_cur;

static mrtos_tcb_t *ready_head[MRTOS_PRIO_LEVELS];
static uint8_t      ready_map;
static mrtos_tcb_t *delay_head;
static volatile uint32_t tick_count;
static uint16_t     slice_left = MRTOS_CFG_SLICE_TICKS;

static mrtos_tcb_t  idle_tcb;
static port_stack_t idle_stack[MRTOS_CFG_IDLE_STACK_WORDS];

/* ------------------------------------------------------------------ */
/* Ready lists: one circular doubly-linked list per priority + bitmap.  */
/* ------------------------------------------------------------------ */
static void ready_insert(mrtos_tcb_t *t)
{
    mrtos_tcb_t **h = &ready_head[t->prio];
    if (*h == NULL) {
        t->next = t;
        t->prev = t;
        *h = t;
        ready_map |= (uint8_t)(1u << t->prio);
    } else {                                  /* insert at tail (= before head) */
        t->next = *h;
        t->prev = (*h)->prev;
        (*h)->prev->next = t;
        (*h)->prev = t;
    }
}

static void ready_remove(mrtos_tcb_t *t)
{
    mrtos_tcb_t **h = &ready_head[t->prio];
    if (t->next == t) {
        *h = NULL;
        ready_map &= (uint8_t)~(1u << t->prio);
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (*h == t)
            *h = t->next;
    }
    t->next = t->prev = NULL;
}

void mrtos_sched_pick(void)
{
    uint8_t map = ready_map;                  /* idle is always ready: map != 0 */
    uint8_t p = 0;
    if (map & 0xF0u) { p += 4; map >>= 4; }
    if (map & 0x0Cu) { p += 2; map >>= 2; }
    if (map & 0x02u) { p += 1; }
    mrtos_cur = ready_head[p];
}

/* ------------------------------------------------------------------ */
/* Wait lists: circular doubly-linked, sorted by descending priority,   */
/* FIFO among equals. Head = next task to wake.                         */
/* ------------------------------------------------------------------ */
static void wait_insert(mrtos_tcb_t **wl, mrtos_tcb_t *t)
{
    t->wait_head = wl;
    if (*wl == NULL) {
        t->next = t;
        t->prev = t;
        *wl = t;
        return;
    }
    mrtos_tcb_t *it = *wl;
    do {
        if (it->prio < t->prio)
            break;
        it = it->next;
    } while (it != *wl);
    /* insert before 'it' (covers both break and full-circle cases) */
    t->next = it;
    t->prev = it->prev;
    it->prev->next = t;
    it->prev = t;
    if (it == *wl && t->prio > it->prio)
        *wl = t;
}

static void wait_remove(mrtos_tcb_t *t)
{
    mrtos_tcb_t **wl = t->wait_head;
    if (t->next == t) {
        *wl = NULL;
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (*wl == t)
            *wl = t->next;
    }
    t->next = t->prev = NULL;
    t->wait_head = NULL;
}

/* ------------------------------------------------------------------ */
/* Delay list: singly-linked delta list keyed on remaining ticks.       */
/* ------------------------------------------------------------------ */
static void delay_insert(mrtos_tcb_t *t, uint16_t ticks)
{
    mrtos_tcb_t **pp = &delay_head;
    while (*pp != NULL && (*pp)->delta <= ticks) {
        ticks = (uint16_t)(ticks - (*pp)->delta);
        pp = &(*pp)->dnext;
    }
    t->delta = ticks;
    t->dnext = *pp;
    if (*pp != NULL)
        (*pp)->delta = (uint16_t)((*pp)->delta - ticks);
    *pp = t;
    t->in_delay = 1;
}

static void delay_remove(mrtos_tcb_t *t)
{
    mrtos_tcb_t **pp = &delay_head;
    while (*pp != NULL && *pp != t)
        pp = &(*pp)->dnext;
    if (*pp == t) {
        *pp = t->dnext;
        if (t->dnext != NULL)
            t->dnext->delta = (uint16_t)(t->dnext->delta + t->delta);
    }
    t->in_delay = 0;
    t->dnext = NULL;
}

/* ------------------------------------------------------------------ */
/* Block / wake primitives (called inside a critical section).          */
/* ------------------------------------------------------------------ */
static void block_current(mrtos_tcb_t **wl, uint16_t timeout)
{
    mrtos_tcb_t *t = mrtos_cur;
    ready_remove(t);
    t->state = MRTOS_TASK_BLOCKED;
    t->wake_res = MRTOS_ERR_TIMEOUT;          /* overwritten on real wake */
    if (wl != NULL)
        wait_insert(wl, t);
    if (timeout != MRTOS_FOREVER)
        delay_insert(t, timeout);
    port_yield();                             /* deferred on MCU ports    */
}

static mrtos_tcb_t *wake_first(mrtos_tcb_t **wl, int8_t res)
{
    mrtos_tcb_t *t = *wl;
    wait_remove(t);
    if (t->in_delay)
        delay_remove(t);
    t->wake_res = res;
    t->state = MRTOS_TASK_READY;
    ready_insert(t);
    return t;
}

static void wake_preempt_check(mrtos_tcb_t *woken)
{
    if (woken->prio > mrtos_cur->prio)
        port_yield();
}

/* ------------------------------------------------------------------ */
/* Tick                                                                 */
/* ------------------------------------------------------------------ */

/* Pop the delay-list head (its time has come) and make it ready.       */
/* Shared by mrtos_tick() and mrtos_tick_advance().                     */
static void wake_delay_head(void)
{
    mrtos_tcb_t *t = delay_head;
    delay_head = t->dnext;
    t->in_delay = 0;
    t->dnext = NULL;
    if (t->wait_head != NULL)
        wait_remove(t);                   /* timed out on an object   */
    t->state = MRTOS_TASK_READY;          /* wake_res keeps TIMEOUT   */
    ready_insert(t);
}

__attribute__((used)) void mrtos_tick(void)
{
    tick_count++;

#if MRTOS_CFG_STACK_CHECK
    {
        mrtos_tcb_t *c = mrtos_cur;
        if (c->stack_base != NULL &&
            (c->stack_base[0] != (port_stack_t)STACK_MAGIC ||
             c->stack_base[1] != (port_stack_t)STACK_MAGIC)) {
            mrtos_stack_overflow_hook(c);
        }
    }
#endif

    if (delay_head != NULL) {
        if (delay_head->delta > 0)
            delay_head->delta--;
        while (delay_head != NULL && delay_head->delta == 0)
            wake_delay_head();
    }

    /* Round-robin among equal priorities. */
    if (ready_head[mrtos_cur->prio] != NULL && --slice_left == 0) {
        slice_left = MRTOS_CFG_SLICE_TICKS;
        ready_head[mrtos_cur->prio] = ready_head[mrtos_cur->prio]->next;
    }

    mrtos_sched_pick();
}

/* ------------------------------------------------------------------ */
/* Tickless support (consumed by a tickless port's port_idle).          */
/* ------------------------------------------------------------------ */

/* Ticks until the earliest pending wake, or 0 if no task has a timeout */
/* pending (the delay list is empty). The head delta is >= 1 between    */
/* ticks, so 0 is unambiguous.                                          */
uint16_t mrtos_next_deadline(void)
{
    uint16_t key = port_irq_save();
    uint16_t d = (delay_head != NULL) ? delay_head->delta : 0u;
    port_irq_restore(key);
    return d;
}

/* Fold n elapsed ticks into the time base in one step: advance the    */
/* clock, wake everything whose deadline has passed, and reschedule.    */
/* Equivalent to n calls of the delay-list half of mrtos_tick(), but    */
/* O(woken) not O(n). Call with interrupts disabled, like mrtos_tick(). */
/* Does no round-robin or stack check: only idle ran during the sleep.  */
void mrtos_tick_advance(uint16_t n)
{
    tick_count += n;
    while (delay_head != NULL && delay_head->delta <= n) {
        n = (uint16_t)(n - delay_head->delta);
        wake_delay_head();
    }
    if (delay_head != NULL)
        delay_head->delta = (uint16_t)(delay_head->delta - n);
    mrtos_sched_pick();
}

/* ------------------------------------------------------------------ */
/* Power-mode locks: a driver caps how deep idle may sleep while it     */
/* needs clocks (e.g. LEA needs LPM0). port_idle() sleeps at the        */
/* deepest mode no one has vetoed (mrtos_pm_max_lpm()).                 */
/* ------------------------------------------------------------------ */
static uint8_t pm_count[MRTOS_LPM_LEVELS];

void mrtos_pm_lock(uint8_t max_lpm)
{
    uint16_t key = port_irq_save();
    if (max_lpm < MRTOS_LPM_LEVELS)
        pm_count[max_lpm]++;
    port_irq_restore(key);
}

void mrtos_pm_unlock(uint8_t max_lpm)
{
    uint16_t key = port_irq_save();
    if (max_lpm < MRTOS_LPM_LEVELS && pm_count[max_lpm] > 0)
        pm_count[max_lpm]--;
    port_irq_restore(key);
}

uint8_t mrtos_pm_max_lpm(void)
{
    uint16_t key = port_irq_save();
    uint8_t lvl = MRTOS_CFG_LPM_DEFAULT;
    for (uint8_t i = 0; i < MRTOS_CFG_LPM_DEFAULT; i++) {
        if (pm_count[i] > 0) {            /* most restrictive cap wins */
            lvl = i;
            break;
        }
    }
    port_irq_restore(key);
    return lvl;
}

uint32_t mrtos_now(void)
{
    uint16_t key = port_irq_save();
    uint32_t t = tick_count;
    port_irq_restore(key);
    return t;
}

/* ------------------------------------------------------------------ */
/* Tasks                                                                */
/* ------------------------------------------------------------------ */
static void task_shell(void *p)
{
    mrtos_tcb_t *t = (mrtos_tcb_t *)p;
    t->entry(t->arg);
    /* Task returned: retire it permanently. */
    uint16_t key = port_irq_save();
    ready_remove(t);
    t->state = MRTOS_TASK_SUSPENDED;
    port_yield();
    port_irq_restore(key);
    for (;;) { /* never reached */ }
}

void mrtos_task_create(mrtos_tcb_t *tcb, const char *name,
                       void (*entry)(void *), void *arg, uint8_t prio,
                       port_stack_t *stack, size_t stack_words)
{
    if (prio > MRTOS_PRIO_MAX)
        prio = MRTOS_PRIO_MAX;

    memset(tcb, 0, sizeof(*tcb));
    tcb->name        = name;
    tcb->entry       = entry;
    tcb->arg         = arg;
    tcb->prio        = prio;
    tcb->base_prio   = prio;
    tcb->state       = MRTOS_TASK_READY;
    tcb->stack_base  = stack;
    tcb->stack_words = stack_words;
#if MRTOS_CFG_STACK_USAGE
    for (size_t i = 0; i < stack_words; i++)
        stack[i] = (port_stack_t)MRTOS_STACK_PAINT;
#endif
#if MRTOS_CFG_STACK_CHECK
    stack[0] = (port_stack_t)STACK_MAGIC;
    stack[1] = (port_stack_t)STACK_MAGIC;
#endif
    tcb->sp = port_stack_init(stack, stack_words, task_shell, tcb);

    uint16_t key = port_irq_save();
    ready_insert(tcb);
    if (mrtos_cur != NULL)                    /* runtime creation         */
        wake_preempt_check(tcb);
    port_irq_restore(key);
}

static void idle_entry(void *arg)
{
    (void)arg;
    for (;;)
        port_idle();
}

void mrtos_init(void)
{
    mrtos_cur  = NULL;
    ready_map  = 0;
    delay_head = NULL;
    tick_count = 0;
    slice_left = MRTOS_CFG_SLICE_TICKS;
    memset(ready_head, 0, sizeof(ready_head));
}

void mrtos_start(void)
{
    mrtos_task_create(&idle_tcb, "idle", idle_entry, NULL, 0,
                      idle_stack, MRTOS_CFG_IDLE_STACK_WORDS);
    mrtos_sched_pick();
    port_start();                             /* no return                */
}

void mrtos_yield(void)
{
    uint16_t key = port_irq_save();
    ready_head[mrtos_cur->prio] = mrtos_cur->next;
    port_yield();
    port_irq_restore(key);
}

void mrtos_sleep(uint16_t ticks)
{
    if (ticks == 0) {
        mrtos_yield();
        return;
    }
    uint16_t key = port_irq_save();
    block_current(NULL, ticks);
    port_irq_restore(key);
}

size_t mrtos_stack_unused(const mrtos_tcb_t *t)
{
#if MRTOS_CFG_STACK_USAGE
    /* Skip the guard words: they hold STACK_MAGIC, not the paint. */
    size_t start = MRTOS_CFG_STACK_CHECK ? 2u : 0u;
    const port_stack_t *p = t->stack_base + start;
    size_t n = 0;
    while (n < t->stack_words - start &&
           p[n] == (port_stack_t)MRTOS_STACK_PAINT)
        n++;
    return n;
#else
    (void)t;
    return 0;
#endif
}

__attribute__((weak)) void mrtos_stack_overflow_hook(mrtos_tcb_t *t)
{
    (void)t;
    for (;;) { /* trap: identify task via debugger (t->name) */ }
}

/* ------------------------------------------------------------------ */
/* Semaphore                                                            */
/* ------------------------------------------------------------------ */
void mrtos_sem_init(mrtos_sem_t *s, int16_t initial, int16_t limit)
{
    s->count   = initial;
    s->limit   = limit;
    s->waiters = NULL;
}

int mrtos_sem_take(mrtos_sem_t *s, uint16_t timeout)
{
    uint16_t key = port_irq_save();
    if (s->count > 0) {
        s->count--;
        port_irq_restore(key);
        return MRTOS_OK;
    }
    if (timeout == 0) {
        port_irq_restore(key);
        return MRTOS_ERR_TIMEOUT;
    }
    mrtos_tcb_t *self = mrtos_cur;
    block_current(&s->waiters, timeout);
    port_irq_restore(key);                    /* switch happens here (MCU)*/
    return self->wake_res;
}

int mrtos_sem_give(mrtos_sem_t *s)
{
    uint16_t key = port_irq_save();
    if (s->waiters != NULL) {
        wake_preempt_check(wake_first(&s->waiters, MRTOS_OK));
    } else if (s->count < s->limit) {
        s->count++;
    }
    port_irq_restore(key);
    return MRTOS_OK;
}

/* ------------------------------------------------------------------ */
/* Mutex with single-level priority inheritance.                        */
/* Limitations (documented): no recursion, no transitive inheritance,   */
/* inherited priority is dropped only at unlock time.                   */
/* ------------------------------------------------------------------ */
static void prio_raise(mrtos_tcb_t *t, uint8_t prio)
{
    if (t->prio >= prio)
        return;
    if (t->state == MRTOS_TASK_READY) {
        ready_remove(t);
        t->prio = prio;
        ready_insert(t);
    } else {
        t->prio = prio;
        if (t->wait_head != NULL) {           /* keep wait list sorted    */
            mrtos_tcb_t **wl = t->wait_head;
            wait_remove(t);
            wait_insert(wl, t);
        }
    }
}

void mrtos_mutex_init(mrtos_mutex_t *m)
{
    m->owner   = NULL;
    m->waiters = NULL;
}

int mrtos_mutex_lock(mrtos_mutex_t *m, uint16_t timeout)
{
    uint16_t key = port_irq_save();
    mrtos_tcb_t *self = mrtos_cur;
    if (m->owner == NULL) {
        m->owner = self;
        port_irq_restore(key);
        return MRTOS_OK;
    }
    if (m->owner == self) {
        port_irq_restore(key);
        return MRTOS_ERR_OWNER;               /* not recursive            */
    }
    if (timeout == 0) {
        port_irq_restore(key);
        return MRTOS_ERR_TIMEOUT;
    }
    prio_raise(m->owner, self->prio);
    block_current(&m->waiters, timeout);
    port_irq_restore(key);
    return self->wake_res;
}

int mrtos_mutex_unlock(mrtos_mutex_t *m)
{
    uint16_t key = port_irq_save();
    mrtos_tcb_t *self = mrtos_cur;
    if (m->owner != self) {
        port_irq_restore(key);
        return MRTOS_ERR_OWNER;
    }
    if (self->prio != self->base_prio) {      /* drop inherited priority  */
        ready_remove(self);
        self->prio = self->base_prio;
        ready_insert(self);
    }
    if (m->waiters != NULL) {
        mrtos_tcb_t *t = wake_first(&m->waiters, MRTOS_OK);
        m->owner = t;                         /* direct hand-off          */
        wake_preempt_check(t);
    } else {
        m->owner = NULL;
    }
    port_yield();                             /* re-evaluate after demotion */
    port_irq_restore(key);
    return MRTOS_OK;
}

/* ------------------------------------------------------------------ */
/* Queue (copy semantics, Mesa-style: woken waiters re-check the        */
/* condition, so a slot can be stolen and the waiter loops again).      */
/* ------------------------------------------------------------------ */
void mrtos_queue_init(mrtos_queue_t *q, void *storage,
                      uint16_t item_size, uint16_t length)
{
    q->buf        = (uint8_t *)storage;
    q->item_size  = item_size;
    q->length     = length;
    q->count      = 0;
    q->head       = 0;
    q->tail       = 0;
    q->tx_waiters = NULL;
    q->rx_waiters = NULL;
}

int mrtos_queue_send(mrtos_queue_t *q, const void *item, uint16_t timeout)
{
    uint16_t key = port_irq_save();
    for (;;) {
        if (q->count < q->length) {
            memcpy(&q->buf[(size_t)q->head * q->item_size], item, q->item_size);
            if (++q->head == q->length)
                q->head = 0;
            q->count++;
            if (q->rx_waiters != NULL)
                wake_preempt_check(wake_first(&q->rx_waiters, MRTOS_OK));
            port_irq_restore(key);
            return MRTOS_OK;
        }
        if (timeout == 0) {
            port_irq_restore(key);
            return MRTOS_ERR_TIMEOUT;
        }
        mrtos_tcb_t *self = mrtos_cur;
        block_current(&q->tx_waiters, timeout);
        port_irq_restore(key);
        if (self->wake_res != MRTOS_OK)
            return MRTOS_ERR_TIMEOUT;
        key = port_irq_save();                /* re-check (Mesa)          */
    }
}

int mrtos_queue_recv(mrtos_queue_t *q, void *item, uint16_t timeout)
{
    uint16_t key = port_irq_save();
    for (;;) {
        if (q->count > 0) {
            memcpy(item, &q->buf[(size_t)q->tail * q->item_size], q->item_size);
            if (++q->tail == q->length)
                q->tail = 0;
            q->count--;
            if (q->tx_waiters != NULL)
                wake_preempt_check(wake_first(&q->tx_waiters, MRTOS_OK));
            port_irq_restore(key);
            return MRTOS_OK;
        }
        if (timeout == 0) {
            port_irq_restore(key);
            return MRTOS_ERR_TIMEOUT;
        }
        mrtos_tcb_t *self = mrtos_cur;
        block_current(&q->rx_waiters, timeout);
        port_irq_restore(key);
        if (self->wake_res != MRTOS_OK)
            return MRTOS_ERR_TIMEOUT;
        key = port_irq_save();
    }
}
