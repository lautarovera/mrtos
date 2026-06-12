#!/bin/sh
# LaunchPad connection doctor: checks every link in the chain from
# Windows USB to a debuggable board, and tells you the next step when
# one is broken. Safe to run any time; changes nothing.
#
# Chain: usbipd (Windows) -> WSL kernel -> USB device visible ->
#        permissions -> mspdebug built -> probe answers.

EZFET_VID=0451
EZFET_PID=bef3
TOOLDIR="${MSPDEBUG_DIR:-$HOME/toolchains/mspdebug}"
if [ -z "${MSP430_GCC_DIR:-}" ]; then
    for _d in "$HOME"/toolchains/msp430-gcc-*_linux64; do
        [ -d "$_d" ] && MSP430_GCC_DIR=$_d
    done
fi
ok()   { printf '  [ok]   %s\n' "$1"; }
bad()  { printf '  [FAIL] %s\n       -> %s\n' "$1" "$2"; FAILED=1; }
FAILED=0

echo "== 1. WSL2: is the eZ-FET visible? =="
found=""
for d in /sys/bus/usb/devices/*/idVendor; do
    [ -e "$d" ] || continue
    vid=$(cat "$d"); pid=$(cat "${d%idVendor}idProduct" 2>/dev/null)
    if [ "$vid" = "$EZFET_VID" ] && [ "$pid" = "$EZFET_PID" ]; then
        found="${d%/idVendor}"
    fi
done
if [ -n "$found" ]; then
    ok "TI eZ-FET ($EZFET_VID:$EZFET_PID) at $found"
else
    bad "no eZ-FET on the WSL USB bus" \
        "attach it from Windows (admin PowerShell):
          winget install usbipd            # once
          usbipd list                      # find the 0451:bef3 busid
          usbipd bind --busid <X-Y>        # once per device
          usbipd attach --wsl --busid <X-Y>  # after every replug/reboot"
fi

echo "== 2. permissions =="
if [ -n "$found" ]; then
    busnum=$(cat "$found/busnum"); devnum=$(cat "$found/devnum")
    node=$(printf '/dev/bus/usb/%03d/%03d' "$busnum" "$devnum")
    if [ -w "$node" ]; then
        ok "$node is writable"
    else
        bad "$node not writable by $(id -un)" \
            "echo 'ATTRS{idVendor}==\"$EZFET_VID\", ATTRS{idProduct}==\"$EZFET_PID\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/99-ezfet.rules
          sudo udevadm control --reload && sudo udevadm trigger
          then detach/attach the device again (usbipd attach --wsl ...)"
    fi
else
    echo "  [skip] no device"
fi

echo "== 3. tools =="
if [ -x "$TOOLDIR/mspdebug" ]; then
    ok "mspdebug at $TOOLDIR ($(LD_LIBRARY_PATH=$TOOLDIR/lib "$TOOLDIR/mspdebug" --version 2>/dev/null | head -1))"
else
    bad "mspdebug not built" "run tools/setup_debug_tools.sh (needs MSP430_GCC_DIR)"
fi
if [ -n "$MSP430_GCC_DIR" ] && [ -e "$MSP430_GCC_DIR/extlib/libncursesw.so.5" ]; then
    ok "msp430-elf-gdb support libs present"
elif [ -z "${MSP430_GCC_DIR:-}" ]; then
    bad "no msp430-gcc under ~/toolchains" "see doc/VALIDATION.md toolchain download"
else
    bad "gdb ncurses5 libs missing" "run tools/setup_debug_tools.sh"
fi

echo "== 4. probe handshake =="
if [ "$FAILED" = 0 ]; then
    if LD_LIBRARY_PATH=$TOOLDIR/lib "$TOOLDIR/mspdebug" ezfet "exit" 2>&1 \
         | grep -qi "fw\|Device:\|MSP430"; then
        ok "eZ-FET answers - you are ready: make run"
    else
        bad "probe did not answer" \
            "check the cable (must be data-capable), replug + usbipd attach, then re-run"
    fi
else
    echo "  [skip] fix the steps above first"
fi

exit $FAILED
