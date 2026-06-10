/*
 * mRTOS port: POSIX/ucontext host port.
 *
 * Purpose: run the *unmodified* kernel on a development machine for unit
 * testing and as proof of the port contract. Cooperative simulation:
 * context switches happen at kernel decision points (block/yield/wake) and
 * the tick advances whenever the idle task runs. Asynchronous preemption
 * (a tick firing mid-instruction) is NOT simulated; that path is exercised
 * only on real hardware.
 */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>
#include <stddef.h>
#include <ucontext.h>

typedef unsigned long port_stack_t;

#define PORT_TCB_EXT ucontext_t uc;

uint16_t      port_irq_save(void);
void          port_irq_restore(uint16_t key);
void          port_yield(void);
port_stack_t *port_stack_init(port_stack_t *base, size_t words,
                              void (*shell)(void *), void *arg);
void          port_start(void) __attribute__((noreturn));
void          port_idle(void);

#endif /* PORT_H */
