# mRTOS - MSP430FR5994 target build.
# Requires TI's msp430-gcc toolchain + device support files:
#   https://www.ti.com/tool/MSP430-GCC-OPENSOURCE
# Point GCC_DIR / SUPPORT_DIR at your installation.

# Toolchain resolution, in priority order:
#   1. GCC_DIR/SUPPORT_DIR given on the command line
#   2. MSP430_GCC_DIR/MSP430_SUPPORT_DIR from the environment
#   3. newest install under ~/toolchains (the doc/VALIDATION.md layout)
#   4. /opt/ti/msp430-gcc
ifndef MSP430_GCC_DIR
MSP430_GCC_DIR := $(lastword $(sort $(wildcard $(HOME)/toolchains/msp430-gcc-*_linux64)))
endif
ifneq ($(MSP430_GCC_DIR),)
GCC_DIR     ?= $(MSP430_GCC_DIR)
endif
GCC_DIR     ?= /opt/ti/msp430-gcc
ifndef MSP430_SUPPORT_DIR
MSP430_SUPPORT_DIR := $(wildcard $(HOME)/toolchains/msp430-gcc-support-files/include)
endif
ifneq ($(MSP430_SUPPORT_DIR),)
SUPPORT_DIR ?= $(MSP430_SUPPORT_DIR)
endif
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

# ------------------------------------------------------------------ #
# On-target run & debug (LaunchPad MSP-EXP430FR5994, onboard eZ-FET). #
# One-time setup: tools/setup_debug_tools.sh; full guide: doc/DEBUG.md #
# ------------------------------------------------------------------ #
MSPDEBUG_DIR ?= $(HOME)/toolchains/mspdebug
MSPDEBUG     ?= LD_LIBRARY_PATH=$(MSPDEBUG_DIR)/lib $(MSPDEBUG_DIR)/mspdebug
MSPDEBUG_DRV ?= ezfet
GDB          ?= MSP430_GCC_DIR=$(GCC_DIR) tools/msp430-gdb.sh
GDB_PORT     ?= 2000

# Program the board and leave it halted under the debugger's control.
flash: mrtos_fr5994.elf
	$(MSPDEBUG) $(MSPDEBUG_DRV) "prog mrtos_fr5994.elf"

# Program, reset and let it run free (mspdebug releases on exit).
run: mrtos_fr5994.elf
	$(MSPDEBUG) $(MSPDEBUG_DRV) "prog mrtos_fr5994.elf" "reset"

# GDB server on :$(GDB_PORT). Programs first, then serves until ^C
# (gdb_loop keeps it alive across client reconnects).
gdbserver: mrtos_fr5994.elf
	$(MSPDEBUG) $(MSPDEBUG_DRV) "prog mrtos_fr5994.elf" \
	    "opt gdb_loop true" "gdb $(GDB_PORT)"

# CLI debug session against a running 'make gdbserver'.
gdb: mrtos_fr5994.elf
	$(GDB) mrtos_fr5994.elf -ex "target remote :$(GDB_PORT)"

# GUI debug: open VSCode here; F5 runs the preconfigured launch
# (.vscode/launch.json starts its own gdbserver task).
debug:
	code .
	@echo "VSCode launched - press F5 (Debug on LaunchPad)."

.PHONY: clean flash run gdbserver gdb debug
