# The image for the amdrdna runs on the RX 5600 XT machine (10.0.10.3, started from USB): the CI amd64 image
# (config/ci/config-amd64.mk, amdrdna built in) with the text console carrying the kernel's lines (the stage marks
# stay on the firmware's screen), the control device and its tool, and the boot words of one run.
#   AMDRDNA_RUN=A1v (default: the stages that only read) or A2, A3, ... as the stages come.
# Build:  plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=A1v
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include config/ci/config-amd64.mk

CONFIG_DRIVER_PCI_AMDRDNA := y
CONFIG_AMDRDNA_CONTROL := y
ZEDBSD_USER_PROGRAMS += amdrdnactl

ZEDBSD_GRAPHICAL_BOOT := n
ZEDBSD_GRAPHICAL_LOGIN := n
ZEDBSD_BOOT_KERNEL_MESSAGES := y

AMDRDNA_RUN ?= A1v
ZEDBSD_BOOT_EXTRA_LINES := amdrdna.auto=$(AMDRDNA_RUN)
