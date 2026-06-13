#!/bin/sh
# Attach the LaunchPad's eZ-FET to WSL2 from the WSL side - no Windows
# terminal, no admin (for the common case). Idempotent.
#
# Background: WSL2 has no native USB; usbipd-win bridges it. `usbipd
# bind` makes a device shareable and needs admin ONCE (it persists
# across reboots). `usbipd attach` is what you redo every session and
# does NOT need admin - so it runs fine through WSL interop. This script
# automates the recurring attach, and only falls back to an elevated
# (one-click UAC) bind the first time a device has never been shared.
set -eu

PROBE_RE='2047:0013|2047:0014'        # eZ-FET / MSP-FET (Windows-side VID:PID)

# --- locate usbipd.exe (not always on the interop PATH) ---------------
find_usbipd() {
    for p in \
        "/mnt/c/Program Files/usbipd-win/usbipd.exe" \
        "/mnt/c/Program Files (x86)/usbipd-win/usbipd.exe"; do
        [ -x "$p" ] && { echo "$p"; return 0; }
    done
    command -v usbipd.exe 2>/dev/null && return 0
    return 1
}

if ! grep -qiE 'microsoft|wsl' /proc/sys/kernel/osrelease 2>/dev/null; then
    echo "not WSL - nothing to attach (native USB)." >&2
    exit 0
fi
USBIPD=$(find_usbipd) || {
    echo "ERROR: usbipd.exe not found. Install on Windows (admin PowerShell):" >&2
    echo "         winget install usbipd" >&2
    exit 1
}

# --- find the probe's Windows busid + state ---------------------------
line=$("$USBIPD" list 2>/dev/null | tr -d '\r' | grep -E "$PROBE_RE" | head -1 || true)
if [ -z "$line" ]; then
    echo "ERROR: no eZ-FET/MSP-FET ($PROBE_RE) in 'usbipd list'." >&2
    echo "       Plug the LaunchPad into the PC and retry." >&2
    exit 1
fi
busid=$(echo "$line" | awk '{print $1}')
echo "probe at Windows busid $busid"

case "$line" in
    *Attached*)  echo "already attached." ;;
    *"Not shared"*)
        echo "first-time bind needs admin - a UAC prompt will appear, click Yes..."
        case "$USBIPD" in /mnt/*) win=$(wslpath -w "$USBIPD") ;; *) win=usbipd ;; esac
        "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile \
            -Command "Start-Process -FilePath '$win' -ArgumentList 'bind','--busid','$busid' -Verb RunAs -Wait" \
            2>/dev/null || true
        sleep 1
        "$USBIPD" attach --wsl --busid "$busid" 2>&1 | sed 's/^/  /'
        ;;
    *Shared*|*)
        "$USBIPD" attach --wsl --busid "$busid" 2>&1 | sed 's/^/  /'
        ;;
esac

# --- wait for the device + its CDC serial nodes to appear in WSL ------
for _ in $(seq 1 20); do
    for d in /sys/bus/usb/devices/*/idVendor; do
        [ -e "$d" ] || continue
        v=$(cat "$d"); p=$(cat "${d%idVendor}idProduct" 2>/dev/null)
        case "$v:$p" in 2047:0013|2047:0014) seen=1 ;; esac
    done
    [ "${seen:-}" = 1 ] && [ -e /dev/ttyACM0 ] && break
    sleep 0.5
done

if [ "${seen:-}" = 1 ] && [ -e /dev/ttyACM0 ]; then
    echo "attached: probe visible in WSL, /dev/ttyACM0 present. Ready."
else
    echo "WARNING: attach ran but the device/ttyACM did not appear." >&2
    echo "         Try a physical replug, then 'tools/check_board.sh'." >&2
    exit 1
fi
