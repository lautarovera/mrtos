/* Unit tests: task creation, priorities, sleep, yield, retire. */
#include "test_common.h"

static mrtos_tcb_t  t1, t2, t3;
static port_stack_t s1[TEST_STK_WORDS], s2[TEST_STK_WORDS],
                    s3[TEST_STK_WORDS];

static void w_mark(void *arg) { ev(*(const char *)arg); }

static void w_sleeper(void *arg)
{
    (void)arg;
    ev('s');
    mrtos_sleep(7);
    ev('S');
}

static int yields_seen;
static void w_yielder(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        ev('y');
        mrtos_yield();
        yields_seen++;
    }
}

static void test_main(void)
{
    int mark;
    uint32_t t0, t1ck;

    printf("task: creation order follows priority\n");
    mark = test_evn;
    static const char ca = 'a', cb = 'b', cc = 'c';
    mrtos_task_create(&t1, "lo",  w_mark, (void *)&ca, 1, s1, TEST_STK_WORDS);
    mrtos_task_create(&t2, "hi",  w_mark, (void *)&cb, 5, s2, TEST_STK_WORDS);
    mrtos_task_create(&t3, "mid", w_mark, (void *)&cc, 3, s3, TEST_STK_WORDS);
    mrtos_sleep(1);
    CHECK_SEQ(mark, "bca");        /* highest priority first */

    printf("task: returned tasks are retired to SUSPENDED\n");
    CHECK(t1.state == MRTOS_TASK_SUSPENDED &&
          t2.state == MRTOS_TASK_SUSPENDED &&
          t3.state == MRTOS_TASK_SUSPENDED, "all three retired");

    printf("task: priority above MRTOS_PRIO_MAX is clamped\n");
    mrtos_task_create(&t1, "clamp", w_mark, (void *)&ca, 9,
                      s1, TEST_STK_WORDS);
    CHECK(t1.prio == MRTOS_PRIO_MAX && t1.base_prio == MRTOS_PRIO_MAX,
          "prio 9 clamped to 7");
    mrtos_yield();                 /* equal prio: let it run and retire */
    CHECK(t1.state == MRTOS_TASK_SUSPENDED, "clamped task ran");

    printf("task: sleep blocks for the requested tick count\n");
    mark = test_evn;
    mrtos_task_create(&t2, "slp", w_sleeper, NULL, 4, s2, TEST_STK_WORDS);
    t0 = mrtos_now();
    mrtos_sleep(10);
    t1ck = mrtos_now();
    CHECK_SEQ(mark, "sS");
    CHECK(t1ck - t0 >= 10 && t1ck - t0 <= 11, "controller slept ~10 ticks");

    printf("task: mrtos_now is monotonic\n");
    t0 = mrtos_now();
    mrtos_sleep(3);
    t1ck = mrtos_now();
    CHECK(t1ck >= t0 + 3, "now() advanced by at least the sleep");

    printf("task: sleep(0) degrades to yield (no block)\n");
    t0 = mrtos_now();
    mrtos_sleep(0);
    CHECK(mrtos_now() == t0, "no tick consumed");

    printf("task: yield round-trips through equal-priority peers\n");
    mark = test_evn;
    yields_seen = 0;
    mrtos_task_create(&t3, "yld", w_yielder, NULL, MRTOS_PRIO_MAX,
                      s3, TEST_STK_WORDS);
    for (int i = 0; i < 3; i++) {
        ev('c');
        mrtos_yield();
    }
    mrtos_yield();                 /* let the peer leave its final yield */
    CHECK_SEQ(mark, "cycycy");     /* strict alternation at equal prio */
    CHECK(yields_seen == 3, "peer completed all its slices");

    printf("task: MRTOS_MS converts using the configured tick rate\n");
    CHECK(MRTOS_MS(1000) == MRTOS_CFG_TICK_HZ, "MS(1000) == TICK_HZ");
    CHECK(MRTOS_MS(0) == 0, "MS(0) == 0");
}
