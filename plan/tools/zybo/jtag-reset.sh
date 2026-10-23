#!/bin/sh
# Resets the ZYBO's PS (and PL) over JTAG and checks that the debugger can
# attach again.  The board has no SRST on its JTAG, so the reset is the SLCR's
# PSS_RST_CTRL written through the DAP's AHB-AP; if the AHB-AP is stuck (an
# access to DDR before SPL set it up hangs it) the same write goes through a
# halted CPU0 instead.  The DAP drops with the reset, so attaching again is a
# second OpenOCD session.
#   plan/tools/zybo/jtag-reset.sh [--halt]
# --halt leaves CPU0 halted in the BootROM's JTAG wait and prints its pc.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"
halt=
[ "${1:-}" = --halt ] && halt=1
zybo_lock

out=$(zybo_openocd 'init; zybo_ps_reset; shutdown')
case "$out" in
*"zybo: PS reset requested"*) ;;
*)
	echo "zybo: the AHB-AP reset failed, trying through CPU0" >&2
	out=$(zybo_openocd 'init; zybo_ps_reset_via_cpu; shutdown')
	case "$out" in
	*"zybo: PS reset requested"*) ;;
	*) printf '%s\n' "$out" | grep -E 'Error|zybo' >&2; exit 1 ;;
	esac
	;;
esac
sleep 1

if [ -n "$halt" ]; then
	check='init; zybo_check_jtag_mode; halt; echo "zybo: CPU0 halted at [zybo_pc]"; shutdown'
else
	check='init; zybo_check_jtag_mode; echo [format "zybo: REBOOT_STATUS 0x%08x" [zybo_rd 0xF8000258]]; shutdown'
fi
out=$(zybo_openocd "$check")
printf '%s\n' "$out" | grep -E '^zybo:|Error'
case "$out" in
*"zybo: BOOT_MODE"*) echo "zybo: reset done" ;;
*) exit 1 ;;
esac
