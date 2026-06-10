/* Simulator test configuration (GNU msp430 sim, real 16-bit ISA). */
#ifndef MRTOS_CONFIG_H
#define MRTOS_CONFIG_H

#define MRTOS_CFG_TICK_HZ          1000u
#define MRTOS_CFG_SLICE_TICKS      4u
#define MRTOS_CFG_IDLE_STACK_WORDS 128u
/* Off: the sim port allocates real stacks from a fixed pool, so the
 * guard words in the app-supplied arrays would never be exercised. */
#define MRTOS_CFG_STACK_CHECK      0

#endif
