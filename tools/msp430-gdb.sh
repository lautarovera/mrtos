#!/bin/sh
# msp430-elf-gdb wrapper.
#
# TI's gdb links against libncursesw.so.5, which modern distros no
# longer ship; tools/setup_debug_tools.sh drops a copy into
# $MSP430_GCC_DIR/extlib. This wrapper injects it and is what both the
# Makefile debug rules and .vscode/launch.json invoke.
set -eu

# Auto-detect the newest install under ~/toolchains when unset.
if [ -z "${MSP430_GCC_DIR:-}" ]; then
    for _d in "$HOME"/toolchains/msp430-gcc-*_linux64; do
        [ -d "$_d" ] && MSP430_GCC_DIR=$_d
    done
fi
: "${MSP430_GCC_DIR:?no msp430-gcc found - see doc/VALIDATION.md}"

LD_LIBRARY_PATH="$MSP430_GCC_DIR/extlib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$MSP430_GCC_DIR/bin/msp430-elf-gdb" "$@"
