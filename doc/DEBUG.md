# On-target run & debug — LaunchPad MSP-EXP430FR5994

How to flash, run and source-level-debug mRTOS on the LaunchPad through
its onboard **eZ-FET** probe, from the CLI or from VSCode.

## 1. One-time setup

Prerequisite: the msp430-gcc toolchain (see `doc/VALIDATION.md`) with
`MSP430_GCC_DIR` exported.

```sh
tools/setup_debug_tools.sh
```

builds, with no root needed:

| Tool | Why this way | Lands in |
|---|---|---|
| `mspdebug` (git) | distro packages predate the `ezfet` driver the LaunchPad needs; built against extracted .deb libs | `~/toolchains/mspdebug/` |
| ncurses5 libs | TI's `msp430-elf-gdb` links `libncursesw.so.5`, dropped by modern distros; fetched from the Ubuntu archive | `$MSP430_GCC_DIR/extlib/` (used via `tools/msp430-gdb.sh`) |

USB permission for the probe (this one does need root, once):

```sh
echo 'ATTRS{idVendor}=="0451", ATTRS{idProduct}=="bef3", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-ezfet.rules
sudo udevadm control --reload && sudo udevadm trigger
```

### WSL2: pass the USB device through first

WSL2 has no native USB. In an **elevated (admin) PowerShell on
Windows**, with the LaunchPad plugged in:

```powershell
winget install usbipd             # once; reopen the terminal after
usbipd list                       # find the TI eZ-FET busid (VID 0451:BEF3)
usbipd bind   --busid <X-Y>       # once per device (admin required)
usbipd attach --wsl --busid <X-Y> # after every replug / Windows reboot
```

Then verify inside WSL with `tools/check_board.sh` — it walks the
whole chain (device visible → permissions → tools → probe handshake)
and prints the exact fix for the first broken link. Tip:
`usbipd attach --wsl --busid <X-Y> --auto-attach` keeps re-attaching
across replugs so you do not repeat the last step every time.

## 2. CLI workflow (make targets)

| Target | Effect |
|---|---|
| `make flash` | program the .elf, leave the MCU halted under the probe |
| `make run` | program + reset; the firmware runs free once mspdebug exits |
| `make gdbserver` | program, then serve GDB on `:2000` until Ctrl-C (survives client reconnects) |
| `make gdb` | CLI debug session: attach `msp430-elf-gdb` to a running `make gdbserver` |
| `make debug` | open VSCode here — then press F5 (section 3) |

Typical CLI session (two terminals):

```sh
make gdbserver MSPDEBUG_DRV=ezfet        # terminal 1: stays serving
make gdb                                  # terminal 2
(gdb) break task_blink
(gdb) continue
```

Variables: `MSPDEBUG_DRV` (default `ezfet`; use `tilib` if you have
TI's debug stack installed), `GDB_PORT` (default 2000), `GCC_DIR` /
`MSP430_GCC_DIR` for the toolchain.

## 3. VSCode workflow

`.vscode/` ships ready-made (needs the **C/C++** extension,
`ms-vscode.cpptools` — auto-recommended on open):

1. Launch VSCode from a shell where `MSP430_GCC_DIR` is exported —
   `make debug` does exactly that.
2. Press **F5** ("Debug on LaunchPad (eZ-FET)").
3. The `mspdebug gdbserver` task starts in a panel (programs the board,
   binds `:2000`); the debugger attaches through
   `tools/msp430-gdb.sh`, resets the MCU, and you get breakpoints,
   stepping, watches and memory view over the live LaunchPad.

To stop: disconnect the debug session, then Ctrl-C the gdbserver panel.

### Debugging notes specific to this target

- **Breakpoints**: the FR5994 has 3 hardware breakpoints; GDB falls
  back to software breakpoints, which work in FRAM (it is writable).
  Keep heavy breakpoint use out of flash-wear-sensitive code paths —
  not a practical concern on FRAM.
- **Inspecting tasks**: `mrtos_cur->name` is the running task;
  per-task `sp`, `state`, `prio` live in the TCBs (`t_ctl`, `tcb_blink`
  …). `mrtos_stack_unused(&tcb)` can be called from the gdb prompt:
  `print mrtos_stack_unused(&tcb_blink)`.
- **The tick keeps firing while you sit at a breakpoint** is *false* —
  the eZ-FET halts the whole device including Timer_A (clock control),
  so kernel time freezes with the CPU; timeouts do not pile up.
- **LPM0 idle**: hitting "pause" while idle usually lands in
  `port_idle`'s `bis SR` line — that is normal, not a hang.

## 4. Verified without hardware

The full pipeline (mspdebug gdbserver → `tools/msp430-gdb.sh` → remote
target, breakpoints, stepping) is exercised in this repo against
mspdebug's `sim` driver, so tool breakage shows up before you are at
the bench:

```sh
make gdbserver MSPDEBUG_DRV=sim    # no board needed
make gdb                            # in another terminal
```

What `sim` does **not** model: the eZ-FET USB path, FRAM, clocks and
peripherals — first contact with the real board can still surface
issues in those layers (driver quirks are usually visible in mspdebug's
first three output lines).
