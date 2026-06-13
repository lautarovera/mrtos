/* Unit tests: tickless support (mrtos_next_deadline / mrtos_tick_advance)
 * and power-mode locks. Pure kernel logic — runs on host and simulator.
 *
 * The controller is the highest-priority task, so it stays current while
 * it calls mrtos_tick_advance(): woken low-priority sleepers go READY but
 * do not run, letting us inspect their state. Assertions use deltas
 * (next_deadline, now()) rather than absolute tick values, so the 1-tick
 * cost of parking the sleepers does not matter.
 */
#include "test_common.h"

#define NSLP 6
static mrtos_tcb_t  slp[NSLP];
static port_stack_t sslp[NSLP][TEST_STK_WORDS];

static void w_sleep(void *arg)
{
    mrtos_sleep((uint16_t)(uintptr_t)arg);
}

static void mk_sleeper(int i, uint16_t ticks)
{
    mrtos_task_create(&slp[i], "slp", w_sleep, (void *)(uintptr_t)ticks,
                      1, sslp[i], TEST_STK_WORDS);
}

static void test_main(void)
{
    uint32_t t0;
    uint16_t d;

    printf("tickless: next_deadline is 0 with an empty delay list\n");
    CHECK(mrtos_next_deadline() == 0, "no sleepers -> 0");

    printf("tickless: advance wakes only the head sleeper\n");
    mk_sleeper(0, 10);
    mk_sleeper(1, 20);
    mk_sleeper(2, 30);
    mrtos_sleep(1);                    /* let them park in the delay list */
    d = mrtos_next_deadline();
    CHECK(d > 0, "head deadline is positive");
    CHECK(slp[0].state == MRTOS_TASK_BLOCKED &&
          slp[1].state == MRTOS_TASK_BLOCKED &&
          slp[2].state == MRTOS_TASK_BLOCKED, "all three blocked");
    t0 = mrtos_now();
    mrtos_tick_advance(d);
    CHECK(mrtos_now() - t0 == d, "tick_count advanced by exactly d");
    CHECK(slp[0].state == MRTOS_TASK_READY, "earliest sleeper woke");
    CHECK(slp[1].state == MRTOS_TASK_BLOCKED &&
          slp[2].state == MRTOS_TASK_BLOCKED, "later sleepers still blocked");
    CHECK(mrtos_next_deadline() > 0, "next deadline now the 2nd sleeper");

    printf("tickless: a large advance crosses every remaining deadline\n");
    t0 = mrtos_now();
    mrtos_tick_advance(1000);
    CHECK(mrtos_now() - t0 == 1000, "tick_count advanced by exactly 1000");
    CHECK(slp[1].state == MRTOS_TASK_READY &&
          slp[2].state == MRTOS_TASK_READY, "both remaining woke");
    CHECK(mrtos_next_deadline() == 0, "delay list drained");
    mrtos_sleep(1);                    /* let the woken sleepers retire */

    printf("tickless: coincident deadlines wake together\n");
    mk_sleeper(3, 15);
    mk_sleeper(4, 15);                 /* same wake tick as slp[3] */
    mrtos_sleep(1);
    d = mrtos_next_deadline();
    mrtos_tick_advance(d);
    CHECK(slp[3].state == MRTOS_TASK_READY &&
          slp[4].state == MRTOS_TASK_READY, "both same-tick sleepers woke");
    CHECK(mrtos_next_deadline() == 0, "list empty after coincident wake");
    mrtos_sleep(1);

    printf("power locks: default cap, most-restrictive wins, balance\n");
    CHECK(mrtos_pm_max_lpm() == MRTOS_CFG_LPM_DEFAULT, "default cap = LPM3");
    mrtos_pm_lock(MRTOS_LPM0);
    CHECK(mrtos_pm_max_lpm() == MRTOS_LPM0, "LPM0 lock caps at LPM0");
    mrtos_pm_lock(MRTOS_LPM1);
    CHECK(mrtos_pm_max_lpm() == MRTOS_LPM0,
          "a deeper lock does not loosen the cap");
    mrtos_pm_unlock(MRTOS_LPM0);
    CHECK(mrtos_pm_max_lpm() == MRTOS_LPM1, "LPM1 cap after LPM0 released");
    mrtos_pm_unlock(MRTOS_LPM1);
    CHECK(mrtos_pm_max_lpm() == MRTOS_CFG_LPM_DEFAULT,
          "back to default when all released");

    printf("power locks: underflow and out-of-range are ignored\n");
    mrtos_pm_unlock(MRTOS_LPM0);       /* count already 0 */
    CHECK(mrtos_pm_max_lpm() == MRTOS_CFG_LPM_DEFAULT,
          "unbalanced unlock is a no-op");
    mrtos_pm_lock(99);                 /* >= MRTOS_LPM_LEVELS */
    mrtos_pm_unlock(99);
    CHECK(mrtos_pm_max_lpm() == MRTOS_CFG_LPM_DEFAULT,
          "out-of-range level ignored");
}
