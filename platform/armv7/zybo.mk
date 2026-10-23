# zedBSD armv7: the Digilent ZYBO (Zynq-7010), and the emulator's
# xilinx-zynq-a9 machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# RAM starts at 0.  The JTAG boot (plan/tools/zybo/jtag-boot.sh) and the
# emulator's ELF loader put the image at 1 MiB, its linked load address.
# The first MiB is never mapped nor used: in the JTAG boot it is on-chip
# memory, a hole that aborts, and an alias of the high on-chip memory
# where the second processor waits.  The boot map holds the board's
# 512 MiB of DDR and nothing past it.
ARMV7_PHYS_BASE := 0x00000000
ARMV7_KERNEL_PHYS := 0x00100000
ARMV7_BOOT_MAP_BYTES := 0x20000000
ARMV7_LOW_RAM_SKIP := 0x00100000

ARMV7_BOARD_DEFINE := -DHAL_BOARD_ZYBO
ARMV7_BSP_DIR := bsp-zybo
ARMV7_BSP_SOURCES := board.c slcr.c uart.c timer.c
ARMV7_BSP_ASM_SOURCES := early.S

# The board's console is the HAL's and it publishes no boot device yet.
ARMV7_PLATFORM_SOURCES := src/kern/platform/hal-console.c
