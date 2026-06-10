/*
 * mRTOS kernel test suite (POSIX port).
 * A priority-7 controller task orchestrates scenarios; worker tasks log
 * single-character events into a buffer which is then asserted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mrtos.h"

#define STK_WORDS 8192u
static port_stack_t stk_ctl[STK_WORDS], stk_a[STK_WORDS], stk_b[STK_WORDS],
                    stk_c[STK_WORDS], stk_hi[STK_WORDS], stk_lo[STK_WORDS],
                    stk_h2[STK_WORDS], stk_p[STK_WORDS], stk_q[STK_WORDS],
                    stk_x[STK_WORDS], stk_y[STK_WORDS];

static mrtos_tcb_t t_ctl, t_a, t_b, t_c, t_hi, t_lo, t_h2, t_p, t_q, t_x, t_y;

static char evbuf[512];
static int  evn;
static int  failures;

static void ev(char c) { evbuf[evn++] = c; }

#define CHECK(cond, msg) do {                                   \
    if (cond) { printf("  ok   - %s\n", msg); }                 \
    else      { printf("  FAIL - %s\n", msg); failures++; }     \
} while (0)

/* ---- test 1: priority order + sleep wake order ---- */
static void wa(void *x) { (void)x; ev('a'); mrtos_sleep(1); ev('A'); }
static void wb(void *x) { (void)x; ev('b'); mrtos_sleep(2); ev('B'); }
static void wc(void *x) { (void)x; ev('c'); mrtos_sleep(3); ev('C'); }

/* ---- test 2: semaphore ---- */
static mrtos_sem_t sem1, sem_to;
static void whi(void *x) { (void)x; ev('s'); mrtos_sem_take(&sem1, MRTOS_FOREVER); ev('S'); }

/* ---- test 3: mutex priority inheritance ---- */
static mrtos_mutex_t mtx;
static int low_unlock_rc = 99, high_unlock_rc = 99;
static void wlow(void *x) {
    (void)x;
    mrtos_mutex_lock(&mtx, MRTOS_FOREVER);
    ev('l');
    mrtos_sleep(5);                        /* hold the mutex while sleeping */
    ev('L');
    low_unlock_rc = mrtos_mutex_unlock(&mtx);
}
static void whigh(void *x) {
    (void)x;
    ev('h');
    mrtos_mutex_lock(&mtx, MRTOS_FOREVER);
    ev('H');
    high_unlock_rc = mrtos_mutex_unlock(&mtx);
}

/* ---- test 4: queue ---- */
static mrtos_queue_t qq;
static uint16_t qstore[4];
static uint16_t got[10];
static int prod_done, cons_done;
static void wprod(void *x) {
    (void)x;
    for (uint16_t i = 0; i < 10; i++)
        mrtos_queue_send(&qq, &i, MRTOS_FOREVER);
    prod_done = 1;
}
static void wcons(void *x) {
    (void)x;
    for (int i = 0; i < 10; i++)
        mrtos_queue_recv(&qq, &got[i], MRTOS_FOREVER);
    cons_done = 1;
}

/* ---- test 5: round-robin via yield ---- */
static void wx(void *x) { (void)x; for (int i = 0; i < 3; i++) { ev('x'); mrtos_yield(); } }
static void wy(void *x) { (void)x; for (int i = 0; i < 3; i++) { ev('y'); mrtos_yield(); } }

