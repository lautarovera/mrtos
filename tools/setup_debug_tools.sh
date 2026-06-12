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

LD_LIBRARY_PATH="$TOOLDIR/lib" "$TOOLDIR/mspdebug" --version | head -1
"$(dirname "$0")/msp430-gdb.sh" --version | head -1

cat <<'EOF'

Done. One-time USB permission setup (needs root):
  echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2047", MODE="0666"' \
    | sudo tee /etc/udev/rules.d/99-tiprobe.rules
  sudo udevadm control --reload && sudo udevadm trigger
On WSL2, first attach the LaunchPad from Windows: see doc/DEBUG.md.
EOF
