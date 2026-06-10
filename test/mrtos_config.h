/* Host test configuration */
#ifndef MRTOS_CONFIG_H
#define MRTOS_CONFIG_H

#define MRTOS_CFG_TICK_HZ          1000u
#define MRTOS_CFG_SLICE_TICKS      4u
#define MRTOS_CFG_IDLE_STACK_WORDS 8192u   /* 64 KiB: ucontext needs room  */
#define MRTOS_CFG_STACK_CHECK      0       /* ucontext owns the stack base */

#endif
