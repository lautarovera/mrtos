/* Unit tests: mutex, error paths and priority inheritance. */
#include "test_common.h"

static mrtos_tcb_t  t_own, t_hi, t_w1, t_w2, t_w3;
static port_stack_t s_own[TEST_STK_WORDS], s_hi[TEST_STK_WORDS],
                    s_w1[TEST_STK_WORDS], s_w2[TEST_STK_WORDS],
                    s_w3[TEST_STK_WORDS];

static mrtos_mutex_t mtx;
static mrtos_sem_t   gate;

/* Locks, then sleeps holding the mutex, then unlocks. */
static void w_hold_sleep(void *arg)
{
    (void)arg;
    mrtos_mutex_lock(&mtx, MRTOS_FOREVER);
    ev('o');
    mrtos_sleep(5);
    ev('O');
    mrtos_mutex_unlock(&mtx);
}

/* Locks, then blocks on the gate semaphore holding the mutex. */
static void w_hold_gate(void *arg)
{
    (void)arg;
    mrtos_mutex_lock(&mtx, MRTOS_FOREVER);
    ev('o');
    mrtos_sem_take(&gate, MRTOS_FOREVER);
    ev('O');
    mrtos_mutex_unlock(&mtx);
}

static int hi_rc = 99;
static void w_contend(void *arg)
{
    char tag = *(const char *)arg;
    hi_rc = mrtos_mutex_lock(&mtx, MRTOS_FOREVER);
    ev(tag);
    mrtos_mutex_unlock(&mtx);
}

static int timed_rc = 99;
static void w_contend_timed(void *arg)
{
    (void)arg;
    timed_rc = mrtos_mutex_lock(&mtx, 3);
    ev(timed_rc == MRTOS_OK ? 'K' : 'X');
    if (timed_rc == MRTOS_OK)
        mrtos_mutex_unlock(&mtx);
}

static void test_main(void)
{
    int mark;

    printf("mutex: uncontended lock/unlock\n");
    mrtos_mutex_init(&mtx);
    CHECK(mrtos_mutex_lock(&mtx, 0) == MRTOS_OK, "lock OK");
    CHECK(mtx.owner == mrtos_cur, "owner is the caller");
    CHECK(mrtos_mutex_unlock(&mtx) == MRTOS_OK, "unlock OK");
    CHECK(mtx.owner == NULL, "owner cleared");

    printf("mutex: misuse is rejected with MRTOS_ERR_OWNER\n");
    CHECK(mrtos_mutex_unlock(&mtx) == MRTOS_ERR_OWNER,
          "unlock when not owned");
    mrtos_mutex_lock(&mtx, 0);
    CHECK(mrtos_mutex_lock(&mtx, MRTOS_FOREVER) == MRTOS_ERR_OWNER,
          "recursive lock rejected");
    mrtos_mutex_unlock(&mtx);

    printf("mutex: poll (timeout=0) on an owned mutex fails fast\n");
    mrtos_mutex_init(&mtx);
    mrtos_task_create(&t_own, "own", w_hold_sleep, NULL, 2,
                      s_own, TEST_STK_WORDS);
    mrtos_sleep(1);                /* owner locks and sleeps */
    CHECK(mrtos_mutex_lock(&mtx, 0) == MRTOS_ERR_TIMEOUT,
          "poll returns MRTOS_ERR_TIMEOUT");
    mrtos_sleep(10);               /* let owner finish */

    printf("mutex: blocked lock times out; owner keeps the mutex\n");
    mrtos_mutex_init(&mtx);
    mark = test_evn;
    mrtos_task_create(&t_own, "own", w_hold_sleep, NULL, 2,
                      s_own, TEST_STK_WORDS);
    mrtos_sleep(1);
    timed_rc = 99;
    mrtos_task_create(&t_w1, "tmo", w_contend_timed, NULL, 4,
                      s_w1, TEST_STK_WORDS);
    mrtos_sleep(4);                /* waiter times out at +3 */
    CHECK(timed_rc == MRTOS_ERR_TIMEOUT, "lock rc == MRTOS_ERR_TIMEOUT");
    CHECK(mtx.owner == &t_own, "owner unchanged after waiter timeout");
    CHECK(mtx.waiters == NULL, "wait list empty after timeout");
    mrtos_sleep(10);
    CHECK_SEQ(mark, "oXO");        /* timeout fired before owner woke */

    printf("mutex: priority inheritance on a READY owner\n");
    mrtos_mutex_init(&mtx);
    mark = test_evn;
    mrtos_task_create(&t_own, "own", w_hold_sleep, NULL, 1,
                      s_own, TEST_STK_WORDS);
    mrtos_sleep(1);
    static const char chh = 'h';
    hi_rc = 99;
    mrtos_task_create(&t_hi, "hi", w_contend, (void *)&chh, 5,
                      s_hi, TEST_STK_WORDS);
    mrtos_sleep(1);                /* hi blocks on the mutex */
    CHECK(t_own.prio == 5 && t_own.base_prio == 1, "owner raised to 5");
    mrtos_sleep(10);
    CHECK(hi_rc == MRTOS_OK, "contender finally locked");
    CHECK(t_own.prio == 1, "owner demoted at unlock");
    CHECK_SEQ(mark, "oOh");        /* hand-off straight to the waiter */

    printf("mutex: priority inheritance on a BLOCKED owner (re-sort)\n");
    mrtos_mutex_init(&mtx);
    mrtos_sem_init(&gate, 0, 1);
    mark = test_evn;
    mrtos_task_create(&t_own, "own", w_hold_gate, NULL, 1,
                      s_own, TEST_STK_WORDS);
    mrtos_sleep(1);                /* owner locked, blocked on gate */
    hi_rc = 99;
    mrtos_task_create(&t_hi, "hi", w_contend, (void *)&chh, 6,
                      s_hi, TEST_STK_WORDS);
    mrtos_sleep(1);
    CHECK(t_own.prio == 6, "blocked owner inherited prio 6");
    mrtos_sem_give(&gate);         /* release the owner */
    mrtos_sleep(2);
    CHECK(hi_rc == MRTOS_OK && t_own.prio == 1,
          "owner ran at inherited prio, then demoted");
    CHECK_SEQ(mark, "oOh");

    printf("mutex: unlock hands off to waiters in priority order\n");
    mrtos_mutex_init(&mtx);
    mrtos_sem_init(&gate, 0, 1);
    mark = test_evn;
    mrtos_task_create(&t_own, "own", w_hold_gate, NULL, 6,
                      s_own, TEST_STK_WORDS);
    mrtos_sleep(1);
    static const char c1 = '1', c2 = '2', c3 = '3';
    mrtos_task_create(&t_w1, "w1", w_contend, (void *)&c1, 2,
                      s_w1, TEST_STK_WORDS);
    mrtos_task_create(&t_w2, "w2", w_contend, (void *)&c2, 4,
                      s_w2, TEST_STK_WORDS);
    mrtos_task_create(&t_w3, "w3", w_contend, (void *)&c3, 3,
                      s_w3, TEST_STK_WORDS);
    mrtos_sleep(1);                /* all three queue on the mutex */
    mrtos_sem_give(&gate);
    mrtos_sleep(3);                /* chain of hand-offs drains */
    CHECK_SEQ(mark, "oO231");      /* prio 4, then 3, then 2 */
}
