#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include "mrtos.h"

static void (*g_shell)(void *);

uint16_t port_irq_save(void)          { return 0; }
void     port_irq_restore(uint16_t k) { (void)k; }

void port_yield(void)
{
    mrtos_tcb_t *prev = mrtos_cur;
    mrtos_sched_pick();
    if (mrtos_cur != prev)
        swapcontext(&prev->uc, &mrtos_cur->uc);
}

static void tramp(void)
{
    g_shell(mrtos_cur);
}

port_stack_t *port_stack_init(port_stack_t *base, size_t words,
                              void (*shell)(void *), void *arg)
{
    mrtos_tcb_t *t = (mrtos_tcb_t *)arg;
    g_shell = shell;
    getcontext(&t->uc);
    t->uc.uc_stack.ss_sp   = base;
    t->uc.uc_stack.ss_size = words * sizeof(port_stack_t);
    t->uc.uc_link          = NULL;
    makecontext(&t->uc, tramp, 0);
    return base;
}

void port_start(void)
{
    setcontext(&mrtos_cur->uc);
    fprintf(stderr, "[host] setcontext failed\n");
    exit(1);
}

void port_idle(void)
{
    static unsigned long safety;
    if (++safety > 200000UL) {
        fprintf(stderr, "[host] tick limit reached: deadlock?\n");
        exit(2);
    }
    mrtos_tcb_t *self = mrtos_cur;
    mrtos_tick();
    if (mrtos_cur != self)
        swapcontext(&self->uc, &mrtos_cur->uc);
}
