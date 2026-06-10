/* mRTOS port: GNU MSP430 simulator. See port.h for the design notes. */
#include <stdio.h>
#include <stdlib.h>
#include "mrtos.h"

static void (*g_shell)(void *);

/* Cooperative port: no interrupt sources exist, so the critical
 * section is a no-op, exactly like the POSIX port. */
uint16_t port_irq_save(void)          { return 0; }
void     port_irq_restore(uint16_t k) { (void)k; }

/*
 * Stack switch, the same register protocol as the fr59xx port's ISRs
 * but call-based: the caller's R4-R15 are saved with PUSHM.A (20-bit,
 * 2 words per register), SP is parked in the old TCB and reloaded from
 * the new one, POPM.A restores, and RET resumes the new context.
 * ABI: R12 = &old->sp, R13 = new sp. SP is R1.
 */
__attribute__((naked))
static void ctx_swap(port_stack_t **save_sp, port_stack_t *new_sp)
{
    (void)save_sp; (void)new_sp;
    __asm__ volatile(
        "pushm.a #12, r15      \n\t"
        "mov.w   r1,  @r12     \n\t"
        "mov.w   r13, r1       \n\t"
        "popm.a  #12, r15      \n\t"
        "ret                   \n\t");
}

void port_yield(void)
{
    mrtos_tcb_t *prev = mrtos_cur;
    mrtos_sched_pick();
    if (mrtos_cur != prev)
        ctx_swap(&prev->sp, mrtos_cur->sp);
}

/* First scheduling of a task lands here via the fabricated frame. */
static void tramp(void)
{
    g_shell(mrtos_cur);
}

/*
 * Task stacks come from a fixed pool at 0xA000-0xEFFF instead of the
 * caller's arrays: the simulator aborts ("STACK OVERFLOW") whenever SP
 * drops below the heap break, and the break starts at _end - i.e. any
 * static array in .bss is illegal stack territory. The pool sits above
 * the heap and below the startup stack (__stack = 0xFFC0), which is
 * abandoned once port_start() switches to the first task.
 */
#define POOL_BASE ((port_stack_t *)0xA000)
#define POOL_END  ((port_stack_t *)0xF000)
extern char _end[];                  /* heap break starts here */

port_stack_t *port_stack_init(port_stack_t *base, size_t words,
                              void (*shell)(void *), void *arg)
{
    static port_stack_t *pool_next = POOL_BASE;
    (void)base;
    (void)arg;                       /* tramp reads mrtos_cur instead */
    if ((char *)POOL_BASE < _end + 1024 ||
        pool_next + words > POOL_END) {
        fprintf(stderr, "[sim] stack pool exhausted/overlaps heap\n");
        exit(2);
    }
    g_shell = shell;
    port_stack_t *sp = pool_next + words;
    pool_next += words;
    *--sp = (port_stack_t)tramp;     /* RET target (16-bit, small model) */
    sp -= 24;                        /* PUSHM.A #12 save area, content x */
    return sp;
}

void port_start(void)
{
    static port_stack_t *discard;
    ctx_swap(&discard, mrtos_cur->sp);
    fprintf(stderr, "[sim] start returned\n");
    exit(2);
}

void port_idle(void)
{
    static unsigned long safety;
    if (++safety > 200000UL) {
        fprintf(stderr, "[sim] tick limit reached: deadlock?\n");
        exit(2);
    }
    mrtos_tcb_t *self = mrtos_cur;
    mrtos_tick();
    if (mrtos_cur != self)
        ctx_swap(&self->sp, mrtos_cur->sp);
}
