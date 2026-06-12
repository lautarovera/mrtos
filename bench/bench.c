/*
 * mRTOS micro-benchmark.
 *
 * Measures the kernel's primitive costs between two marker functions:
 *
 *   bench_mark_a();  <operation under test>  ...  bench_mark_b();
 *
 * The markers carry no timing code, which keeps the measured path
 * pristine. How an interval becomes a number depends on where it runs:
 *
 *   - GNU MSP430 simulator: tools/bench.py drives msp430-elf-gdb
 *     (target sim), breaks on both markers and counts single-stepped
 *     instructions in between. Deterministic to the instruction -
 *     ideal for CI regression tracking. Not cycle-accurate (MSP430
 *     instructions take 1-6 cycles; FRAM wait states not modeled).
 *   - MSP430FR5994 target (future): the markers toggle a GPIO and the
 *     interval is measured externally (logic analyzer) or with a
 *     free-running Timer_A read at the same two points.
 *
 * Intervals may span a context switch (mark_a in the signaler, mark_b
 * first thing in the woken task), so "signal-to-wake" includes the
 * full hand-off: wake + preemption check + context switch + return
 * from the blocking call.
 *
 * The metric sequence and sample count are a contract with
 * tools/bench.py: BENCH_N_INTERVALS = metrics x samples, in bench_id
 * order.
 */
#include <stdlib.h>
#include "mrtos.h"

#define SAMPLES        3
#define N_SLEEPERS     8
#define STK_WORDS      256u

/* Metric ids, reported by tools/bench.py in this order. */
enum {
    BM_BASELINE = 0,   /* empty interval: marker overhead, subtracted */
    BM_YIELD,          /* one-way switch between equal-prio tasks     */
    BM_SEM_WAKE,       /* sem_give -> higher-prio waiter running      */
    BM_QSEND,          /* queue_send, poll, no waiter                 */
    BM_QRECV,          /* queue_recv, poll, data present              */
    BM_MUTEX,          /* uncontended lock + unlock                   */
    BM_TICK0,          /* mrtos_tick, empty delay list                */
    BM_TICK8,          /* mrtos_tick, 8 sleeping tasks (O(1) check)   */
    BM_COUNT
};

volatile uint8_t bench_id;

void __attribute__((noinline)) bench_mark_a(void) { __asm__ volatile("nop"); }
void __attribute__((noinline)) bench_mark_b(void) { __asm__ volatile("nop"); }

static mrtos_tcb_t  t_ctl, t_peer, t_waiter, t_slp[N_SLEEPERS];
static port_stack_t s_ctl[STK_WORDS], s_peer[STK_WORDS], s_wtr[STK_WORDS],
                    s_slp[N_SLEEPERS][STK_WORDS];

static mrtos_sem_t   sem;
static mrtos_queue_t q;
static uint16_t      qstore[4];

/* Equal-priority peer: completes the yield round-trip. */
static void peer_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_b();                /* end of ctl's one-way switch */
        mrtos_yield();
    }
}

/* Higher-priority waiter: preempts the giver on each give. */
static void waiter_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < SAMPLES; i++) {
        mrtos_sem_take(&sem, MRTOS_FOREVER);
        bench_mark_b();                /* running again: hand-off done */
    }
}

static void sleeper_task(void *arg)
{
    (void)arg;
    mrtos_sleep(30000);                /* park in the delay list */
}

static void controller(void *arg)
{
    (void)arg;
    uint16_t v = 42;

    bench_id = BM_BASELINE;
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        bench_mark_b();
    }

    bench_id = BM_YIELD;
    mrtos_task_create(&t_peer, "peer", peer_task, NULL, 6,
                      s_peer, STK_WORDS);
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_yield();                 /* peer runs mark_b, yields back */
    }
    mrtos_sleep(1);                    /* let the peer retire */

    bench_id = BM_SEM_WAKE;
    mrtos_sem_init(&sem, 0, 1);
    mrtos_task_create(&t_waiter, "wtr", waiter_task, NULL, 7,
                      s_wtr, STK_WORDS);    /* preempts, blocks on sem */
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_sem_give(&sem);          /* waiter preempts, marks b */
    }
    mrtos_sleep(1);

    bench_id = BM_QSEND;
    mrtos_queue_init(&q, qstore, sizeof(uint16_t), 4);
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_queue_send(&q, &v, 0);
        bench_mark_b();
    }
    bench_id = BM_QRECV;
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_queue_recv(&q, &v, 0);
        bench_mark_b();
    }

    bench_id = BM_MUTEX;
    static mrtos_mutex_t mtx;
    mrtos_mutex_init(&mtx);
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_mutex_lock(&mtx, 0);
        mrtos_mutex_unlock(&mtx);
        bench_mark_b();
    }

    /* Tick cost with an empty delay list... */
    bench_id = BM_TICK0;
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_tick();
        bench_mark_b();
    }

    /* ...and with 8 sleepers parked: must not get more expensive
     * (delta list: only the head is ever decremented). */
    for (int i = 0; i < N_SLEEPERS; i++)
        mrtos_task_create(&t_slp[i], "slp", sleeper_task, NULL, 1,
                          s_slp[i], STK_WORDS);
    mrtos_sleep(1);                    /* let them enter the delay list */
    bench_id = BM_TICK8;
    for (int i = 0; i < SAMPLES; i++) {
        bench_mark_a();
        mrtos_tick();
        bench_mark_b();
    }

    exit(0);
}

int main(void)
{
    mrtos_init();
    mrtos_task_create(&t_ctl, "ctl", controller, NULL, 6,
                      s_ctl, STK_WORDS);
    mrtos_start();
    return 0;
}
