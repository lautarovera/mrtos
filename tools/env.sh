# mRTOS toolchain environment.
#
#   source tools/env.sh          # one-off
#   echo "source $(pwd)/tools/env.sh" >> ~/.bashrc   # permanent
#
# Auto-detects the newest msp430-gcc under ~/toolchains (the layout
# tools/setup_debug_tools.sh and doc/VALIDATION.md create). Explicitly
# exported values are respected. NOTE: the Makefile and the tools/*.sh
# scripts perform this same detection on their own, so sourcing this
# file is only needed for the CMake/uv workflows or custom commands.

if [ -z "${MSP430_GCC_DIR:-}" ]; then
    for _d in "$HOME"/toolchains/msp430-gcc-*_linux64; do
        [ -d "$_d" ] && MSP430_GCC_DIR=$_d
    done
    unset _d
fi
if [ -z "${MSP430_SUPPORT_DIR:-}" ] \
   && [ -d "$HOME/toolchains/msp430-gcc-support-files/include" ]; then
    MSP430_SUPPORT_DIR=$HOME/toolchains/msp430-gcc-support-files/include
fi

if [ -n "${MSP430_GCC_DIR:-}" ]; then
    export MSP430_GCC_DIR
    echo "MSP430_GCC_DIR=$MSP430_GCC_DIR"
else
    echo "warning: no msp430-gcc found under ~/toolchains (see doc/VALIDATION.md)" >&2
fi
if [ -n "${MSP430_SUPPORT_DIR:-}" ]; then
    export MSP430_SUPPORT_DIR
    echo "MSP430_SUPPORT_DIR=$MSP430_SUPPORT_DIR"
fi