/* ---- controller ---- */
static void controller(void *x)
{
    (void)x;
    int mark;
    uint32_t t0, t1;

    printf("test 1: fixed-priority start order + delta-list wake order\n");
    mark = evn;
    mrtos_task_create(&t_a, "A", wa, NULL, 3, stk_a, STK_WORDS);
    mrtos_task_create(&t_b, "B", wb, NULL, 2, stk_b, STK_WORDS);
    mrtos_task_create(&t_c, "C", wc, NULL, 1, stk_c, STK_WORDS);
    mrtos_sleep(10);
    CHECK(strncmp(&evbuf[mark], "abcABC", 6) == 0, "sequence abcABC");

    printf("test 2: semaphore block/give\n");
    mark = evn;
    mrtos_sem_init(&sem1, 0, 1);
    mrtos_task_create(&t_hi, "HI", whi, NULL, 6, stk_hi, STK_WORDS);
    mrtos_sleep(1);                            /* let HI block on the sem  */
    ev('g');
    mrtos_sem_give(&sem1);
    mrtos_sleep(1);                            /* let HI consume it        */
    CHECK(strncmp(&evbuf[mark], "sgS", 3) == 0, "sequence sgS");

    printf("test 3: semaphore timeout\n");
    mrtos_sem_init(&sem_to, 0, 1);
    t0 = mrtos_now();
    int rc = mrtos_sem_take(&sem_to, 5);
    t1 = mrtos_now();
    CHECK(rc == MRTOS_ERR_TIMEOUT, "returns MRTOS_ERR_TIMEOUT");
    CHECK(t1 - t0 >= 5 && t1 - t0 <= 6, "blocked ~5 ticks");

    printf("test 4: mutex priority inheritance (single level)\n");
    mark = evn;
    mrtos_mutex_init(&mtx);
    mrtos_task_create(&t_lo, "LO", wlow, NULL, 1, stk_lo, STK_WORDS);
    mrtos_sleep(1);                            /* LO locks, sleeps holding */
    mrtos_task_create(&t_h2, "H2", whigh, NULL, 5, stk_h2, STK_WORDS);
    mrtos_sleep(1);                            /* H2 blocks on the mutex   */
    CHECK(t_lo.prio == 5 && t_lo.base_prio == 1, "owner inherited prio 5");
    mrtos_sleep(8);                            /* LO wakes, unlocks, H2 runs */
    CHECK(strncmp(&evbuf[mark], "lhLH", 4) == 0, "sequence lhLH (hand-off)");
    CHECK(t_lo.prio == 1, "inherited prio dropped at unlock");
    CHECK(low_unlock_rc == MRTOS_OK && high_unlock_rc == MRTOS_OK,
          "both unlocks returned OK");

    printf("test 5: queue producer/consumer through 4-slot buffer\n");
    mrtos_queue_init(&qq, qstore, sizeof(uint16_t), 4);
    mrtos_task_create(&t_q, "CONS", wcons, NULL, 2, stk_q, STK_WORDS);
    mrtos_task_create(&t_p, "PROD", wprod, NULL, 3, stk_p, STK_WORDS);
    mrtos_sleep(5);
    int order_ok = 1;
    for (int i = 0; i < 10; i++)
        if (got[i] != (uint16_t)i)
            order_ok = 0;
    CHECK(prod_done && cons_done, "producer and consumer completed");
    CHECK(order_ok, "10 items received in order through full queue");
    uint16_t dummy;
    t0 = mrtos_now();
    rc = mrtos_queue_recv(&qq, &dummy, 3);
    t1 = mrtos_now();
    CHECK(rc == MRTOS_ERR_TIMEOUT && t1 - t0 >= 3, "recv timeout on empty");

    printf("test 6: round-robin among equal priorities (yield)\n");
    mark = evn;
    mrtos_task_create(&t_x, "X", wx, NULL, 2, stk_x, STK_WORDS);
    mrtos_task_create(&t_y, "Y", wy, NULL, 2, stk_y, STK_WORDS);
    mrtos_sleep(2);
    CHECK(strncmp(&evbuf[mark], "xyxyxy", 6) == 0, "sequence xyxyxy");

    printf("\nevents: %.*s\n", evn, evbuf);
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        exit(0);
    }
    printf("%d FAILURE(S)\n", failures);
    exit(1);
}

int main(void)
{
    mrtos_init();
    mrtos_task_create(&t_ctl, "ctl", controller, NULL, 7, stk_ctl, STK_WORDS);
    mrtos_start();
    return 0;                                  /* not reached */
}
