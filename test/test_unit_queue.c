/* Unit tests: message queue. */
#include "test_common.h"

static mrtos_tcb_t  t_a, t_b;
static port_stack_t s_a[TEST_STK_WORDS], s_b[TEST_STK_WORDS];

static mrtos_queue_t q;
static uint16_t      store16[4];

typedef struct {
    uint8_t  tag;
    uint16_t seq;
    uint32_t payload;
} item_t;
static item_t store_item[2];

static int send_rc = 99;
static void w_send_timed(void *arg)
{
    (void)arg;
    uint16_t v = 0xDEAD;
    ev('t');
    send_rc = mrtos_queue_send(&q, &v, 4);
    ev('T');
}

static uint16_t drained[8];
static int      ndrained;
static void w_drain(void *arg)
{
    (void)arg;
    while (mrtos_queue_recv(&q, &drained[ndrained], 2) == MRTOS_OK)
        ndrained++;
    ev('d');
}

static void test_main(void)
{
    int mark;
    uint16_t v;
    uint32_t t0, t1;

    printf("queue: poll on empty/full fails fast\n");
    mrtos_queue_init(&q, store16, sizeof(uint16_t), 4);
    CHECK(mrtos_queue_recv(&q, &v, 0) == MRTOS_ERR_TIMEOUT,
          "recv poll on empty");
    for (uint16_t i = 0; i < 4; i++)
        CHECK(mrtos_queue_send(&q, &i, 0) == MRTOS_OK, "send fills a slot");
    v = 99;
    CHECK(mrtos_queue_send(&q, &v, 0) == MRTOS_ERR_TIMEOUT,
          "send poll on full");
    CHECK(q.count == 4, "count unchanged by failed send");

    printf("queue: FIFO order preserved across wraparound\n");
    CHECK(mrtos_queue_recv(&q, &v, 0) == MRTOS_OK && v == 0, "recv 0");
    CHECK(mrtos_queue_recv(&q, &v, 0) == MRTOS_OK && v == 1, "recv 1");
    v = 4; mrtos_queue_send(&q, &v, 0);    /* head wraps to slot 0/1 */
    v = 5; mrtos_queue_send(&q, &v, 0);
    uint16_t expect[] = { 2, 3, 4, 5 };
    int order_ok = 1;
    for (int i = 0; i < 4; i++) {
        mrtos_queue_recv(&q, &v, 0);
        if (v != expect[i])
            order_ok = 0;
    }
    CHECK(order_ok, "2,3,4,5 received in order through the wrap");

    printf("queue: copy semantics (sender's buffer can be reused)\n");
    v = 1234;
    mrtos_queue_send(&q, &v, 0);
    v = 9999;                              /* clobber the source */
    mrtos_queue_recv(&q, &v, 0);
    CHECK(v == 1234, "received the value at send time");

    printf("queue: struct items copied whole\n");
    mrtos_queue_init(&q, store_item, sizeof(item_t), 2);
    item_t in = { 0xAB, 0x1234, 0xCAFEF00Du }, out = { 0, 0, 0 };
    mrtos_queue_send(&q, &in, 0);
    mrtos_queue_recv(&q, &out, 0);
    CHECK(out.tag == 0xAB && out.seq == 0x1234 && out.payload == 0xCAFEF00Du,
          "all fields intact");

    printf("queue: recv timeout on empty\n");
    mrtos_queue_init(&q, store16, sizeof(uint16_t), 4);
    t0 = mrtos_now();
    CHECK(mrtos_queue_recv(&q, &v, 3) == MRTOS_ERR_TIMEOUT, "rc TIMEOUT");
    t1 = mrtos_now();
    CHECK(t1 - t0 >= 3 && t1 - t0 <= 4, "blocked ~3 ticks");

    printf("queue: send timeout on full leaves contents intact\n");
    for (uint16_t i = 10; i < 14; i++)
        mrtos_queue_send(&q, &i, 0);       /* fill */
    mark = test_evn;
    send_rc = 99;
    mrtos_task_create(&t_a, "snd", w_send_timed, NULL, 3,
                      s_a, TEST_STK_WORDS);
    mrtos_sleep(6);                        /* sender blocks, times out at +4 */
    CHECK(send_rc == MRTOS_ERR_TIMEOUT, "send rc TIMEOUT");
    CHECK(q.count == 4 && q.tx_waiters == NULL,
          "queue still full, wait list clean");
    CHECK_SEQ(mark, "tT");
    int ok = 1;
    for (uint16_t i = 10; i < 14; i++) {
        mrtos_queue_recv(&q, &v, 0);
        if (v != i)
            ok = 0;
    }
    CHECK(ok, "original items 10..13 unharmed");

    printf("queue: blocked sender completes when space frees\n");
    for (uint16_t i = 0; i < 4; i++)
        mrtos_queue_send(&q, &i, 0);       /* fill again */
    ndrained = 0;
    mark = test_evn;
    mrtos_task_create(&t_a, "snd", w_send_timed, NULL, 3,
                      s_a, TEST_STK_WORDS);
    mrtos_sleep(1);                        /* sender blocks on full queue */
    mrtos_task_create(&t_b, "drn", w_drain, NULL, 2,
                      s_b, TEST_STK_WORDS);
    mrtos_sleep(10);                       /* drain frees a slot; sender runs */
    CHECK(send_rc == MRTOS_OK, "blocked send completed");
    CHECK(ndrained == 5 && drained[4] == 0xDEAD,
          "drainer saw 4 originals + the late item");
    CHECK_SEQ(mark, "tTd");
}
