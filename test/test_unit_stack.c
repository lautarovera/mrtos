/* Unit tests: stack painting and high-water marking.
 *
 * Runs on both host (POSIX/ucontext) and the MSP430 simulator. On the
 * simulator the numbers are genuine MSP430 frame sizes (the sim port
 * retargets stack_base at the pool stack it actually uses), so this
 * test doubles as an empirical stack-budget probe for chapter 4 of the
 * manual.
 */
#include "test_common.h"

static mrtos_tcb_t  t_shallow, t_deep;
static port_stack_t s_shallow[TEST_STK_WORDS], s_deep[TEST_STK_WORDS];

#define DEEP_WORDS 64u

static void w_shallow(void *arg)
{
    (void)arg;
    ev('s');
}

/* Forces a frame at least DEEP_WORDS deep; volatile defeats both
 * optimization of the writes and conversion to a static buffer. */
static void touch_deep(void)
{
    volatile port_stack_t buf[DEEP_WORDS];
    for (size_t i = 0; i < DEEP_WORDS; i++)
        buf[i] = (port_stack_t)i;          /* anything but the paint */
    ev((char)('d' + (buf[DEEP_WORDS - 1] & 0)));
}

static void w_deep(void *arg)
{
    (void)arg;
    touch_deep();
}

static void test_main(void)
{
    printf("stack: fresh TCB reports a painted (unused) stack\n");
    mrtos_task_create(&t_shallow, "sh", w_shallow, NULL, 1,
                      s_shallow, TEST_STK_WORDS);
    size_t fresh = mrtos_stack_unused(&t_shallow);
    /* Only the port's fabricated start frame may be consumed so far. */
    CHECK(fresh > 0 && fresh <= TEST_STK_WORDS,
          "unused in (0, words] before first run");

    printf("stack: usage appears after the task runs\n");
    mrtos_sleep(1);                        /* shallow runs and retires */
    CHECK(t_shallow.state == MRTOS_TASK_SUSPENDED, "shallow retired");
    size_t un_shallow = mrtos_stack_unused(&t_shallow);
    CHECK(un_shallow < fresh, "running consumed stack words");
    CHECK(un_shallow > 0, "no overflow: paint remains at the base");

    printf("stack: deeper call chain leaves a lower high-water mark\n");
    mrtos_task_create(&t_deep, "dp", w_deep, NULL, 1,
                      s_deep, TEST_STK_WORDS);
    mrtos_sleep(1);
    CHECK(t_deep.state == MRTOS_TASK_SUSPENDED, "deep retired");
    size_t un_deep = mrtos_stack_unused(&t_deep);
    CHECK(un_deep > 0, "no overflow in the deep task");
    /* Absolute, not relative to shallow: on register-saving ports the
     * context save area (24 words on the sim port) puts a floor under
     * every task's high-water mark, so shallow's usage is dominated by
     * it and "deep - shallow >= DEEP_WORDS" does not hold in general.
     * (Found on the simulator; the host hides it because ucontext
     * saves registers into the TCB, not onto the stack.) */
    size_t used_deep = TEST_STK_WORDS - un_deep;
    CHECK(used_deep >= DEEP_WORDS, "deep task used at least its buffer");
    CHECK(un_deep < un_shallow, "deep mark strictly below shallow's");

    printf("stack: high-water marks (words of %u): shallow=%u deep=%u\n",
           (unsigned)TEST_STK_WORDS,
           (unsigned)un_shallow, (unsigned)un_deep);
    CHECK(mrtos_stack_unused(&t_deep) == un_deep,
          "scan is repeatable (read-only)");
}
