/*
 * mRTOS test framework (POSIX port).
 *
 * Pattern: a priority-7 controller task (the highest level) orchestrates
 * each scenario; worker tasks log single-character events into a shared
 * buffer which the controller then asserts. The process exits 0 on full
 * pass, 1 on any failure, so the suite plugs directly into CTest.
 *
 * Host-port constraint to keep in mind when writing tests: ticks advance
 * only while the idle task runs (port_idle), i.e. when every other task
 * is blocked. Busy-wait loops therefore never time-slice and will trip
 * the deadlock watchdog. Always make tasks block (sleep/take/recv).
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mrtos.h"

#define TEST_STK_WORDS 8192u  /* 64 KiB per ucontext stack */

static char test_evbuf[1024];
static int  test_evn;
static int  test_failures;
static int  test_checks;

static inline void ev(char c)
{
    if (test_evn < (int)sizeof(test_evbuf) - 1)
        test_evbuf[test_evn++] = c;
}

/* Events recorded since 'mark', as a NUL-terminated string. */
static inline const char *ev_since(int mark)
{
    test_evbuf[test_evn] = '\0';
    return &test_evbuf[mark];
}

#define CHECK(cond, msg) do {                                       \
    test_checks++;                                                  \
    if (cond) { printf("  ok   - %s\n", msg); }                     \
    else      { printf("  FAIL - %s (%s:%d)\n", msg,                \
                       __FILE__, __LINE__); test_failures++; }      \
} while (0)

#define CHECK_SEQ(mark, expect) do {                                \
    const char *got_ = ev_since(mark);                              \
    test_checks++;                                                  \
    if (strcmp(got_, expect) == 0) {                                \
        printf("  ok   - sequence \"%s\"\n", expect);               \
    } else {                                                        \
        printf("  FAIL - sequence: expected \"%s\" got \"%s\" "     \
               "(%s:%d)\n", expect, got_, __FILE__, __LINE__);      \
        test_failures++;                                            \
    }                                                               \
} while (0)

/* Each test file implements this; it runs as the prio-7 controller. */
static void test_main(void);

static void test_controller(void *arg)
{
    (void)arg;
    test_main();
    test_evbuf[test_evn] = '\0';
    printf("\nevents: %s\n", test_evbuf);
    if (test_failures == 0) {
        printf("PASS: %d checks\n", test_checks);
        exit(0);
    }
    printf("FAIL: %d of %d checks failed\n", test_failures, test_checks);
    exit(1);
}

static mrtos_tcb_t  test_ctl_tcb;
static port_stack_t test_ctl_stk[TEST_STK_WORDS];

int main(void)
{
    mrtos_init();
    mrtos_task_create(&test_ctl_tcb, "ctl", test_controller, NULL,
                      MRTOS_PRIO_MAX, test_ctl_stk, TEST_STK_WORDS);
    mrtos_start();
    return 0; /* not reached */
}

#endif /* TEST_COMMON_H */
