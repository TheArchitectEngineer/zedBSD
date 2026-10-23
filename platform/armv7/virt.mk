# zedBSD armv7: the emulator's virt board.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# RAM starts at 1 GiB.  The emulator boots a raw image the way it boots
# Linux: the image at 64 KiB into RAM (the loader is below it) and the
# device tree's address in r2.  The boot map holds the first 1 GiB of RAM,
# all of what the direct map can.
ARMV7_PHYS_BASE := 0x40000000
ARMV7_KERNEL_PHYS := 0x40010000
ARMV7_BOOT_MAP_BYTES := 0x40000000

ARMV7_BOARD_DEFINE := -DHAL_BOARD_VIRT
ARMV7_BSP_DIR := bsp-virt
ARMV7_BSP_SOURCES := board.c uart.c psci.c timer.c cons.c fwcfg.c framebuffer.c font.c

# The board's console is the HAL's and it publishes no boot device.
ARMV7_PLATFORM_SOURCES := src/kern/platform/hal-console.c
