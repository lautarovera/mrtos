#!/bin/sh
# One-time toolchain download: TI msp430-gcc + device support files,
# from TI's public mirror (no registration), into ~/toolchains/ - the
# layout every tool in this repo auto-detects. Idempotent.
#
# Full setup order:
#   1. tools/setup_toolchain.sh      <- this script (compiler, no root)
#   2. tools/setup_debug_tools.sh    (mspdebug, gdb libs, energytrace)
#   3. doc/DEBUG.md one-time root steps (TI debug stack, udev, usbipd)
#   4. tools/check_board.sh          (verifies the whole chain)
set -eu

GCC_VER=9.3.1.11
GCC_REL=9_3_1_2
SF_VER=1.212
BASE="https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/$GCC_REL/export"

mkdir -p "$HOME/toolchains"
cd "$HOME/toolchains"

if [ -d "msp430-gcc-${GCC_VER}_linux64" ]; then
    echo "msp430-gcc $GCC_VER already present - skipping"
else
    echo "== downloading msp430-gcc $GCC_VER (~64 MB) =="
    curl -O "$BASE/msp430-gcc-${GCC_VER}_linux64.tar.bz2"
    tar xjf "msp430-gcc-${GCC_VER}_linux64.tar.bz2"
fi

if [ -d msp430-gcc-support-files ]; then
    echo "support files already present - skipping"
else
    echo "== downloading device support files $SF_VER (~22 MB) =="
    curl -O "$BASE/msp430-gcc-support-files-${SF_VER}.zip"
    python3 -m zipfile -e "msp430-gcc-support-files-${SF_VER}.zip" .
fi

"./msp430-gcc-${GCC_VER}_linux64/bin/msp430-elf-gcc" --version | head -1
echo "OK - tools will auto-detect this location. Next: tools/setup_debug_tools.sh"
