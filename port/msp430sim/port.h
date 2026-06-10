/*
 * mRTOS port: GNU MSP430 simulator (msp430-elf-run, ships with TI's
 * msp430-gcc).
 *
 * Purpose: run the kernel test suite on the real MSP430 ISA - 16-bit
 * ints, 16-bit pointers, msp430-gcc codegen, and a genuine CPUX
 * PUSHM.A/POPM.A context switch - without hardware. The simulator has
 * no peripherals or interrupt injection, so like the POSIX port this
 * one is cooperative: ticks advance in port_idle(). Asynchronous
 * preemption remains target-only (see doc/VALIDATION.md).
 *
 * Build: -mcpu=msp430x -msim (small memory model), run: msp430-elf-run.
 */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>
#include <stddef.h>

typedef uint16_t port_stack_t;

/* Test-suite stack sizing: no ucontext here, real frames are tiny. */
#define PORT_TEST_STACK_WORDS 256u

uint16_t      port_irq_save(void);
void          port_irq_restore(uint16_t key);
void          port_yield(void);
port_stack_t *port_stack_init(port_stack_t *base, size_t words,
                              void (*shell)(void *), void *arg);
void          port_start(void);
void          port_idle(void);

#endif /* PORT_H */
