# mRTOS - MSP430FR5994 target build.
# Requires TI's msp430-gcc toolchain + device support files:
#   https://www.ti.com/tool/MSP430-GCC-OPENSOURCE
# Point GCC_DIR / SUPPORT_DIR at your installation.

GCC_DIR     ?= /opt/ti/msp430-gcc
SUPPORT_DIR ?= $(GCC_DIR)/include

CC      = $(GCC_DIR)/bin/msp430-elf-gcc
MCU     = msp430fr5994

CFLAGS  = -mmcu=$(MCU) -mhwmult=f5series -Os -g -Wall -Wextra \
          -ffunction-sections -fdata-sections \
          -I$(SUPPORT_DIR) -Ikernel -Iport/msp430fr59xx -Iapp
# -mmcu only once on the combined compile+link line, or the device
# linker script is included twice.
LDFLAGS = -L$(SUPPORT_DIR) -Wl,--gc-sections

SRCS = kernel/mrtos.c port/msp430fr59xx/port.c app/main_fr5994.c

mrtos_fr5994.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	$(GCC_DIR)/bin/msp430-elf-size $@

clean:
	rm -f mrtos_fr5994.elf
