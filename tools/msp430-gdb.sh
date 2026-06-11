#!/bin/sh
# msp430-elf-gdb wrapper.
#
# TI's gdb links against libncursesw.so.5, which modern distros no
# longer ship; tools/setup_debug_tools.sh drops a copy into
# $MSP430_GCC_DIR/extlib. This wrapper injects it and is what both the
# Makefile debug rules and .vscode/launch.json invoke.
set -eu

: "${MSP430_GCC_DIR:?set MSP430_GCC_DIR to your msp430-gcc install}"

LD_LIBRARY_PATH="$MSP430_GCC_DIR/extlib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$MSP430_GCC_DIR/bin/msp430-elf-gdb" "$@"
