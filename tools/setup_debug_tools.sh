#!/bin/sh
# One-time setup of the on-target debug tools, no root required.
#
#   1. mspdebug (current git: the 'ezfet' driver for the LaunchPad's
#      onboard eZ-FET appeared after the version distros package),
#      built against extracted .deb libraries -> ~/toolchains/mspdebug
#   2. libncursesw5/libtinfo5 for TI's msp430-elf-gdb ->
#      $MSP430_GCC_DIR/extlib (used by tools/msp430-gdb.sh)
#
# Root IS needed once for USB permissions; this script prints the udev
# rule but does not install it. See doc/DEBUG.md (incl. WSL2/usbipd).
set -eu

# Auto-detect the newest install under ~/toolchains when unset.
if [ -z "${MSP430_GCC_DIR:-}" ]; then
    for _d in "$HOME"/toolchains/msp430-gcc-*_linux64; do
        [ -d "$_d" ] && MSP430_GCC_DIR=$_d
    done
fi
: "${MSP430_GCC_DIR:?no msp430-gcc found - see doc/VALIDATION.md}"
TOOLDIR="${MSPDEBUG_DIR:-$HOME/toolchains/mspdebug}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ -x "$TOOLDIR/mspdebug" ]; then
    echo "mspdebug already present at $TOOLDIR - skipping build"
else
    echo "== building mspdebug (ezfet driver) =="
    cd "$WORK"
    apt-get download libusb-dev libusb-0.1-4
    for deb in ./*.deb; do dpkg -x "$deb" sysroot; done
    git clone --depth 1 https://github.com/dlbeer/mspdebug.git
    make -C mspdebug -j"$(nproc)" WITHOUT_READLINE=1 \
         CFLAGS="-O2 -I$WORK/sysroot/usr/include" \
         LDFLAGS="-L$WORK/sysroot/usr/lib/x86_64-linux-gnu"
    mkdir -p "$TOOLDIR/lib"
    cp mspdebug/mspdebug "$TOOLDIR/"
    cp -P sysroot/lib/x86_64-linux-gnu/libusb-0.1.so.4* "$TOOLDIR/lib/"
fi

if [ -e "$MSP430_GCC_DIR/extlib/libncursesw.so.5" ]; then
    echo "gdb support libs already present - skipping"
else
    echo "== fetching ncurses5 for msp430-elf-gdb =="
    cd "$WORK"
    base=http://archive.ubuntu.com/ubuntu/pool/universe/n/ncurses
    curl -sO "$base/libncursesw5_6.3-2ubuntu0.1_amd64.deb"
    curl -sO "$base/libtinfo5_6.3-2ubuntu0.1_amd64.deb"
    dpkg -x libncursesw5_*.deb gdblibs
    dpkg -x libtinfo5_*.deb gdblibs
    mkdir -p "$MSP430_GCC_DIR/extlib"
    cp -P gdblibs/lib/x86_64-linux-gnu/* "$MSP430_GCC_DIR/extlib/"
fi

ET_DIR="$HOME/toolchains/energytrace"
if [ -x "$ET_DIR/energytrace" ]; then
    echo "energytrace-util already present - skipping"
else
    echo "== building energytrace-util (EnergyTrace CLI, needs TI stack) =="
    cd "$WORK"
    git clone --depth 1 https://github.com/carrotIndustries/energytrace-util.git
    mkdir -p energytrace-util/inc
    base=https://raw.githubusercontent.com/osresearch/MSPDebugStack/master/DLL430_v3/include
    for h in MSP430.h MSP430_EnergyTrace.h MSP430_Debug.h MSP430_FET.h DLL430_SYMBOL.h; do
        curl -s -o "energytrace-util/inc/$h" "$base/$h"
    done
    TI_DLL=""
    for d in /opt/ti/msp430-gcc-full/bin "$HOME/ti/msp430-gcc-full/bin"; do
        [ -e "$d/libmsp430.so" ] && TI_DLL=$d
    done
    if [ -n "$TI_DLL" ]; then
        gcc -Ienergytrace-util/inc -o energytrace-util/energytrace \
            energytrace-util/energytrace.c -L"$TI_DLL" -lmsp430
        mkdir -p "$ET_DIR"
        cp energytrace-util/energytrace "$ET_DIR/"
    else
        echo "  (skipped: TI debug stack not installed - see doc/DEBUG.md)"
    fi
fi

LD_LIBRARY_PATH="$TOOLDIR/lib" "$TOOLDIR/mspdebug" --version | head -1
"$(dirname "$0")/msp430-gdb.sh" --version | head -1

cat <<'EOF'

Done. One-time USB permission setup (needs root):
  echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2047", MODE="0666"' \
    | sudo tee /etc/udev/rules.d/99-tiprobe.rules
  sudo udevadm control --reload && sudo udevadm trigger
On WSL2, first attach the LaunchPad from Windows: see doc/DEBUG.md.
EOF
