/* MSP430FR5994 application configuration */
#ifndef MRTOS_CONFIG_H
#define MRTOS_CONFIG_H

#define MRTOS_CFG_TICK_HZ          1000u
#define MRTOS_CFG_SLICE_TICKS      10u
#define MRTOS_CFG_IDLE_STACK_WORDS 64u
#define MRTOS_CFG_STACK_CHECK      1

#define PORT_CFG_SMCLK_HZ          8000000UL
#define PORT_CFG_ISR_STACK_WORDS   96u

#endif
