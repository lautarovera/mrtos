/*
 * Integration tests: multi-task concurrency, synchronization and
 * deterministic race scenarios.
 *
 * Note on scope: the POSIX port is cooperative (ticks advance only in
 * idle), so asynchronous preemption races cannot fire here — those are
 * exercised on target (see doc/VALIDATION.md). What this file pins down
 * is the kernel's *logical* race resolution: same-tick timeout vs. give,
 * Mesa-style wakeup stealing, and priority-inversion bounding via PI.
 */
#include "test_common.h"

static mrtos_tcb_t  t_p1, t_p2, t_c1, t_c2, t_aux;
static port_stack_t s_p1[TEST_STK_WORDS], s_p2[TEST_STK_WORDS],
                    s_c1[TEST_STK_WORDS], s_c2[TEST_STK_WORDS],
                    s_aux[TEST_STK_WORDS];

/* ---- scenario 1: 2 producers / 2 consumers through a 4-slot queue ---- */
#define ITEMS_PER_PROD 20
static mrtos_queue_t q;
static uint16_t      qstore[4];
static uint16_t      log_buf[2 * ITEMS_PER_PROD];
static int           log_n;
static int           prods_done, cons_running;

static void producer(void *arg)
{
    uint16_t tag = (uint16_t)(uintptr_t)arg;       /* 0x0100 or 0x0200 */
    for (uint16_t i = 0; i < ITEMS_PER_PROD; i++) {
        uint16_t v = (uint16_t)(tag | i);
        mrtos_queue_send(&q, &v, MRTOS_FOREVER);
        if ((i & 3) == 0)
            mrtos_sleep(1);                        /* stir the schedule */
    }
    prods_done++;
}

static void consumer(void *arg)
{
    (void)arg;
    uint16_t v;
    cons_running++;
    while (mrtos_queue_recv(&q, &v, 8) == MRTOS_OK)
        log_buf[log_n++] = v;
    cons_running--;
}

/* ---- scenario 2: same-tick timeout vs. give ---- */
static mrtos_sem_t race_sem;
static void giver(void *arg)
{
    uint16_t delay = (uint16_t)(uintptr_t)arg;
    mrtos_sleep(delay);
    mrtos_sem_give(&race_sem);
}

/* ---- scenario 3: Mesa wakeup steal ---- */
static mrtos_queue_t mq;
static uint16_t      mq_store[1];
static uint16_t      mesa_got;
static int           mesa_wakeups;
static void mesa_waiter(void *arg)
{
    (void)arg;
    /* One blocking recv. If the woken check finds the queue empty
     * (slot stolen), the kernel loops internally - we count laps by
     * instrumenting around the call. */
    mesa_wakeups++;
    mrtos_queue_recv(&mq, &mesa_got, MRTOS_FOREVER);
    ev('w');
}

/* ---- scenario 4: priority inversion bounded by PI ---- */
static mrtos_mutex_t inv_mtx;
static uint32_t      inv_acquired_at;
static void inv_low(void *arg)
{
    (void)arg;
    mrtos_mutex_lock(&inv_mtx, MRTOS_FOREVER);
    ev('l');
    mrtos_sleep(6);                                /* long hold */
    mrtos_mutex_unlock(&inv_mtx);
    ev('L');
}
static void inv_mid(void *arg)
{
    (void)arg;
    for (int i = 0; i < 8; i++) {
        ev('m');
        mrtos_sleep(2);                            /* periodic interferer */
    }
}
static void inv_high(void *arg)
{
    (void)arg;
    ev('h');
    mrtos_mutex_lock(&inv_mtx, MRTOS_FOREVER);
    inv_acquired_at = mrtos_now();
    ev('H');
    mrtos_mutex_unlock(&inv_mtx);
}

