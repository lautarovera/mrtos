/*
 * mRTOS - minimal preemptive RTOS for MSP430FR59xx (portable core)
 *
 * Kernel public API. The kernel is port-agnostic: everything CPU/board
 * specific lives behind the port contract declared at the bottom of this
 * file and implemented in port/<arch>/port.{h,c}.
 *
 * Author: generated design for lautarovera, 2026. MIT license.
 */
#ifndef MRTOS_H
#define MRTOS_H

#include <stdint.h>
#include <stddef.h>
#include "mrtos_config.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Configuration defaults (override in mrtos_config.h)                 */
/* ------------------------------------------------------------------ */
#ifndef MRTOS_CFG_TICK_HZ
#define MRTOS_CFG_TICK_HZ          1000u
#endif
#ifndef MRTOS_CFG_SLICE_TICKS
#define MRTOS_CFG_SLICE_TICKS      10u     /* round-robin quantum        */
#endif
#ifndef MRTOS_CFG_IDLE_STACK_WORDS
#define MRTOS_CFG_IDLE_STACK_WORDS 64u
#endif
#ifndef MRTOS_CFG_STACK_CHECK
#define MRTOS_CFG_STACK_CHECK      1
#endif
#ifndef MRTOS_CFG_STACK_USAGE
#define MRTOS_CFG_STACK_USAGE      1   /* paint stacks, enable high-water */
#endif

/* Stack paint pattern (distinct from the 0x5AFE guard words). Ports
 * that substitute their own stack memory repaint with this value. */
#define MRTOS_STACK_PAINT   0xC35Au

/* Low-power-mode levels for the power-lock API (mrtos_pm_lock). The
 * kernel only tracks the integer level; the port maps it to hardware
 * (on MSP430: LPM0..LPM4). Higher = deeper sleep. A tickless port idles
 * at the deepest level no driver has vetoed. */
#define MRTOS_LPM0  0u
#define MRTOS_LPM1  1u
#define MRTOS_LPM2  2u
#define MRTOS_LPM3  3u
#define MRTOS_LPM4  4u
#define MRTOS_LPM_LEVELS  5u
#ifndef MRTOS_CFG_LPM_DEFAULT
#define MRTOS_CFG_LPM_DEFAULT  MRTOS_LPM3  /* deepest idle when unvetoed */
#endif

#define MRTOS_PRIO_LEVELS   8u             /* 0 = idle (lowest) .. 7     */
#define MRTOS_PRIO_MAX      (MRTOS_PRIO_LEVELS - 1u)

#define MRTOS_FOREVER       0xFFFFu        /* timeout sentinel            */

/* Return codes */
#define MRTOS_OK            0
#define MRTOS_ERR_TIMEOUT   (-1)
#define MRTOS_ERR_OWNER     (-2)           /* mutex misuse                */

#define MRTOS_MS(ms)  ((uint16_t)(((uint32_t)(ms) * MRTOS_CFG_TICK_HZ) / 1000u))

typedef enum {
    MRTOS_TASK_READY     = 0,              /* in a ready list (or running)*/
    MRTOS_TASK_BLOCKED   = 1,              /* waiting on object/delay     */
    MRTOS_TASK_SUSPENDED = 2               /* exited; never scheduled     */
} mrtos_state_t;

/* ------------------------------------------------------------------ */
/* Task control block. sp MUST be the first member: the MSP430 port    */
/* context-switch assembly stores/loads SP at offset 0 of the TCB.     */
/* ------------------------------------------------------------------ */
typedef struct mrtos_tcb {
    port_stack_t      *sp;                 /* saved stack pointer  (+0)   */
    struct mrtos_tcb  *next;               /* ready/wait list (circular)  */
    struct mrtos_tcb  *prev;
    struct mrtos_tcb **wait_head;          /* wait list head we sit in    */
    struct mrtos_tcb  *dnext;              /* delta delay list            */
    uint16_t           delta;              /* ticks relative to dprev     */
    uint8_t            in_delay;
    uint8_t            prio;               /* effective (may be inherited)*/
    uint8_t            base_prio;          /* assigned at creation        */
    uint8_t            state;
    int8_t             wake_res;           /* result seen by unblocked task*/
    void             (*entry)(void *);
    void              *arg;
    port_stack_t      *stack_base;
    size_t             stack_words;
    const char        *name;
#ifdef PORT_TCB_EXT
    PORT_TCB_EXT                           /* port-private extension      */
#endif
} mrtos_tcb_t;

/* Currently running task. Read-only outside the kernel/port.           */
extern mrtos_tcb_t * volatile mrtos_cur;

/* ------------------------------------------------------------------ */
/* Core API                                                             */
/* ------------------------------------------------------------------ */
void     mrtos_init(void);
void     mrtos_task_create(mrtos_tcb_t *tcb, const char *name,
                           void (*entry)(void *), void *arg, uint8_t prio,
                           port_stack_t *stack, size_t stack_words);
