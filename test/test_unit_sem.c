/* Unit tests: counting semaphore. */
#include "test_common.h"

static mrtos_tcb_t  t_lo, t_mid, t_hi, t_f1, t_f2;
static port_stack_t s_lo[TEST_STK_WORDS], s_mid[TEST_STK_WORDS],
                    s_hi[TEST_STK_WORDS], s_f1[TEST_STK_WORDS],
                    s_f2[TEST_STK_WORDS];

static mrtos_sem_t sem;

static void w_take(void *arg)
{
    char tag = *(const char *)arg;
    mrtos_sem_take(&sem, MRTOS_FOREVER);
    ev(tag);
}

static int timed_rc = 99;
static void w_take_timed(void *arg)
{
    (void)arg;
    ev('t');
    timed_rc = mrtos_sem_take(&sem, 4);
    ev('T');
}

static void test_main(void)
{
    int mark, rc;
    uint32_t t0, t1;

    printf("sem: initial count is consumed without blocking\n");
    mrtos_sem_init(&sem, 2, 4);
    CHECK(mrtos_sem_take(&sem, 0) == MRTOS_OK, "first poll take OK");
    CHECK(mrtos_sem_take(&sem, 0) == MRTOS_OK, "second poll take OK");
    CHECK(mrtos_sem_take(&sem, 0) == MRTOS_ERR_TIMEOUT,
          "third poll fails: count exhausted");

    printf("sem: give saturates at the configured limit\n");
    mrtos_sem_init(&sem, 0, 2);
    for (int i = 0; i < 5; i++)
        mrtos_sem_give(&sem);
    CHECK(sem.count == 2, "count capped at limit after 5 gives");
    CHECK(mrtos_sem_take(&sem, 0) == MRTOS_OK &&
          mrtos_sem_take(&sem, 0) == MRTOS_OK &&
          mrtos_sem_take(&sem, 0) == MRTOS_ERR_TIMEOUT,
          "exactly limit tokens retrievable");

    printf("sem: blocked take returns OK on give\n");
    mrtos_sem_init(&sem, 0, 1);
    mark = test_evn;
    static const char ch = 'h';
    mrtos_task_create(&t_hi, "hi", w_take, (void *)&ch, 5,
                      s_hi, TEST_STK_WORDS);
    mrtos_sleep(1);                /* let it block */
    CHECK(t_hi.state == MRTOS_TASK_BLOCKED, "waiter is BLOCKED");
    ev('g');
    mrtos_sem_give(&sem);
    mrtos_sleep(1);
    CHECK_SEQ(mark, "gh");
    CHECK(sem.count == 0, "token went to the waiter, not the count");

    printf("sem: timeout returns MRTOS_ERR_TIMEOUT after exact ticks\n");
    mrtos_sem_init(&sem, 0, 1);
    t0 = mrtos_now();
    rc = mrtos_sem_take(&sem, 5);
    t1 = mrtos_now();
    CHECK(rc == MRTOS_ERR_TIMEOUT, "rc == MRTOS_ERR_TIMEOUT");
    CHECK(t1 - t0 >= 5 && t1 - t0 <= 6, "blocked ~5 ticks");
    CHECK(sem.waiters == NULL, "timed-out task removed from wait list");

    printf("sem: give wakes waiters in priority order\n");
    mrtos_sem_init(&sem, 0, 1);
    mark = test_evn;
    static const char cl = 'l', cm = 'm', ci = 'i';
    mrtos_task_create(&t_lo,  "lo",  w_take, (void *)&cl, 1,
                      s_lo,  TEST_STK_WORDS);
    mrtos_task_create(&t_mid, "mid", w_take, (void *)&cm, 3,
                      s_mid, TEST_STK_WORDS);
    mrtos_task_create(&t_hi,  "hi2", w_take, (void *)&ci, 5,
                      s_hi,  TEST_STK_WORDS);
    mrtos_sleep(1);                /* all three block, creation order */
    for (int i = 0; i < 3; i++) {
        mrtos_sem_give(&sem);
        mrtos_sleep(1);
    }
    CHECK_SEQ(mark, "iml");        /* descending priority, not FIFO */

    printf("sem: FIFO among waiters of equal priority\n");
    mrtos_sem_init(&sem, 0, 1);
    mark = test_evn;
    static const char c1 = '1', c2 = '2';
    mrtos_task_create(&t_f1, "f1", w_take, (void *)&c1, 2,
                      s_f1, TEST_STK_WORDS);
    mrtos_task_create(&t_f2, "f2", w_take, (void *)&c2, 2,
                      s_f2, TEST_STK_WORDS);
    mrtos_sleep(1);
    mrtos_sem_give(&sem);
    mrtos_sleep(1);
    mrtos_sem_give(&sem);
    mrtos_sleep(1);
    CHECK_SEQ(mark, "12");         /* first blocked, first woken */

    printf("sem: waiter timeout does not disturb a later give\n");
    mrtos_sem_init(&sem, 0, 1);
    mark = test_evn;
    mrtos_task_create(&t_f1, "tt", w_take_timed, NULL, 2,
                      s_f1, TEST_STK_WORDS);
    mrtos_sleep(6);                /* waiter blocks at +0, times out at +4 */
    CHECK(timed_rc == MRTOS_ERR_TIMEOUT, "waiter saw timeout");
    mrtos_sem_give(&sem);          /* nobody waiting: goes to count */
    CHECK(sem.count == 1, "give after timeout increments count");
    CHECK_SEQ(mark, "tT");
}
