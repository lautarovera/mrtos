#!/bin/sh
# LaunchPad connection doctor: checks every link in the chain from
# Windows USB to a debuggable board, and tells you the next step when
# one is broken. Safe to run any time; changes nothing.
#
# Chain: usbipd (Windows) -> WSL kernel -> USB device visible ->
#        permissions -> mspdebug built -> probe answers.

# Known TI debug probes: modern eZ-FET (LaunchPads) enumerates as
# 2047:0013 "MSP Tools Driver"; 2047:0014 is the MSP-FET; 0451:bef3
# is the older eZ-FET VID some firmware revisions report.
PROBE_IDS="2047:0013 2047:0014 0451:bef3"
TOOLDIR="${MSPDEBUG_DIR:-$HOME/toolchains/mspdebug}"
if [ -z "${MSP430_GCC_DIR:-}" ]; then
    for _d in "$HOME"/toolchains/msp430-gcc-*_linux64; do
        [ -d "$_d" ] && MSP430_GCC_DIR=$_d
    done
fi
ok()   { printf '  [ok]   %s\n' "$1"; }
bad()  { printf '  [FAIL] %s\n       -> %s\n' "$1" "$2"; FAILED=1; }
FAILED=0

echo "== 1. WSL2: is the debug probe visible? =="
found=""; found_id=""
for d in /sys/bus/usb/devices/*/idVendor; do
    [ -e "$d" ] || continue
    id="$(cat "$d"):$(cat "${d%idVendor}idProduct" 2>/dev/null)"
    for probe in $PROBE_IDS; do
        if [ "$id" = "$probe" ]; then
            found="${d%/idVendor}"; found_id=$id
        fi
    done
done
if [ -n "$found" ]; then
    ok "TI debug probe ($found_id, $(cat "$found/product" 2>/dev/null)) at $found"
else
    bad "no TI probe on the WSL USB bus" \
        "attach it from Windows (admin PowerShell):
          winget install usbipd            # once
          usbipd list                      # find the 'MSP Debug Interface' busid (2047:0013)
          usbipd bind --busid <X-Y>        # once per device
          usbipd attach --wsl --busid <X-Y> --auto-attach"
fi

echo "== 2. permissions =="
if [ -n "$found" ]; then
    busnum=$(cat "$found/busnum"); devnum=$(cat "$found/devnum")
    node=$(printf '/dev/bus/usb/%03d/%03d' "$busnum" "$devnum")
    if [ -w "$node" ]; then
        ok "$node is writable"
    else
        vid=${found_id%:*}
        bad "$node not writable by $(id -un)" \
            "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"$vid\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/99-tiprobe.rules
          sudo udevadm control --reload && sudo udevadm trigger
          then re-run this script (replug/reattach if still failing)"
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

echo "== 4. TI debug stack (FRAM write capability) =="
TI_DLL=""
for d in /opt/ti/msp430-gcc-full/bin "$HOME/ti/msp430-gcc-full/bin"; do
    [ -e "$d/libmsp430.so" ] && TI_DLL=$d
done
if [ -n "$TI_DLL" ]; then
    ok "libmsp430.so at $TI_DLL (tilib driver)"
    DRV=tilib
else
    bad "TI debug stack not installed - ezfet fallback CANNOT write FR5994 FRAM" \
        "sudo ~/toolchains/full-installer/msp430-gcc-full-linux-x64-installer-9.3.1.2.run \\
              --mode unattended --prefix /opt/ti/msp430-gcc-full"
    DRV=ezfet
fi
if [ -n "$found" ] && ! ls /dev/ttyACM* >/dev/null 2>&1; then
    bad "probe attached but no /dev/ttyACM* (CDC driver unbound)" \
        "udev tty rule + physically replug the board:
          echo 'KERNEL==\"ttyACM*\", ATTRS{idVendor}==\"2047\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/99-tiprobe-tty.rules
          sudo udevadm control --reload"
fi

echo "== 5. probe handshake =="
if [ "$FAILED" = 0 ]; then
    if LD_LIBRARY_PATH=$TI_DLL:$TOOLDIR/lib "$TOOLDIR/mspdebug" $DRV "exit" 2>&1 \
         | grep -q "Device:"; then
        ok "probe answers and identified the device - you are ready: make run"
    else
        bad "probe did not answer" \
            "check the cable (must be data-capable), replug + usbipd attach, then re-run"
    fi
else
    echo "  [skip] fix the steps above first"
fi

exit $FAILED
