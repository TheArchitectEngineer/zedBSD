# zedBSD arm64: the Mali Valhall GPU driver and its display node.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The GPU's attach, the S7D's clocks and resets, the GPU control block,
# its address spaces, the firmware's file and image, the command stream
# front end and its interface, the driver's own stream, and the display
# node on the bootloader's screen and the VPU's first OSD plane.
ARM64_MALI_SOURCES := \
	src/drivers/gpu/mali/mali-attach.c \
	src/drivers/gpu/mali/mali-s7d.c \
	src/drivers/gpu/mali/mali-gpu.c \
	src/drivers/gpu/mali/mali-pagetable.c \
	src/drivers/gpu/mali/mali-mmu.c \
	src/drivers/gpu/mali/mali-firmware-file.c \
	src/drivers/gpu/mali/mali-firmware-image.c \
	src/drivers/gpu/mali/mali-interface.c \
	src/drivers/gpu/mali/mali-csf.c \
	src/drivers/gpu/mali/mali-queue.c \
	src/drivers/gpu/mali/display/osd.c \
	src/drivers/gpu/mali/display/display.c