void     mrtos_start(void);                /* does not return             */
void     mrtos_yield(void);
void     mrtos_sleep(uint16_t ticks);      /* ticks > 0                   */
uint32_t mrtos_now(void);                  /* tick count since start      */

/* ------------------------------------------------------------------ */
/* Counting semaphore. give() is ISR-safe. take(timeout=0) polls.       */
/* ------------------------------------------------------------------ */
typedef struct {
    int16_t       count;
    int16_t       limit;
    mrtos_tcb_t  *waiters;                 /* prio-ordered, FIFO in prio  */
} mrtos_sem_t;

void mrtos_sem_init(mrtos_sem_t *s, int16_t initial, int16_t limit);
int  mrtos_sem_take(mrtos_sem_t *s, uint16_t timeout);
int  mrtos_sem_give(mrtos_sem_t *s);

/* ------------------------------------------------------------------ */
/* Mutex with single-level priority inheritance. Not recursive.         */
/* Must not be used from ISRs.                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    mrtos_tcb_t  *owner;
    mrtos_tcb_t  *waiters;
} mrtos_mutex_t;

void mrtos_mutex_init(mrtos_mutex_t *m);
int  mrtos_mutex_lock(mrtos_mutex_t *m, uint16_t timeout);
int  mrtos_mutex_unlock(mrtos_mutex_t *m);

/* ------------------------------------------------------------------ */
/* Message queue: fixed-size items, copy semantics (Mesa-style wakeups).*/
/* send/recv with timeout=0 are ISR-safe (non-blocking).                */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t      *buf;
    uint16_t      item_size;
    uint16_t      length;
    uint16_t      count;
    uint16_t      head;                    /* next write slot             */
    uint16_t      tail;                    /* next read slot              */
    mrtos_tcb_t  *tx_waiters;
    mrtos_tcb_t  *rx_waiters;
} mrtos_queue_t;

void mrtos_queue_init(mrtos_queue_t *q, void *storage,
                      uint16_t item_size, uint16_t length);
int  mrtos_queue_send(mrtos_queue_t *q, const void *item, uint16_t timeout);
int  mrtos_queue_recv(mrtos_queue_t *q, void *item, uint16_t timeout);

/* Weak hook: called with interrupts disabled on guard corruption.      */
void mrtos_stack_overflow_hook(mrtos_tcb_t *t);

/* High-water mark: words of t's stack never touched since creation
 * (0 when MRTOS_CFG_STACK_USAGE is off). O(stack size) scan - call it
 * from diagnostics/idle code, not hot paths. Standard paint-pattern
 * caveat: a task that legitimately writes MRTOS_STACK_PAINT at its
 * deepest extent under-reports by those words.                         */
size_t mrtos_stack_unused(const mrtos_tcb_t *t);

/* ------------------------------------------------------------------ */
/* Power-mode locks. A driver caps idle's sleep depth while it needs    */
/* clocks running (e.g. LEA: mrtos_pm_lock(MRTOS_LPM0)). Balanced       */
/* lock/unlock; counter-based, so nesting and multiple holders compose. */
/* ISR-safe.                                                            */
/* ------------------------------------------------------------------ */
void    mrtos_pm_lock(uint8_t max_lpm);
void    mrtos_pm_unlock(uint8_t max_lpm);
uint8_t mrtos_pm_max_lpm(void);            /* deepest level allowed now   */

/* ------------------------------------------------------------------ */
/* Kernel entry points used by the PORT (not by application code)       */
/* ------------------------------------------------------------------ */
void mrtos_tick(void);                     /* tick ISR body               */
void mrtos_sched_pick(void);               /* select highest-prio ready   */
/* Tickless support: a tickless port_idle() reads mrtos_next_deadline()  */
/* to size an LPM sleep, then mrtos_tick_advance(elapsed) on wake. Both  */
/* called with interrupts disabled, like mrtos_tick().                   */
uint16_t mrtos_next_deadline(void);        /* ticks to earliest wake, 0=none */
void     mrtos_tick_advance(uint16_t n);   /* fold n elapsed ticks at once   */

/* ------------------------------------------------------------------ */
/* PORT CONTRACT - every port must provide (in port.h / port.c):        */
/*                                                                      */
/*   typedef ... port_stack_t;                                          */
/*   uint16_t      port_irq_save(void);                                 */
/*   void          port_irq_restore(uint16_t key);                      */
/*   void          port_yield(void);     deferred or immediate switch   */
/*   port_stack_t *port_stack_init(port_stack_t *base, size_t words,    */
/*                                 void (*shell)(void *), void *arg);   */
/*   void          port_start(void);     tick setup + first task, noret */
/*   void          port_idle(void);      idle-loop body (e.g. LPM0)     */
/*   optional: #define PORT_TCB_EXT  <extra TCB fields>                 */
/* ------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif
#endif /* MRTOS_H */
