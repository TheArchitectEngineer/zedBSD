# The amd64 image with the generic GHCI 1.0 GPU driver and the GHCI tools, for WS290 (and later a
# KC705 GHCI board passed through to a QEMU guest): the CI amd64 configuration with the driver and the
# kernel's lines on the screen.
# Kernel:  make ZEDBSD_CONFIG=plan/ws290/tests/config-amd64-ghci.mk BUILD=build/<dir> build/<dir>/vmunix
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_GHCI := y
ZEDBSD_USER_PROGRAMS += ghcictl ghci-display

ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y
