#!/bin/sh
# Checks plio on the ZYBO without zedBSD (design §7.5): SPL holds the PS
# (DDR, clocks, FCLK_CLK0 up), pl-jtag.sh writes the PL, OpenOCD opens the
# level shifters and the PL resets (zybo_pl_enable), and the registers are
# read and written over GP0 through the DAP's AHB-AP (no core is used for
# GP0).  devcfg's INT_STS is printed before and after the JTAG write (bit 2
# is PCFG_DONE, design §5.6).  The PL-to-PS interrupt is read on the GIC's
# SPI status (PL390 spi_status, 0xF8F01D04 bit 29 = ID 61) through the
# halted CPU0, since the private region is not on the AHB-AP's bus.
#   plan/tools/zybo/plio-jtag.sh [FILE.bit]   (default build/zybo-pl/plio/plio.bit)
# Prints PASS or FAIL.  The LEDs show 0b0101 at the end (LD0 and LD2 on).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
tools=$(cd "$(dirname -- "$0")" && pwd)
. "$tools/zybo-common.sh"
zybo_cfg=$tools/openocd-zybo.cfg
bit=${1:-$zybo_root/build/zybo-pl/plio/plio.bit}
[ -f "$bit" ] || { echo "plio-jtag: no $bit (run pl-build.sh)" >&2; exit 2; }
zybo_lock

"$tools/jtag-boot.sh" --spl-only || { echo "FAIL plio-jtag: SPL"; exit 1; }
zybo_openocd 'init; echo [format "zybo: before the PL: INT_STS 0x%08x STATUS 0x%08x" [zybo_rd 0xF800700C] [zybo_rd 0xF8007014]]; shutdown' |
	grep '^zybo:'
"$tools/pl-jtag.sh" "$bit" || { echo "FAIL plio-jtag: pl-jtag"; exit 1; }

out=$(zybo_openocd '
init
set fails 0
proc expect {what got want} {
	global fails
	if {$got != $want} {
		echo [format "zybo: FAIL %s: 0x%08x, want 0x%08x" $what $got $want]
		incr fails
	} else {
		echo [format "zybo: ok %s: 0x%08x" $what $got]
	}
}
proc spi61 {} {
	targets zynq.cpu0
	return [expr {([lindex [read_memory 0xF8F01D04 32 1 phys] 0] >> 29) & 1}]
}
echo [format "zybo: after the JTAG write: INT_STS 0x%08x STATUS 0x%08x" [zybo_rd 0xF800700C] [zybo_rd 0xF8007014]]
zybo_pl_enable
expect ID [zybo_rd 0x43C00000] 0x504C494F
expect VERSION [zybo_rd 0x43C00004] 1
zybo_wr 0x43C00010 0x12345678
expect SCRATCH [zybo_rd 0x43C00010] 0x12345678
zybo_wr 0x43C00010 0xA5A55A5A
expect SCRATCH2 [zybo_rd 0x43C00010] 0xA5A55A5A
set c1 [zybo_rd 0x43C00014]
sleep 100
set c2 [zybo_rd 0x43C00014]
set d [expr {($c2 - $c1) & 0xFFFFFFFF}]
echo [format "zybo: COUNTER 0x%08x -> 0x%08x in about 100 ms (%d, %.1f MHz)" $c1 $c2 $d [expr {$d / 100000.0}]]
expect "COUNTER advanced" [expr {$d > 1000000}] 1
zybo_wr 0x43C00008 0xF
expect "LED all" [zybo_rd 0x43C00008] 0xF
sleep 500
zybo_wr 0x43C00008 0x5
expect LED [zybo_rd 0x43C00008] 0x5
echo [format "zybo: INPUT 0x%08x (SW0-3 in the low nibble, BTN0-3 above)" [zybo_rd 0x43C0000C]]
expect "unused offset" [zybo_rd 0x43C00040] 0
expect "IRQ line idle" [spi61] 0
zybo_wr 0x43C00018 3
zybo_wr 0x43C00020 2
expect IRQ_STATUS [zybo_rd 0x43C00018] 2
expect "IRQ line masked" [spi61] 0
zybo_wr 0x43C0001C 2
expect "IRQ line raised (ID 61)" [spi61] 1
zybo_wr 0x43C00018 2
expect "IRQ line after W1C" [spi61] 0
zybo_wr 0x43C0001C 0
echo "zybo: fails $fails"
shutdown
')
printf '%s\n' "$out" | grep -E '^zybo:|Error'
case "$out" in
*"zybo: fails 0"*) echo "PASS plio-jtag" ;;
*) echo "FAIL plio-jtag"; exit 1 ;;
esac
