#!/bin/sh
# Prints what the ZYBO's JTAG sees, without changing the board's state: the
# scan chain, the DAP's two APs, the SLCR's ID, boot mode, OCM, clock and PL
# registers, both CPUs' pc (each halted and resumed), and the word at
# 0xFFFFFFF0 that CPU1's BootROM loop waits on.  DDR is not read: before SPL
# has set the DDR controller up, an AHB-AP read there hangs the AHB-AP until
# the next reset.
#   plan/tools/zybo/jtag-probe.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
. "$(dirname -- "$0")/zybo-common.sh"
zybo_lock
zybo_openocd '
init
echo [capture scan_chain]
foreach ap {0 1} { echo "zybo: AP$ap [lindex [split [string trim [capture "zynq.dap info $ap"]] \n] 2]" }
foreach {n a} {
	PSS_IDCODE 0xF8000530 BOOT_MODE 0xF800025C REBOOT_STATUS 0xF8000258
	OCM_CFG 0xF8000910 ARM_PLL_CTRL 0xF8000100 DDR_PLL_CTRL 0xF8000104
	IO_PLL_CTRL 0xF8000108 ARM_CLK_CTRL 0xF8000120 APER_CLK_CTRL 0xF800012C
	UART_CLK_CTRL 0xF8000154 FPGA0_CLK_CTRL 0xF8000170 FPGA_RST_CTRL 0xF8000240
	A9_CPU_RST_CTRL 0xF8000244 LVL_SHFTR_EN 0xF8000900 DEVCFG_STATUS 0xF8007014
	OCM_FFFFFFF0 0xFFFFFFF0
} { echo [format "zybo: %-16s 0x%08x = 0x%08x" $n $a [zybo_rd $a]] }
set was [zynq.cpu0 curstate]
targets zynq.cpu0
if {$was ne "halted"} { halt }
echo "zybo: CPU0 was $was, pc [zybo_pc], cpsr [format 0x%08x [dict get [zynq.cpu0 get_reg cpsr] cpsr]]"
if {$was ne "halted"} { resume }
zynq.cpu1 arp_examine
targets zynq.cpu1
halt
set pc1 [dict get [zynq.cpu1 get_reg pc] pc]
echo [format "zybo: CPU1 pc 0x%08x, cpsr 0x%08x" $pc1 [dict get [zynq.cpu1 get_reg cpsr] cpsr]]
echo [capture "arm disassemble [expr {$pc1 - 8}] 8"]
resume
shutdown
' | sed -n '/TapName/,$p' | grep -v -E 'gdb port|^Info|^target halted|^cpsr:|^MMU:|^shutdown'