static void test_main(void)
{
    int mark;

    printf("integration 1: 2 producers + 2 consumers, 4-slot queue\n");
    mrtos_queue_init(&q, qstore, sizeof(uint16_t), 4);
    mrtos_task_create(&t_c1, "c1", consumer, NULL, 2, s_c1, TEST_STK_WORDS);
    mrtos_task_create(&t_c2, "c2", consumer, NULL, 2, s_c2, TEST_STK_WORDS);
    mrtos_task_create(&t_p1, "p1", producer, (void *)0x0100,
                      3, s_p1, TEST_STK_WORDS);
    mrtos_task_create(&t_p2, "p2", producer, (void *)0x0200,
                      3, s_p2, TEST_STK_WORDS);
    while (prods_done < 2 || cons_running > 0)
        mrtos_sleep(5);                            /* consumers exit on timeout */
    CHECK(log_n == 2 * ITEMS_PER_PROD, "all 40 items delivered exactly once");
    int seq_ok = 1, last1 = -1, last2 = -1;
    for (int i = 0; i < log_n; i++) {
        int tag = log_buf[i] & 0xFF00, s = log_buf[i] & 0xFF;
        if (tag == 0x0100) { if (s <= last1) seq_ok = 0; last1 = s; }
        else               { if (s <= last2) seq_ok = 0; last2 = s; }
    }
    CHECK(seq_ok, "per-producer sequence preserved end to end");
    CHECK(last1 == ITEMS_PER_PROD - 1 && last2 == ITEMS_PER_PROD - 1,
          "no loss: both streams complete");
    CHECK(q.count == 0 && q.tx_waiters == NULL && q.rx_waiters == NULL,
          "queue fully quiesced");

    printf("integration 2a: give one tick BEFORE timeout wins\n");
    mrtos_sem_init(&race_sem, 0, 1);
    mrtos_task_create(&t_aux, "gv", giver, (void *)4, 6,
                      s_aux, TEST_STK_WORDS);
    int rc = mrtos_sem_take(&race_sem, 5);
    CHECK(rc == MRTOS_OK, "take returns OK");
    CHECK(race_sem.count == 0, "token consumed, not double-counted");
    mrtos_sleep(2);                /* let the giver retire before TCB reuse */

    printf("integration 2b: give on the SAME tick as timeout\n");
    /* Both the waiter's timeout and the giver's wake land on tick N.
     * The tick handler readies the timed-out waiter first; the waiter
     * (higher prio) sees TIMEOUT, then the give lands in the count.
     * The invariant under test: exactly one of {waiter OK, count==1},
     * never both, never neither. */
    mrtos_sem_init(&race_sem, 0, 1);
    mrtos_task_create(&t_aux, "gv", giver, (void *)5, 6,
                      s_aux, TEST_STK_WORDS);
    rc = mrtos_sem_take(&race_sem, 5);
    mrtos_sleep(2);                                /* let the giver finish */
    int token_in_count = (race_sem.count == 1);
    CHECK((rc == MRTOS_OK && !token_in_count) ||
          (rc == MRTOS_ERR_TIMEOUT && token_in_count),
          "exactly one token outcome (no loss, no duplication)");

    printf("integration 3: Mesa wakeup - stolen slot forces re-block\n");
    mrtos_queue_init(&mq, mq_store, sizeof(uint16_t), 1);
    mark = test_evn;
    mesa_wakeups = 0;
    mrtos_task_create(&t_aux, "mw", mesa_waiter, NULL, 1,
                      s_aux, TEST_STK_WORDS);
    mrtos_sleep(1);                                /* waiter blocks on recv */
    uint16_t v = 111;
    mrtos_queue_send(&mq, &v, 0);  /* wakes waiter, but we keep running */
    uint16_t stolen;
    CHECK(mrtos_queue_recv(&mq, &stolen, 0) == MRTOS_OK && stolen == 111,
          "controller steals the item before the waiter runs");
    mrtos_sleep(1);                /* waiter re-checks: empty, re-blocks */
    CHECK(t_aux.state == MRTOS_TASK_BLOCKED, "waiter re-blocked (Mesa loop)");
    v = 222;
    mrtos_queue_send(&mq, &v, 0);
    mrtos_sleep(1);
    CHECK(mesa_got == 222 && mesa_wakeups == 1,
          "single recv call survived the steal and got the next item");
    CHECK_SEQ(mark, "w");

    printf("integration 4: priority inversion bounded by inheritance\n");
    mrtos_mutex_init(&inv_mtx);
    mark = test_evn;
    mrtos_task_create(&t_p1, "low", inv_low, NULL, 1, s_p1, TEST_STK_WORDS);
    mrtos_task_create(&t_p2, "mid", inv_mid, NULL, 3, s_p2, TEST_STK_WORDS);
    mrtos_sleep(1);                /* low locks; mid starts its cycle */
    uint32_t blocked_at = mrtos_now();
    mrtos_task_create(&t_c1, "high", inv_high, NULL, 5,
                      s_c1, TEST_STK_WORDS);
    mrtos_sleep(2);
    CHECK(t_p1.prio == 5, "low boosted above the interferer");
    mrtos_sleep(20);               /* drain the scenario */
    CHECK(inv_acquired_at - blocked_at <= 6,
          "high waited only for low's hold, not for mid");
    /* The discriminating moment is the release tick: low and mid become
     * ready together. With PI low runs at prio 5 and hands off to high
     * before mid (prio 3) gets the CPU; without PI mid would slip in a
     * 4th 'm' first. ('m' at ticks 0/2/4 while the owner sleeps is not
     * inversion - high is waiting on low's hold there, not on mid.) */
    const char *evs = ev_since(mark);
    const char *H = strchr(evs, 'H');
    CHECK(H != NULL && strchr(evs, 'l') != NULL,
          "both critical sections ran");
    int m_before_H = 0;
    for (const char *p = evs; H && p < H; p++)
        if (*p == 'm')
            m_before_H++;
    CHECK(m_before_H == 3, "release tick went to the boosted owner");
}
